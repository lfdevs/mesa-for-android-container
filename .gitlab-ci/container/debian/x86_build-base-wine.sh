#!/bin/bash

set -e
set -o xtrace

# Installing wine, need this for testing mingw or nine

# We need multiarch for Wine
dpkg --add-architecture i386
apt-get update
apt-get install -y --no-remove \
      wine \
      wine32 \
      wine64 \
      xvfb

# Used to initialize the Wine environment to reduce build time
wine64 whoami.exe

