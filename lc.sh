#!/bin/bash
set -e -u

wc -cl $(find *.sh ext/ ctrl/ sim/ fsw/ -type f      \
              -not -name '*.o'                       \
              -not -name 'fakewire_*_test'           \
              -not -name 'kernel'                    \
              -not -name '.sconsign.dblite'          \
              -not -path '*build*'                   \
              -not -name '*-bin'                     \
              -not -name 'rtos_*.c'                  \
              -not -name '*.pyc'                     \
        ) | sort -n
