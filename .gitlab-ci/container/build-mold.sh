#!/bin/bash

set -ex

MOLD_VERSION="1.6.0"

git clone -b v"$MOLD_VERSION" --single-branch --depth 1 https://github.com/rui314/mold.git
cd mold
make
make install
cd ..
rm -rf mold
