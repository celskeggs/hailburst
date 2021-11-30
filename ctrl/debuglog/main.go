package main

import (
	"log"
	"os"

	"github.com/celskeggs/hailburst/ctrl/debuglog/readlog"
)

func main() {
	if len(os.Args) < 2 || (len(os.Args) > 2 && (len(os.Args) < 4 || os.Args[2] != "--")) {
		log.Printf("Usage: %s <guest.log> [-- <source binary> [<source binary> [...]]]", os.Args[0])
		os.Exit(1)
	}
	var binaries []string
	if len(os.Args) > 2 {
		binaries = os.Args[3:]
	} else {
		binaries = []string{"fsw/build-freertos/kernel", "fsw/build-freertos/bootrom-elf"}
	}
	input, err := os.Open(os.Args[1])
	if err != nil {
		log.Fatal(err)
	}
	recordCh := make(chan readlog.Record)
	var parseError error
	go func() {
		defer close(recordCh)
		parseError = readlog.Parse(binaries, input, recordCh, true)
	}()
	renderError := readlog.Renderer(recordCh, os.Stdout, false)
	log.Fatalf("Errors: parse: %v, render: %v", parseError, renderError)
}
