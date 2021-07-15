#!/bin/bash
rm -f ./batch-proc ./experiment-proc
go build -o ./experiment-proc ./sim/experiments/requirements
go build -o ./batch-proc ./ctrl/batch
go run ./ctrl/bulk "$@"
