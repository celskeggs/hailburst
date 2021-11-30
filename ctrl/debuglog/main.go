package main

import (
	"log"
	"os"
	"strings"

	"github.com/celskeggs/hailburst/ctrl/debuglog/readlog"
)

func main() {
	var guestLog string
	var binaries []string
	var usage bool
	var follow bool
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
		} else if os.Args[i] == "--follow" {
			follow = true
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
		log.Printf("Usage: %s [--loglevel <level>] <guest.log> [-- <source binary> [<source binary> [...]]]",
			os.Args[0])
		os.Exit(1)
	}
	if len(binaries) == 0 {
		binaries = []string{"fsw/build-freertos/kernel", "fsw/build-freertos/bootrom-elf"}
	}
	input, err := os.Open(guestLog)
	if err != nil {
		log.Fatal(err)
	}
	recordCh := make(chan readlog.Record)
	var parseError error
	go func() {
		defer close(recordCh)
		parseError = readlog.Parse(binaries, input, recordCh, follow)
	}()
	renderError := readlog.Renderer(recordCh, os.Stdout, minLogLevel, false)
	if parseError != nil || renderError != nil || follow {
		log.Fatalf("Errors: parse: %v, render: %v", parseError, renderError)
	}
}
