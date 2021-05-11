#!/bin/bash
export GOPATH="$(readlink -f "$(dirname "$0")")"
go run ctrl "$@"
