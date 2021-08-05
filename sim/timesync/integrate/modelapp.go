package integrate

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/exchange"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/packetlink"
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

func MakePacketApp(main func(model.SimContext, fwmodel.PacketSource, fwmodel.PacketSink), injectErrors bool) timesync.ProtocolImpl {
	sim := component.MakeSimControllerSeeded(1)

	// input: bytes -> line characters
	inputSource, inputSink := component.DataBufferBytes(sim, 1024)
	if injectErrors {
		inputSink = InjectErrors(sim, inputSink, 4096)
	}
	// inputCharsSink = charlink.TeeDataSinksFW(sim, testpoint.MakeLoggerFW(sim, "[APP->BENCH]"), inputCharsSink)

	// output: line characters -> bytes
	outputSource, outputSink := component.DataBufferBytes(sim, 1024)
	if injectErrors {
		outputSink = InjectErrors(sim, outputSink, 4096)
	}
	// outputCharsSink = charlink.TeeDataSinksFW(sim, testpoint.MakeLoggerFW(sim, "[BENCH->APP]"), outputCharsSink)

	// packet exchange
	packetRecv := packetlink.MakePacketNode(sim)
	packetTxmit := packetlink.MakePacketNode(sim)
	exchange.FakeWire(sim, outputSink, inputSource, packetRecv.Sink(), packetTxmit.Source(), "App")
	main(sim, packetRecv.Source(), packetTxmit.Sink())

	return MakeModelApp(sim, outputSource, inputSink)
}
