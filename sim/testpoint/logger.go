package testpoint

import (
	"fmt"
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/codec"
	"github.com/celskeggs/hailburst/sim/model"
	"log"
	"strings"
	"time"
)

type Logger struct {
	ctx model.SimContext
	*component.EventDispatcher
	chs              string
	name             string
	flushTimerCancel func()
	flushDelay       time.Duration
}

func (l *Logger) TryWrite(from []byte) int {
	if l.flushTimerCancel != nil {
		l.flushTimerCancel()
		l.flushTimerCancel = nil
	}
	for _, u8 := range from {
		if codec.IsCtrl(u8) {
			l.chs += codec.ControlChar(u8).String() + " "
		} else {
			l.chs += fmt.Sprintf("%02x ", u8)
		}
		if len(l.chs) >= 100 {
			l.flush()
		}
	}
	if len(l.chs) > 0 {
		l.flushTimerCancel = l.ctx.SetTimer(l.ctx.Now().Add(l.flushDelay), "sim.testpoint.Logger/Flush", func() {
			l.flush()
		})
	}
	return len(from)
}

func (l *Logger) flush() {
	log.Printf("%v [%s] COMM: %s\n", l.ctx.Now(), l.name, strings.TrimRight(l.chs, " "))
	l.chs = ""
}

func MakeLogger(ctx model.SimContext, name string, flushDelay time.Duration) model.DataSinkBytes {
	return &Logger{
		ctx:             ctx,
		EventDispatcher: component.MakeEventDispatcher(ctx, "sim.testpoint.Logger"),
		name:            name,
		flushDelay:      flushDelay,
	}
}
