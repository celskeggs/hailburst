#!/bin/bash
set -e -u
# use this command: set {int}0x800000 = N
# -s kernel 
gdb-multiarch -ex 'target remote :1234' -ex 'maintenance packet Qqemu.PhyMemMode:1' -ex 'set pagination off' -ex 'source ctrl.py' -ex 'log_inject ../injections.csv' "$@"
