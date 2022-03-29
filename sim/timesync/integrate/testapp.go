package integrate

import (
	"bytes"
	"fmt"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/timesync"
	"log"
	"time"
)

type TestApp struct {
	collectedData []byte
	lastWrite     model.VirtualTime
}

func (t *TestApp) Sync(pendingBytes int, nowRaw int64, writeData []byte) (expireAtRaw int64, readData []byte) {
	now := model.VirtualTime(nowRaw)
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
	var expireAt model.VirtualTime
	if now.AtOrAfter(t.lastWrite.Add(time.Second)) {
		expireAt = model.TimeNever
	} else {
		expireAt = t.lastWrite.Add(time.Second)
	}
	log.Printf("%v: next timer set for: %v", now, expireAt)
	return int64(expireAt), readData
}

func MakeTestApp() timesync.ProtocolImpl {
	return &TestApp{
		lastWrite: 0,
	}
}
