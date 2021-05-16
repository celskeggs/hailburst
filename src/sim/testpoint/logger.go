package testpoint

import (
	"fmt"
	"sim/component"
	"sim/fakewire/fwmodel"
	"sim/model"
	"strings"
	"time"
)

type LoggerFW struct {
	ctx model.SimContext
	*component.EventDispatcher
	chs  string
	name string
	flushTimerCancel func()
}

func (l *LoggerFW) TryWrite(from []fwmodel.FWChar) int {
	if l.flushTimerCancel != nil {
		l.flushTimerCancel()
		l.flushTimerCancel = nil
	}
	for _, ch := range from {
		if ch == fwmodel.ParityFail || ch.IsCtrl() {
			l.chs = fmt.Sprintf("%s\" %v \"", l.chs, ch)
		} else {
			u8, _ := ch.Data()
			if u8 == '\n' {
				l.chs += "\\n"
			} else if u8 == '\\' {
				l.chs += "\\\\"
			} else if u8 == '"' {
				l.chs += "\\\""
			} else if u8 >= 32 && u8 <= 126 && u8 != '\\' && u8 != '"' {
				l.chs += string(rune(u8))
			} else {
				l.chs = fmt.Sprintf("%s\\x%02x", l.chs, u8)
			}
		}
		if ch == fwmodel.CtrlEOP {
			l.flush(" (pckt)")
		} else if len(l.chs) >= 100 {
			l.flush(" (wrap)")
		}
	}
	if len(l.chs) > 0 {
		l.flushTimerCancel = l.ctx.SetTimer(l.ctx.Now().Add(time.Millisecond * 500), "sim.testpoint.LoggerFW/Flush", func() {
			l.flush(" (time)")
		})
	}
	return len(from)
}

func (l *LoggerFW) flush(hint string) {
	fmt.Printf("%s line%s: %s\n", l.name, hint, strings.ReplaceAll(fmt.Sprintf("\"%s\"", l.chs), " \"\"", ""))
	l.chs = ""
}

func MakeLoggerFW(ctx model.SimContext, name string) fwmodel.DataSinkFWChar {
	return &LoggerFW{
		ctx: ctx,
		EventDispatcher: component.MakeEventDispatcher(ctx, "sim.testpoint.LoggerFW"),
		name: name,
	}
}