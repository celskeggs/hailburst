package main

import (
	"log"
	"sim/component"
	"sim/fakewire/charlink"
	"sim/fakewire/fwmodel"
	"sim/testpoint"
	"sim/timesync"
)

func MakeTestModelApp() timesync.ProtocolImpl {
	sim := component.MakeSimController()
	inputBufSource, inputBufSink := component.DataBufferBytes(sim, 1024)
	logger := testpoint.MakeLoggerFW(sim, "Timesync")
	charlink.DecodeFakeWire(sim, logger, inputBufSource)
	outputBufSource, outputBufSink := component.DataBufferBytes(sim, 1024)
	dataProvider := testpoint.MakeDataSourceFW(sim, []fwmodel.FWChar{
		'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd', fwmodel.CtrlEOP, fwmodel.CtrlESC, fwmodel.CtrlFCT,
	})
	charlink.EncodeFakeWire(sim, outputBufSink, dataProvider)
	return MakeModelApp(sim, outputBufSource, inputBufSink)
}

func main() {
	app := MakeTestModelApp()
	err := timesync.Simple("./timesync-test.sock", app)
	if err != nil {
		log.Fatalf("Encountered top-level error: %v", err)
	}
}
