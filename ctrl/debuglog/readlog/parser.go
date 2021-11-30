package readlog

import (
	"bytes"
	"encoding/hex"
	"errors"
	"github.com/celskeggs/hailburst/sim/model"
	"io"
	"log"
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
	follow bool
}

func (w *WaitingReader) Read() (b byte, err error) {
	for len(w.cur) == 0 {
		n, err := w.stream.Read(w.buffer)
		if err == nil || (err == io.EOF && n > 0) {
			w.cur = w.buffer[:n]
			continue
		} else if err == io.EOF && w.follow {
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

func ParseStream(inputStream io.Reader, outputCh chan<- []byte, junkCh chan<- []byte, follow bool) error {
	wr := WaitingReader{
		buffer: make([]byte, 4096),
		stream: inputStream,
		follow: follow,
	}
	for {
		// read bytes until we get to the start of the next segment
		b, err := wr.Read()
		if err == io.EOF {
			return nil
		} else if err != nil {
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
			// reconstruct the original sequence of bytes for maximally accurate junk reporting
			junk := append([]byte{DebugSegmentStart}, body...)
			junk = append(junk, DebugSegmentEnd)
			junkCh <- junk
		} else {
			outputCh <- decoded
		}
	}
}

func Parse(elfPaths []string, inputStream io.Reader, recordsOut chan<- Record, follow bool) error {
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
		streamErr = ParseStream(inputStream, recordCh, junkCh, follow)
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
	if streamErr == nil && follow {
		streamErr = errors.New("unexpected halt in ParseStream")
	}
	return streamErr
}
