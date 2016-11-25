#!/bin/sh

set -e

TAG=justincormack/netlisten

IMAGE=$(tar cf - Dockerfile listen.c | docker build -q -)
docker run $IMAGE | docker build -q -t $TAG -
