#!/usr/bin/env bash
set -e -u

export FAIL_EXPERIMENT=idle-test
fail-client -q -f bochs.input
