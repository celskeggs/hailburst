#!/bin/bash
export GOPATH="$(readlink -f "$(dirname "$0")")"
export GO111MODULE=off
go run ctrl "$@"
