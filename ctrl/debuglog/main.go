package main

import (
	"github.com/celskeggs/hailburst/sim/model"
	"log"
	"os"
	"strings"

	"github.com/celskeggs/hailburst/ctrl/debuglog/readlog"
)

func main() {
	var guestLog string
	var srcDir string
	var binaries []string
	earliest := model.TimeNever
	latest := model.TimeNever
	var usage bool
	var follow bool
	var full bool
	var fixTime bool = true
	minLogLevel := readlog.LogTrace
	for i := 1; i < len(os.Args); i++ {
		if os.Args[i] == "--loglevel" && i+1 < len(os.Args) {
			level, err := readlog.ParseStringLogLevel(os.Args[i+1])
			if err != nil {
				log.Printf("Error: %v", err)
				os.Exit(1)
			}
			minLogLevel = level
			i += 1
		} else if os.Args[i] == "--srcdir" && i+1 < len(os.Args) {
			srcDir = os.Args[i+1]
			i += 1
		} else if os.Args[i] == "--follow" {
			follow = true
		} else if os.Args[i] == "--full" {
			full = true
		} else if (os.Args[i] == "--from" || os.Args[i] == "--until") && i+1 < len(os.Args) {
			time, err := model.ParseTime(os.Args[i+1])
			if err != nil {
				log.Printf("Error: %v", err)
				os.Exit(1)
			}
			if os.Args[i] == "--from" {
				earliest = time
			} else {
				latest = time
			}
			i += 1
		} else if os.Args[i] == "--raw-time" {
			fixTime = false
		} else if strings.HasPrefix(os.Args[i], "-") {
			usage = true
			break
		} else if guestLog == "" {
			guestLog = os.Args[i]
		} else {
			binaries = append(binaries, os.Args[i])
		}
	}
	if guestLog == "" || usage {
		log.Printf("Usage: %s [--loglevel <level>] [--full] [--follow] [--from <time>] [--until <time>] [--srcdir <dir>] [--raw-time] <guest.log> [<source binary> [<source binary> [...]]]",
			os.Args[0])
		os.Exit(1)
	}
	if len(binaries) == 0 {
		binaries = []string{"fsw/build-vivid/kernel", "fsw/build-vivid/bootrom-elf"}
	}
	input, err := os.Open(guestLog)
	if err != nil {
		log.Fatal(err)
	}
	recordCh := make(chan readlog.Record)
	var parseError error
	go func() {
		defer close(recordCh)
		parseError = readlog.Parse(binaries, input, recordCh, follow, fixTime)
	}()
	renderCh := make(chan readlog.Record)
	readlog.Filter(recordCh, renderCh, minLogLevel, earliest, latest)
	renderError := readlog.Renderer(renderCh, os.Stdout, srcDir, full)
	if parseError != nil || renderError != nil || follow {
		log.Fatalf("Errors: parse: %v, render: %v", parseError, renderError)
	}
}
