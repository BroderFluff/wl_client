#!/bin/bash

mkdir -p wl/
cd wl/

wayland-scanner private-code \
    < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
    > xdg-shell-protocol.c

wayland-scanner client-header \
    < /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
    > xdg-shell-client-protocol.h

cc -c xdg-shell-protocol.c -o xdg-shell-protocol.o
