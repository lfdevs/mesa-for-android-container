#!/usr/bin/env bash

CI_COMMON_DIR=$(dirname -- "${BASH_SOURCE[0]}")

mkdir -p /tmp/.X11-unix
export DISPLAY=:0

WAYLAND_DISPLAY=wayland-0
weston --config="$CI_COMMON_DIR/weston.ini" --socket="$WAYLAND_DISPLAY" "$@" &
export WAYLAND_DISPLAY

while [ ! -S /tmp/.X11-unix/X0 ]; do sleep 1; done
