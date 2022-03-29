package main

import (
	"github.com/celskeggs/hailburst/sim/timesync"
	"time"
)

type IOMonitor struct {
	Underlying  timesync.ProtocolImpl
	InitDelayNs int64
	DelayNs     int64
	LastIONs    int64
	EndFn       func(lastTxmit int64)
}

func (i *IOMonitor) Sync(pendingReadBytes int, now int64, writeData []byte) (expireAt int64, readData []byte) {
	if len(writeData) > 0 {
		i.LastIONs = now
	}
	expireAt, readData = i.Underlying.Sync(pendingReadBytes, now, writeData)
	if i.EndFn != nil {
		// calculate the expiration point, when no data has been written for a certain amount of time
		ioExpire := i.LastIONs + i.DelayNs
		if ioExpire < i.InitDelayNs {
			ioExpire = i.InitDelayNs
		}
		// if we've passed that point, and no data has been written, report it.
		if now >= ioExpire {
			// report expiration
			i.EndFn(i.LastIONs)
			// stop tracking now
			i.EndFn = nil
		}
		// if we wouldn't get interrupted by the expiration point normally, adjust the next timer so that we will
		if expireAt < 0 || expireAt > ioExpire {
			expireAt = ioExpire
		}
	}
	return expireAt, readData
}

var _ timesync.ProtocolImpl = &IOMonitor{}

func MakeMonitor(impl timesync.ProtocolImpl, initDelay time.Duration, delay time.Duration, end func(lastTxmitNs int64)) timesync.ProtocolImpl {
	return &IOMonitor{
		Underlying:  impl,
		InitDelayNs: initDelay.Nanoseconds(),
		DelayNs:     delay.Nanoseconds(),
		LastIONs:    0,
		EndFn:       end,
	}
}
