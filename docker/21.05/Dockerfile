FROM ubuntu:focal

LABEL description="ubuntu-focal-usd"

ENV DEBIAN_FRONTEND=noninteractive

RUN rm /bin/sh && ln -s /bin/bash /bin/sh

# Install build dependencies 
RUN apt-get update -qq
RUN apt-get install -y cmake \
                       build-essential \
                       curl \
                       git \
                       software-properties-common \
                       wget \
                       libgl1-mesa-dev \
                       mesa-utils \
                       rsync \
                       libqt5gui5

# Install python 3.7
RUN add-apt-repository -y ppa:deadsnakes/ppa
RUN apt-get install -y python3.7 libpython3.7-dev
RUN ln -s /usr/bin/python3.7 /usr/bin/python

# Install pip
RUN curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py
RUN python3.7 get-pip.py

# Install relevant python deps
RUN pip install --upgrade pip
RUN pip install pyopengl pyside2

# Clone and install USD
WORKDIR /root/
RUN git clone https://github.com/PixarAnimationStudios/USD.git

# Release 21.05 (https://github.com/PixarAnimationStudios/USD/commit/090ef0d849ced875b3b99a15e454b69148ccd8e1)
RUN cd USD && git checkout 090ef0d849ced875b3b99a15e454b69148ccd8e1

RUN python USD/build_scripts/build_usd.py /usr/local/USD

RUN rsync -a /usr/local/USD/ /usr/local
ENV PYTHONPATH "${PYTHONPATH}:/usr/local/lib/python"

# Cleanup
RUN rm -rf /usr/local/USD
