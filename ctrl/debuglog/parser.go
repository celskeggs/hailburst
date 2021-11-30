package main

import (
	"bytes"
	"encoding/hex"
	"errors"
	"github.com/celskeggs/hailburst/sim/model"
	"io"
	"log"
	"os"
	"time"
)

const (
	DebugEscapeByte   = 0xA7
	DebugSegmentStart = 0xA9
	DebugSegmentEnd   = 0xAF
)

type WaitingReader struct {
	buffer []byte
	cur    []byte
	stream io.Reader
}

func (w *WaitingReader) Read() (b byte, err error) {
	for len(w.cur) == 0 {
		n, err := w.stream.Read(w.buffer)
		if err == nil || (err == io.EOF && n > 0) {
			w.cur = w.buffer[:n]
			continue
		} else if err == io.EOF {
			// handle EOF case by waiting
			// TODO: better way to do this?
			time.Sleep(time.Millisecond * 100)
		} else {
			w.cur = w.buffer[:n]
			return 0, err
		}
	}
	b = w.cur[0]
	w.cur = w.cur[1:]
	return b, nil
}

func decodeBody(in []byte) (output []byte, junk bool) {
	fragments := bytes.Split(in, []byte{DebugEscapeByte})
	for i := 1; i < len(fragments); i++ {
		frag := fragments[i]
		// invalid to have a DebugEscapeByte directly before another special character
		if len(frag) == 0 {
			return nil, true
		}
		// flip top bit to decode escaped byte
		frag[0] ^= 0x80
		// make sure it decodes to something that would have made sense to encode in the first place
		if frag[0] != DebugEscapeByte && frag[0] != DebugSegmentStart && frag[0] != DebugSegmentEnd {
			return nil, true
		}
		// continue on to the next fragment
	}
	// rejoin fragments with their fixed first bytes and eliminated DebugEscapeBytes
	return bytes.Join(fragments, nil), false
}

func ParseStream(inputStream io.Reader, outputCh chan<- []byte, junkCh chan<- []byte) error {
	wr := WaitingReader{
		buffer: make([]byte, 4096),
		stream: inputStream,
	}
	for {
		// read bytes until we get to the start of the next segment
		b, err := wr.Read()
		if err != nil {
			return err
		}
		var junk []byte
		for b != DebugSegmentStart {
			junk = append(junk, b)
			if b, err = wr.Read(); err != nil {
				return err
			}
		}
		if len(junk) > 0 {
			log.Printf("JUNK 1")
			junkCh <- junk
		}
		// read bytes until we find the end of the segment
		if b, err = wr.Read(); err != nil {
			return err
		}
		var body []byte
		for b != DebugSegmentEnd {
			if b == DebugSegmentStart {
				// turns out that our original start was actually a false start to a segment... treat it as junk!
				log.Printf("JUNK 2")
				junkCh <- append([]byte{DebugSegmentStart}, body...)
				body = nil
			} else {
				body = append(body, b)
			}
			if b, err = wr.Read(); err != nil {
				return err
			}
		}
		// hit start, body, and then end
		decoded, isJunk := decodeBody(body)
		if isJunk || len(decoded) < 12 {
			log.Printf("JUNK 3")
			// reconstruct the original sequence of bytes for maximally accurate junk reporting
			junk := append([]byte{DebugSegmentStart}, body...)
			junk = append(junk, DebugSegmentEnd)
			junkCh <- junk
		} else {
			outputCh <- decoded
		}
	}
}

func Parse(elfPaths []string, inputStream io.Reader, recordsOut chan<- Record) error {
	dd, err := LoadDebugData(elfPaths)
	if err != nil {
		return err
	}
	recordCh := make(chan []byte)
	junkCh := make(chan []byte)
	stopCh := make(chan struct{})
	var streamErr error
	go func() {
		defer close(stopCh)
		streamErr = ParseStream(inputStream, recordCh, junkCh)
	}()
	var done bool
	for !done {
		select {
		case record := <-recordCh:
			recordDec, err := dd.Decode(record)
			if err != nil {
				log.Printf("Error during decoding: %v", err)
			}
			recordsOut <- recordDec
		case junk := <-junkCh:
			recordsOut <- Record{
				ArgumentData: []interface{}{hex.EncodeToString(junk)},
				Metadata:     dd.JunkData,
				Timestamp:    model.TimeNever,
			}
		case <-stopCh:
			done = true
		}
	}
	if streamErr == nil {
		streamErr = errors.New("unexpected halt in ParseStream")
	}
	return streamErr
}

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
	recordCh := make(chan Record)
	var parseError error
	go func() {
		defer close(recordCh)
		parseError = Parse(binaries, input, recordCh)
	}()
	renderError := Renderer(recordCh, os.Stdout, false)
	log.Fatalf("Errors: parse: %v, render: %v", parseError, renderError)
}
