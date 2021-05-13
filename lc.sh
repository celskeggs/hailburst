#!/bin/bash
set -e -u

wc -cl $(find bare-arm/ *.sh ext src -type f -not -name '*.o' -not -name 'fakewire_*_test' -not -name 'kernel')
