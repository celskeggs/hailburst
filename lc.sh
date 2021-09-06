#!/bin/bash
set -e -u

wc -cl $(find *.sh *.py ext/ ctrl/ sim/ fsw/ -type f -not -name '*.o' -not -name 'fakewire_*_test' -not -name 'kernel')
