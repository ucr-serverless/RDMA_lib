#!/bin/bash
sudo apt update && sudo apt install -y flex bison build-essential dwarves libssl-dev libelf-dev \
            libnuma-dev pkg-config python3-pip python3-pyelftools \
            libconfig-dev clang uuid-dev sysstat clang-format libglib2.0-dev apache2-utils cmake libjson-c-dev gdb libstdc++-12-dev nlohmann-json3-dev ccache systemd-coredump

sudo pip3 install meson ninja
