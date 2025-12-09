#!/usr/bin/env bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# ALPINE_X86_64_BUILD_TAG
# DEBIAN_BUILD_BASE_TAG
# FEDORA_X86_64_BUILD_TAG

rm -f /usr/lib/python3.*/EXTERNALLY-MANAGED

# We need at least 1.7.0 for proper rust binding generation
pip3 install 'meson==1.7.0'
