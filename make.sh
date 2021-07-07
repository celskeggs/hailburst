#!/bin/bash
set -e -u

LOCAL="$(dirname "$(readlink -f "$0")")"
TREE="${LOCAL}/tree"
EXT="${LOCAL}/ext"
source "${LOCAL}/local.config" # for BUILDROOT_DIR
if [ ! -d "${BUILDROOT_DIR}" ]
then
	echo "Cannot find buildroot directory" 1>&2
	exit 1
fi
mkdir -p "${TREE}"
make -C "${BUILDROOT_DIR}" O="${TREE}" BR2_EXTERNAL="${EXT}" "$@"
