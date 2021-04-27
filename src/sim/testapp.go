package main

import (
	"bytes"
	"fmt"
	"log"
	"time"
)

type TestApp struct {
	collectedData []byte
	lastWrite VirtualTime
}

func (t *TestApp) Sync(pendingBytes int, now VirtualTime, writeData []byte) (expireAt VirtualTime, readData []byte) {
	if len(writeData) > 0 {
		t.collectedData = append(t.collectedData, writeData...)
		subslices := bytes.Split(t.collectedData, []byte("\n"))
		for _, ss := range subslices[:len(subslices)-1] {
			log.Printf("%v: received completed line from timesync leader: %q", now, string(ss))
		}
		t.collectedData = subslices[len(subslices)-1]
	}
	if now.AtOrAfter(t.lastWrite.Add(time.Second)) && pendingBytes == 0 {
		t.lastWrite = now
		readData = []byte(fmt.Sprintf("data written at time %v from timesync follower\n", now))
		log.Printf("%v: wrote data to timesync leader: %q", now, string(readData))
	}
	if now.AtOrAfter(t.lastWrite.Add(time.Second)) {
		expireAt = NeverTimeout
	} else {
		expireAt = t.lastWrite.Add(time.Second)
	}
	log.Printf("%v: next timer set for: %v", now, expireAt)
	return expireAt, readData
}

func MakeTestApp() TimesyncProtocolImpl {
	return &TestApp{
		lastWrite: 0,
	}
}