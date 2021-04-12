#!/bin/bash
set -e -u
# use this command: set {int}0x800000 = N
gdb-multiarch -s kernel -ex 'target remote :1234' -ex 'source test_harness.py'
