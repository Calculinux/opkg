#!/bin/bash

set -ex

SCRIPT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

docker build $SCRIPT_PATH -t opkg-dev
docker run -it \
    -v $(pwd):/root/ \
    -e HISTFILE=/root/.bash_history \
    opkg-dev
