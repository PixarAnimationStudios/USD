//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "pxr/pxr.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/staticData.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/iterator.h"
#include "pxr/base/arch/defines.h"
#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/arch/systemInfo.h"
#include "pxr/base/arch/errno.h"

#include <boost/assign.hpp>
#include <boost/functional/hash.hpp>
#include <boost/noncopyable.hpp>
#include "pxr/base/tf/hashset.h"

#include <set>
#include <string>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#if !defined(ARCH_OS_WINDOWS)
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>
#include <dirent.h>
#include <utime.h>
#else
#include <Windows.h>
#include <Shellapi.h>
#include <ShlwAPI.h>
#include <sys/utime.h>
#endif
using std::set;
using std::string;
using std::vector;

PXR_NAMESPACE_OPEN_SCOPE

#if defined(ARCH_OS_WINDOWS)
// Test if file exists (attribute == 0) or has or doesn't have particular
// attributes by testing: (actual & attributes) == expected
bool
Tf_HasAttribute(
    string const& path,
    bool resolveSymlinks,
    DWORD attribute,
    DWORD expected)
{
    if (path.empty()) {
        SetLastError(ERROR_SUCCESS);
        return false;
    }

    // On Linux, a trailing slash forces the resolution of symlinks
    // (https://man7.org/linux/man-pages/man7/path_resolution.7.html).
    // We need to provide the same behavior on Windows.
    if (path.back() == '/' || path.back() == '\\')
        resolveSymlinks = true;

    const DWORD attribs = GetFileAttributes(path.c_str());
    if (attribs == INVALID_FILE_ATTRIBUTES) {
        if (attribute == 0 && GetLastError() == ERROR_FILE_NOT_FOUND) {
            // Don't report an error if we're just testing existence.
            SetLastError(ERROR_SUCCESS);
        }
        return false;
    }
    if (!resolveSymlinks || (attribs & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        return attribute == 0 || (attribs & attribute) == expected;
    }

    // Read symlinks until we find the real file.
    return Tf_HasAttribute(TfReadLink(path.c_str()), resolveSymlinks,
                           attribute, expected);
}

// Same as above but the bits in attribute must all be set.
bool
Tf_HasAttribute(
    string const& path,
    bool resolveSymlinks,
    DWORD attribute)
{
    return Tf_HasAttribute(path, resolveSymlinks, attribute, attribute);
}
#endif

static bool
Tf_Stat(string const& path, bool resolveSymlinks, ArchStatType* st = 0)
{
    if (path.empty()) {
        return false;
    }

    ArchStatType unused;
    if (!st) {
        st = &unused;
    }

#if defined(ARCH_OS_WINDOWS)
    if (!resolveSymlinks) {
        TF_CODING_ERROR("resolveSymlinks must be true on Windows");
        return false;
    }
    return _stat64(path.c_str(), st) == 0;
#else
    int result = resolveSymlinks ?
        stat(path.c_str(), st) : lstat(path.c_str(), st);
    return result == 0;
#endif
}

bool
TfPathExists(string const& path, bool resolveSymlinks)
{
#if defined(ARCH_OS_WINDOWS)
    return Tf_HasAttribute(path, resolveSymlinks, 0);
#else
    if (Tf_Stat(path, resolveSymlinks)) {
        return true;
    }
    // Reset error code if path doesn't exist.
    if (errno == ENOENT) {
        errno = 0;
    }
    return false;
#endif
}

bool
TfIsDir(string const& path, bool resolveSymlinks)
{
#if defined (ARCH_OS_WINDOWS)
    // Report not a directory if path is a symlink and resolveSymlinks is
    // false.
    if (!resolveSymlinks && TfIsLink((path)))
        return false;
    return Tf_HasAttribute(path, resolveSymlinks,
                    FILE_ATTRIBUTE_DIRECTORY);
#else
    ArchStatType st;
    if (Tf_Stat(path, resolveSymlinks, &st)) {
        return S_ISDIR(st.st_mode);
    }
    return false;
#endif
}

bool
TfIsFile(string const& path, bool resolveSymlinks)
{
#if defined (ARCH_OS_WINDOWS)
    // Report not a file if path is a symlink and resolveSymlinks is false.
    if (!resolveSymlinks && TfIsLink((path)))
        return false;
    return Tf_HasAttribute(path, resolveSymlinks,
                    FILE_ATTRIBUTE_DIRECTORY,
                    0);
#else
    ArchStatType st;
    if (Tf_Stat(path, resolveSymlinks, &st)) {
        return S_ISREG(st.st_mode);
    }
    return false;
#endif
}

bool
TfIsLink(string const& path)
{
#if defined(ARCH_OS_WINDOWS)
    return Tf_HasAttribute(path, /* resolveSymlinks = */ false,
                           FILE_ATTRIBUTE_REPARSE_POINT) &&
                           !TfReadLink(path).empty();
#else
    ArchStatType st;
    if (Tf_Stat(path, /* resolveSymlinks */ false, &st)) {
        return S_ISLNK(st.st_mode);
    }
#endif
    return false;
}

bool
TfIsWritable(string const& path)
{
#if defined(ARCH_OS_LINUX)
    // faccessat accounts for mount read-only status. For maintaining legacy
    // behavior, use faccessat instead of access so we can use the effective
    // UID instead of the real UID. 
    return faccessat(AT_FDCWD, path.c_str(), W_OK, AT_EACCESS) == 0;
#else
    ArchStatType st;
    if (Tf_Stat(path, /* resolveSymlinks */ true, &st)) {
        return ArchStatIsWritable(&st);
    }
    return false;
#endif
}

bool
TfIsDirEmpty(string const& path)
{
    if (!TfIsDir(path))
        return false;
#if defined(ARCH_OS_WINDOWS)
    return PathIsDirectoryEmpty(path.c_str()) == TRUE;
#else
    if (DIR *dirp = opendir(path.c_str()))
    {
        struct dirent *dent;
        while ((dent = readdir(dirp)) != NULL)
        {
            if ((dent->d_ino > 0) &&
                (strcmp(dent->d_name, ".") != 0) &&
                (strcmp(dent->d_name, "..") != 0)) {
                (void) closedir(dirp);
                return false;
            }
        }
        (void) closedir(dirp);
        return true;
    }
    return false;
#endif
}

bool
TfSymlink(string const& src, string const& dst)
{
#if defined(ARCH_OS_WINDOWS)
    if (CreateSymbolicLink(dst.c_str(), src.c_str(),
                           TfIsDir(src) ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0)) {
        return true;
    }

    // Translate lack of privilege to EPERM,  Anything else translates
    // to something else.  This is used by tests to disable symlink
    // testing.
    errno = EACCES;
    if (GetLastError() == ERROR_PRIVILEGE_NOT_HELD) {
        errno = EPERM;
    }
    return false;
#else
    return (symlink(src.c_str(), dst.c_str()) != -1);
#endif
}

bool
TfDeleteFile(std::string const& path)
{
    if (ArchUnlinkFile(path.c_str()) != 0) {
        TF_RUNTIME_ERROR("Failed to delete '%s': %s", 
                path.c_str(), ArchStrerror(errno).c_str());
        return false;
    }
    return true;
}

bool
TfMakeDir(string const& path, int mode)
{
#if defined(ARCH_OS_WINDOWS)
    return CreateDirectory(path.c_str(), nullptr) == TRUE;
#else
    // Default mode is 0777
    if (mode == -1)
        mode = S_IRWXU|S_IRWXG|S_IRWXO;

    return (mkdir(path.c_str(), mode) != -1);
#endif
}

static bool
Tf_MakeDirsRec(string const& path, int mode, bool existOk)
{
#if defined(ARCH_OS_WINDOWS)
    static const string pathsep = "\\/";
#else
    static const string pathsep = "/";
#endif

    const string head = TfStringTrimRight(TfGetPathName(path), pathsep.c_str());
    const string tail = TfGetBaseName(path);

    if (!head.empty() && !tail.empty() && !TfPathExists(head) && head != path) {
        if (!Tf_MakeDirsRec(head, mode, existOk)) {
#if defined(ARCH_OS_WINDOWS)
            if (GetLastError() != ERROR_ALREADY_EXISTS)
#else
            if (errno != EEXIST)
#endif
            {
                return false;
            }
        }
    }

    if (!TfMakeDir(path, mode)) {
        if (!existOk || !TfIsDir(path)) {
            return false;
        }
    }

    return true;
}

bool
TfMakeDirs(string const& path, int mode, bool existOk)
{
    return path.empty() ? false
        : Tf_MakeDirsRec(TfNormPath(path), mode, existOk);
}

bool
TfReadDir(
    const string& dirPath,
    vector<string>* dirnames,
    vector<string>* filenames,
    vector<string>* symlinknames,
    string *errMsg)
{
#if defined(ARCH_OS_WINDOWS)
    char szPath[MAX_PATH];
    WIN32_FIND_DATA fdFile;
    HANDLE hFind = NULL;

    PathCombine(szPath, dirPath.c_str(), "*.*");

    if((hFind = FindFirstFile(szPath, &fdFile)) == INVALID_HANDLE_VALUE)
    {
        if (errMsg) {
            *errMsg = TfStringPrintf("Path not found: %s", szPath);
        }
        return false;
    }
    else
    {
        do
        {
            if(strcmp(fdFile.cFileName, ".") != 0
                && strcmp(fdFile.cFileName, "..") != 0)
            {
                if(fdFile.dwFileAttributes &FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (dirnames)
                        dirnames->push_back(fdFile.cFileName);
                }
                else
                {
                    if (filenames)
                        filenames->push_back(fdFile.cFileName);
                }
            }
        }
        while (FindNextFile(hFind, &fdFile));

        FindClose(hFind);

        return true;
    }
#else
    DIR *dir;
    struct dirent entry;
    struct dirent *result;
    int rc;

    if ((dir = opendir(dirPath.c_str())) == NULL) {
        if (errMsg) {
            *errMsg = TfStringPrintf("opendir failed: %s", 
                        ArchStrerror(errno).c_str());
        }
        return false;
    }

    for (rc = readdir_r(dir, &entry, &result);
         result && rc == 0;
         rc = readdir_r(dir, &entry, &result)) {

        if (strcmp(entry.d_name, ".") == 0 || 
            strcmp(entry.d_name, "..") == 0)
            continue;

        bool entryIsDir = false;
        bool entryIsLnk = false;
#if defined(_DIRENT_HAVE_D_TYPE)
        // If we are on a BSD-like system, and the underlying filesystem has
        // support for it, we can use dirent.d_type to avoid lstat.
        if (entry.d_type == DT_DIR) {
            entryIsDir = true;
        } else if (entry.d_type == DT_LNK) {
            entryIsLnk = true;
        } else if (entry.d_type == DT_UNKNOWN) {
#endif
            // If d_type is not available, or the filesystem has no support
            // for d_type, fall back to lstat.
            ArchStatType st;
            if (fstatat(dirfd(dir), entry.d_name, 
                        &st, AT_SYMLINK_NOFOLLOW) != 0)
                continue;

            if (S_ISDIR(st.st_mode)) {
                entryIsDir = true;
            } else if (S_ISLNK(st.st_mode)) {
                entryIsLnk = true;
            }
#if defined(_DIRENT_HAVE_D_TYPE)
        }
#endif

        if (entryIsDir) {
            if (dirnames)
                dirnames->push_back(entry.d_name);
        } else if (entryIsLnk) {
            if (symlinknames)
                symlinknames->push_back(entry.d_name);
        } else if (filenames) {
            filenames->push_back(entry.d_name);
        }
    }

    closedir(dir);
#endif
    return true;
}    

static void
Tf_ReadDir(
    const string& dirPath,
    const TfWalkErrorHandler& onError,
    vector<string>* dirnames,
    vector<string>* filenames,
    vector<string>* symlinknames)
{
    if (!(TF_VERIFY(dirnames) &&
          TF_VERIFY(filenames) &&
          TF_VERIFY(symlinknames)))
        return;

    string errMsg;
    if (!TfReadDir(dirPath, dirnames, filenames, symlinknames, &errMsg))
        if (onError) {
            onError(dirPath, errMsg);
        }
}    

struct Tf_FileId {
    Tf_FileId(const ArchStatType& st)
        : dev(st.st_dev), ino(st.st_ino)
    { }

    bool operator==(const Tf_FileId& other) const {
        return dev == other.dev && ino == other.ino;
    }

    dev_t dev;
    ino_t ino;
};

static size_t hash_value(const Tf_FileId& fileId) {
    size_t seed = 0;
    boost::hash_combine(seed, fileId.dev);
    boost::hash_combine(seed, fileId.ino);
    return seed;
}

typedef TfHashSet<Tf_FileId, boost::hash<Tf_FileId> > Tf_FileIdSet;

static bool
Tf_WalkDirsRec(
    const string& dirpath,
    const TfWalkFunction& func,
    bool topDown,
    const TfWalkErrorHandler& onError,
    bool followLinks,
    Tf_FileIdSet* linkTargets)
{
    if (!TF_VERIFY(linkTargets))
        return false;

    vector<string> dirnames, filenames, symlinknames;
    Tf_ReadDir(dirpath, onError, &dirnames, &filenames, &symlinknames);

    // If we're following symbolic links, stat each symlink name returned by
    // readdir. If the symlink is to a directory, record information about the
    // directory to which the symlink points. If we visit this directory via a
    // symbolic link again, omit it from the directory list to prevent the
    // directory from being followed until stat() eventually fails with ELOOP.
    if (followLinks) {
        for (const auto& name : symlinknames) {
            ArchStatType st;
            if (Tf_Stat(string(dirpath + "/" + name).c_str(),
                    /* resolveSymlinks */ true, &st)) {
                if (S_ISDIR(st.st_mode)) {
                    Tf_FileId fileId(st);
                    if (linkTargets->find(fileId) != linkTargets->end())
                        continue;
                    linkTargets->insert(fileId);

                    dirnames.push_back(name);
                } else
                    filenames.push_back(name);
            } else
                filenames.push_back(name);
        }
    } else
        filenames.insert(filenames.end(),
            symlinknames.begin(), symlinknames.end());

    if (topDown && !func(dirpath, &dirnames, filenames))
       return false;

    for (const auto& name : dirnames) {
        if (!Tf_WalkDirsRec(dirpath + "/" + name,
                func, topDown, onError, followLinks, linkTargets))
            return false;
    }

    if (!topDown && !func(dirpath, &dirnames, filenames))
       return false;

    return true;
}

void
TfWalkDirs(
    const string& top,
    TfWalkFunction func,
    bool topDown,
    TfWalkErrorHandler onError,
    bool followLinks)
{
    if (!TfIsDir(top, /* followSymlinks */ true)) {
        if (onError)
            onError(top, TfStringPrintf("%s is not a directory", top.c_str()));
        return;
    }

    Tf_FileIdSet linkTargets;
    Tf_WalkDirsRec(TfNormPath(top),
        func, topDown, onError, followLinks, &linkTargets);
}

void
TfWalkIgnoreErrorHandler(string const& path, string const& msg)
{
}

static bool
Tf_RmTree(string const& dirpath,
          vector<string> *dirnames,
          vector<string> const& filenames,
          TfWalkErrorHandler onError)
{
    vector<string>::const_iterator it;
    for (it = filenames.begin(); it != filenames.end(); ++it) {
        string path = dirpath + "/" + *it;
        if (ArchUnlinkFile(path.c_str()) != 0) {
            // CODE_COVERAGE_OFF this could happen if the file is removed by
            // another process before we get there, or a file exists but is
            // not writable by us, or the parent directory is not writable by
            // us.
            if (onError) {
                onError(dirpath, 
                    TfStringPrintf("ArchUnlinkFile failed for '%s': %s",
                    path.c_str(), ArchStrerror(errno).c_str()));
            }
            // CODE_COVERAGE_ON
        }
    }

    if (ArchRmDir(dirpath.c_str()) != 0) {
        // CODE_COVERAGE_OFF this could happen for all the same reasons the
        // ArchUnlinkFile above could fail.
        if (onError) {
            onError(dirpath, TfStringPrintf("rmdir failed for '%s': %s",
                dirpath.c_str(), ArchStrerror(errno).c_str()));
        }
        // CODE_COVERAGE_ON
    }

    return true;
}

static void
Tf_RmTreeRaiseErrors(string const& path, string const& msg)
{
    TF_RUNTIME_ERROR("failed to remove '%s': %s", path.c_str(), msg.c_str());
}

void
TfRmTree(string const& path, TfWalkErrorHandler onError)
{
    namespace ph = std::placeholders;
    TfWalkDirs(path,
               std::bind(Tf_RmTree, ph::_1, ph::_2, ph::_3, onError),
               /* topDown */ false,
               onError ? onError : Tf_RmTreeRaiseErrors);
}

static bool
Tf_ListDir(string const& dirpath,
           vector<string> *dirnames,
           vector<string> const& filenames,
           vector<string> *paths,
           bool recursive)
{
    for (vector<string>::const_iterator it = dirnames->begin();
         it != dirnames->end(); ++it) {
        paths->push_back(dirpath + "/" + *it + "/");
    }

    for (vector<string>::const_iterator it = filenames.begin();
         it != filenames.end(); ++it)
        paths->push_back(dirpath + "/" + *it);

    return recursive;
}

vector<string>
TfListDir(string const& path, bool recursive)
{
    namespace ph = std::placeholders;
    vector<string> result;
    TfWalkDirs(path, std::bind(Tf_ListDir, ph::_1, ph::_2, ph::_3,
                               &result, recursive));
    return result;
}

TF_API bool
TfTouchFile(string const &fileName, bool create)
{
    if (create) {
#if !defined(ARCH_OS_WINDOWS)
        // Attempt to create the file so it is readable and writable by user,
        // group and other.
        int fd = open(fileName.c_str(),
            O_WRONLY | O_CREAT | O_NONBLOCK | O_NOCTTY,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if (fd == -1)
            return false;
        close(fd);
#else
        HANDLE fileHandle = ::CreateFile(fileName.c_str(),
            GENERIC_WRITE,          // open for write
            0,                      // not for sharing
            NULL,                   // default security
            CREATE_ALWAYS,          // overwrite existing
            FILE_ATTRIBUTE_NORMAL,  //normal file
            NULL);                  // no template

        if (fileHandle == INVALID_HANDLE_VALUE) {
            return false;
        }

        // Close the file
        ::CloseHandle(fileHandle);
#endif
    }

    // Passing NULL to the 'times' argument sets both the atime and mtime to
    // the current time, with millisecond precision.
#if defined(ARCH_OS_WINDOWS)
    return _utime(fileName.c_str(), /* times */ NULL) == 0;
#else
    return utimes(fileName.c_str(), /* times */ NULL) == 0;
#endif
}

PXR_NAMESPACE_CLOSE_SCOPE
