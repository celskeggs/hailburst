#!/bin/bash
go run ./ctrl/chart --fswbin "$(dirname "$0")/fsw/build-freertos" "$@"
