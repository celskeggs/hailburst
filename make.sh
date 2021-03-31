#!/bin/bash
set -e -u

LOCAL="$(dirname "$(readlink -f "$0")")"
TREE="${LOCAL}/tree"
EXT="${LOCAL}/ext"
mkdir -p "${TREE}"
make -C /home/user/Binary/buildroot-2021.02/ O="${TREE}" BR2_EXTERNAL="${EXT}" "$@"
