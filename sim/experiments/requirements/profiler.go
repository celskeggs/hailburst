package main

import (
	"encoding/csv"
	"fmt"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/timesync"
	"os"
	"time"
)

type ProfilerImpl struct {
	wrapped  timesync.ProtocolImpl
	writer   *csv.Writer
	timebase time.Time
}

func MakeProfiler(profile string, wrapped timesync.ProtocolImpl) (*ProfilerImpl, error) {
	pc, err := os.Create(profile)
	if err != nil {
		return nil, err
	}
	pi := &ProfilerImpl{
		wrapped:  wrapped,
		writer:   csv.NewWriter(pc),
		timebase: time.Now(),
	}
	err = pi.writer.Write([]string{"Phase", "Virtual Time", "Clock Time"})
	if err != nil {
		return nil, err
	}
	return pi, nil
}

func (p *ProfilerImpl) Sync(pendingReadBytes int, now model.VirtualTime, writeData []byte) (expireAt model.VirtualTime, readData []byte) {
	if err := p.writer.Write([]string{"Q2S", fmt.Sprint(now.Nanoseconds()), fmt.Sprint(time.Now().Sub(p.timebase).Nanoseconds())}); err != nil {
		panic("error during sync: " + err.Error())
	}
	expireAt, readData = p.wrapped.Sync(pendingReadBytes, now, writeData)
	if err := p.writer.Write([]string{"S2Q", fmt.Sprint(now.Nanoseconds()), fmt.Sprint(time.Now().Sub(p.timebase).Nanoseconds())}); err != nil {
		panic("error during sync: " + err.Error())
	}
	p.writer.Flush()
	if err := p.writer.Error(); err != nil {
		panic("error during sync: " + err.Error())
	}
	return expireAt, readData
}

var _ timesync.ProtocolImpl = &ProfilerImpl{}
