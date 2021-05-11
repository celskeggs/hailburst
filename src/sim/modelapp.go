package main

import (
	"sim/component"
	"sim/model"
	"sim/timesync"
)

type ModelApp struct {
	controller *component.SimController
	source     model.DataSourceBytes
	sink       model.DataSinkBytes
}

func (t *ModelApp) Sync(pendingBytes int, now model.VirtualTime, writeData []byte) (expireAt model.VirtualTime, readData []byte) {
	if len(writeData) > 0 {
		// write the received data into the simulation
		actual := t.sink.TryWrite(writeData)
		if actual < len(writeData) {
			panic("UNIMPLEMENTED: back pressure on writes!")
		}
	}
	expireAt = t.controller.Advance(now)
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
