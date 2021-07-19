package main

import (
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/timesync"
	"time"
)

type IOMonitor struct {
	Underlying timesync.ProtocolImpl
	InitDelay  time.Duration
	Delay      time.Duration
	LastIO     model.VirtualTime
	EndFn      func(lastTxmit model.VirtualTime)
}

func (i *IOMonitor) Sync(pendingReadBytes int, now model.VirtualTime, writeData []byte) (expireAt model.VirtualTime, readData []byte) {
	if len(writeData) > 0 {
		i.LastIO = now
	}
	expireAt, readData = i.Underlying.Sync(pendingReadBytes, now, writeData)
	if i.EndFn != nil {
		// calculate the expiration point, when no data has been written for a certain amount of time
		ioExpire := i.LastIO.Add(i.Delay)
		if ioExpire.Since(model.TimeZero) < i.InitDelay {
			ioExpire = model.TimeZero.Add(i.InitDelay)
		}
		// if we've passed that point, and no data has been written, report it.
		if now.AtOrAfter(ioExpire) {
			// report expiration
			i.EndFn(i.LastIO)
			// stop tracking now
			i.EndFn = nil
		}
		// if we wouldn't get interrupted by the expiration point normally, adjust the next timer so that we will
		if !expireAt.TimeExists() || expireAt.After(ioExpire) {
			expireAt = ioExpire
		}
	}
	return expireAt, readData
}

var _ timesync.ProtocolImpl = &IOMonitor{}

func MakeMonitor(impl timesync.ProtocolImpl, initDelay time.Duration, delay time.Duration, end func(lastTxmit model.VirtualTime)) timesync.ProtocolImpl {
	return &IOMonitor{
		Underlying: impl,
		InitDelay:  initDelay,
		Delay:      delay,
		LastIO:     model.TimeZero,
		EndFn:      end,
	}
}
