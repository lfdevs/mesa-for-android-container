#!/bin/bash

set -e

arch=s390x

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL="libssl-dev"

apt-get -y install "$STABLE_EPHEMERAL"

. .gitlab-ci/container/build-mold.sh

apt-get purge -y "$STABLE_EPHEMERAL"

. .gitlab-ci/container/cross_build.sh
