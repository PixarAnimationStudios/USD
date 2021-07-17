#!/bin/bash

CMD=$1

if [[ -z $CMD ]]
then
	CMD="/bin/bash"
fi

XAUTH=$HOME/.Xauthority

echo "run: $CMD"

docker run --rm \
			-it \
			--network=host \
			--gpus all \
			--privileged \
			-e DISPLAY \
			-e NVIDIA_DRIVER_CAPABILITIES=compute,utility,graphics \
			-e QT_X11_NO_MITSHM=1 \
			-v /tmp/.X11-unix:/tmp.X11-unix \
			-v $XAUTH:/root/.Xauthority \
			ubuntu-focal-usd \
			$CMD
