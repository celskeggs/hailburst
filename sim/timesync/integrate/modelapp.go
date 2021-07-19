package integrate

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/charlink"
	"github.com/celskeggs/hailburst/sim/fakewire/exchange"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/timesync"
)

type ModelApp struct {
	controller *component.SimController
	source     model.DataSourceBytes
	sink       model.DataSinkBytes
}

func (t *ModelApp) Sync(pendingBytes int, now model.VirtualTime, writeData []byte) (expireAt model.VirtualTime, readData []byte) {
	// fast-forward time right up until the receive point
	if t.controller.Now() < now {
		_ = t.controller.Advance(now - 1)
	}
	// then send the data
	if len(writeData) > 0 {
		// write the received data into the simulation
		actual := t.sink.TryWrite(writeData)
		if actual < len(writeData) {
			panic("UNIMPLEMENTED: back pressure on writes!")
		}
	}
	// then allow the data to be received in the proper nanosecond
	expireAt = t.controller.Advance(now)
	// then extract any reply data
	if pendingBytes == 0 {
		outputData := make([]byte, 1024)
		actual := t.source.TryRead(outputData)
		if actual > 0 {
			readData = outputData[:actual]
		}
	}
	if expireAt.TimeExists() && expireAt.AtOrBefore(now) {
		panic("timer too soon to be valid")
	}
	return expireAt, readData
}

func MakeModelApp(controller *component.SimController, source model.DataSourceBytes, sink model.DataSinkBytes) timesync.ProtocolImpl {
	if controller.Now() != model.VirtualTime(0) {
		panic("invalid starting time in MakeModelApp")
	}
	return &ModelApp{
		controller: controller,
		source:     source,
		sink:       sink,
	}
}

func MakePacketApp(main func(model.SimContext, fwmodel.PacketSource, fwmodel.PacketSink)) timesync.ProtocolImpl {
	sim := component.MakeSimControllerSeeded(1)

	// input: bytes -> line characters
	inputBytesSource, inputBytesSink := component.DataBufferBytes(sim, 1024)
	inputCharsSource, inputCharsSink := charlink.DataBufferFWChar(sim, 1024)
	// inputCharsSink = charlink.TeeDataSinksFW(sim, testpoint.MakeLoggerFW(sim, "[APP->BENCH]"), inputCharsSink)
	charlink.DecodeFakeWire(sim, inputCharsSink, inputBytesSource)

	// output: line characters -> bytes
	outputBytesSource, outputBytesSink := component.DataBufferBytes(sim, 1024)
	outputCharsSource, outputCharsSink := charlink.DataBufferFWChar(sim, 1024)
	charlink.EncodeFakeWire(sim, outputBytesSink, outputCharsSource)
	// outputCharsSink = charlink.TeeDataSinksFW(sim, testpoint.MakeLoggerFW(sim, "[BENCH->APP]"), outputCharsSink)

	// packet exchange
	psink, psource := exchange.FakeWireExchange(sim, outputCharsSink, inputCharsSource, true)
	main(sim, psource, psink)

	return MakeModelApp(sim, outputBytesSource, inputBytesSink)
}
