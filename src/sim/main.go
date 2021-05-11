package main

import (
	"fmt"
	"log"
	"sim/component"
	"sim/fakewire"
	"sim/fakewire/fwmodel"
	"sim/testpoint"
	"sim/timesync"
)

type Logger struct {
	*component.EventDispatcher
	chs string
}

func (l *Logger) TryWrite(from []fwmodel.FWChar) int {
	for _, ch := range from {
		if ch == fwmodel.ParityFail || ch.IsCtrl() {
			l.chs = fmt.Sprintf("%s\" %v \"", l.chs, ch)
		} else {
			u8, _ := ch.Data()
			if u8 == '\n' {
				fmt.Printf("Received FakeWire line: \"%s\"\n", l.chs)
				l.chs = ""
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
	}
	return len(from)
}

func MakeTestModelApp() timesync.ProtocolImpl {
	sim := component.MakeSimController()
	inputBufSource, inputBufSink := component.DataBufferBytes(sim, 1024)
	logger := &Logger{
		EventDispatcher: component.MakeEventDispatcher(sim),
	}
	fakewire.DecodeFakeWire(sim, logger, inputBufSource)
	outputBufSource, outputBufSink := component.DataBufferBytes(sim, 1024)
	dataProvider := testpoint.MakeDataSourceFW(sim, []fwmodel.FWChar{
		'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd', fwmodel.CtrlEOP, fwmodel.CtrlESC, fwmodel.CtrlFCT,
	})
	fakewire.EncodeFakeWire(sim, outputBufSink, dataProvider)
	return MakeModelApp(sim, outputBufSource, inputBufSink)
}

func main() {
	app := MakeTestModelApp()
	err := timesync.Simple("./timesync-test.sock", app)
	if err != nil {
		log.Fatalf("Encountered top-level error: %v", err)
	}
}
