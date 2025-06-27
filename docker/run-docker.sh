#!/bin/bash

set -ex

SCRIPT_PATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
PROJECT_ROOT=$(realpath "${SCRIPT_PATH}/..")

docker build $SCRIPT_PATH -t opkg-dev
docker run -it \
    -v "${PROJECT_ROOT}":/usr/local/src/opkg/:rw \
    -v /etc/passwd:/etc/passwd:ro \
    -v /etc/group:/etc/group:ro \
    -u $(id -u):$(id -g) \
    opkg-dev
