package verifier

import (
	"fmt"
	"log"
	"reflect"
	"sim/model"
	"sim/telecomm/transport"
	"sort"
)

type Event interface {
	Timestamp() model.VirtualTime
}

type CommandUplinkEvent struct {
	SendTimestamp    model.VirtualTime
	ReceiveTimestamp model.VirtualTime
	Command          transport.Command
}

func (e CommandUplinkEvent) Timestamp() model.VirtualTime {
	return e.ReceiveTimestamp
}

type TelemetryDownlinkEvent struct {
	RemoteTimestamp model.VirtualTime
	LocalTimestamp  model.VirtualTime
	Telemetry       transport.Telemetry
}

func (e TelemetryDownlinkEvent) Timestamp() model.VirtualTime {
	return e.LocalTimestamp
}

func (e TelemetryDownlinkEvent) String() string {
	return fmt.Sprintf("TlmDownEvt{Remote=%v, Local=%v, Telemetry=%v %v}",
		e.RemoteTimestamp, e.LocalTimestamp, reflect.TypeOf(e.Telemetry), e.Telemetry)
}

type MagnetometerPowerEvent struct {
	Powered         bool
	ActionTimestamp model.VirtualTime
}

func (e MagnetometerPowerEvent) Timestamp() model.VirtualTime {
	return e.ActionTimestamp
}

type MagnetometerMeasureEvent struct {
	X, Y, Z       int16
	MeasTimestamp model.VirtualTime
}

func (e MagnetometerMeasureEvent) Timestamp() model.VirtualTime {
	return e.MeasTimestamp
}

type tracker struct {
	sim                   model.SimContext
	Events                []Event
	TelemetryByteErrors   int
	TelemetryPacketErrors int
	IsMagnetometerPowered bool
}

func (a *tracker) insert(event Event) {
	if event.Timestamp() != a.sim.Now() {
		panic("invalid timestamp for new event")
	}
	if len(a.Events) > 0 {
		if a.Events[len(a.Events)-1].Timestamp().After(event.Timestamp()) {
			panic("events not in order")
		}
	}
	a.Events = append(a.Events, event)
}

// search returns all events in the specified range of times [startTime, endTime) that match the predicate.
func (a *tracker) search(startTime, endTime model.VirtualTime, predicate func(Event) bool) []Event {
	startIdx := sort.Search(len(a.Events), func(i int) bool {
		return a.Events[i].Timestamp().AtOrAfter(startTime)
	})
	endIdx := startIdx + sort.Search(len(a.Events)-startIdx, func(i int) bool {
		return a.Events[i+startIdx].Timestamp().AtOrAfter(endTime)
	})
	var all []Event
	for i := startIdx; i < endIdx; i++ {
		if predicate(a.Events[i]) {
			all = append(all, a.Events[i])
		}
	}
	return all
}

// searchLast returns the most recent element that matches the predicate.
func (a *tracker) searchLast(predicate func(Event) bool) Event {
	for i := len(a.Events) - 1; i >= 0; i-- {
		if predicate(a.Events[i]) {
			return a.Events[i]
		}
	}
	return nil
}

func (a *tracker) OnCommandUplink(command transport.Command, sendTimestamp model.VirtualTime) {
	a.insert(CommandUplinkEvent{
		SendTimestamp:    sendTimestamp,
		ReceiveTimestamp: a.sim.Now(),
		Command:          command,
	})
}

func (a *tracker) OnTelemetryErrors(byteErrors int, packetErrors int) {
	log.Printf("Detected telemetry errors: byte=%d, packet=%d", byteErrors, packetErrors)
	a.TelemetryByteErrors += byteErrors
	a.TelemetryPacketErrors += packetErrors
}

func (a *tracker) OnTelemetryDownlink(telemetry transport.Telemetry, remoteTimestamp model.VirtualTime) {
	a.insert(TelemetryDownlinkEvent{
		RemoteTimestamp: remoteTimestamp,
		LocalTimestamp:  a.sim.Now(),
		Telemetry:       telemetry,
	})
}

func (a *tracker) OnSetMagnetometerPower(powered bool) {
	if a.IsMagnetometerPowered == powered {
		panic("should not transition to power state that was already set")
	}
	a.IsMagnetometerPowered = powered
	a.insert(MagnetometerPowerEvent{
		Powered:         powered,
		ActionTimestamp: a.sim.Now(),
	})
}

func (a *tracker) OnMeasureMagnetometer(x, y, z int16) {
	a.insert(MagnetometerMeasureEvent{
		X:             x,
		Y:             y,
		Z:             z,
		MeasTimestamp: a.sim.Now(),
	})
}
