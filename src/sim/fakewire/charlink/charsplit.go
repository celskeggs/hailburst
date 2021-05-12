package charlink

import (
	"sim/component"
	"sim/fakewire/fwmodel"
	"sim/model"
)

type teeSinkHelper struct {
	sink fwmodel.DataSinkFWChar
	pending []fwmodel.FWChar
}

type teeDataSinks struct {
	*component.EventDispatcher
	sinks []*teeSinkHelper
}

func TeeDataSinksFW(ctx model.SimContext, sinks ...fwmodel.DataSinkFWChar) fwmodel.DataSinkFWChar {
	tds := &teeDataSinks{
		EventDispatcher: component.MakeEventDispatcher(ctx, "sim.fakewire.charlink.TeeDataSinksFW"),
		sinks: make([]*teeSinkHelper, len(sinks)),
	}
	for i, sink := range sinks {
		tds.sinks[i] = &teeSinkHelper{
			sink:    sink,
			pending: nil,
		}
		i := i
		// fmt.Printf("subscribing to sink %d\n", i)
		sink.Subscribe(func() {
			// fmt.Printf("received event on subscription for sink %d\n", i)
			tds.onEvent(i)
		})
	}
	return tds
}

func (t *teeDataSinks) areAllReady() bool {
	for _, ts := range t.sinks {
		if len(ts.pending) > 0 {
			return false
		}
	}
	return true
}

func (t *teeDataSinks) TryWrite(from []fwmodel.FWChar) int {
	if !t.areAllReady() {
		return 0
	}
	// fmt.Printf("tee writing %d bytes to %d outputs\n", len(from), len(t.sinks))
	for _, ts := range t.sinks {
		if len(ts.pending) > 0 {
			panic("not consistent with areAllReady check")
		}
		count := ts.sink.TryWrite(from)
		if count < len(from) {
			ts.pending = append([]fwmodel.FWChar{}, from[count:]...)
		}
		// fmt.Printf("tee output %d: wrote %d, pending %d\n", i, count, len(ts.pending))
	}
	return len(from)
}

func (t *teeDataSinks) onEvent(i int) {
	ts := t.sinks[i]
	if len(ts.pending) > 0 {
		count := ts.sink.TryWrite(ts.pending)
		// fmt.Printf("triggered further write on sink %d; sent %d bytes out of %d pending\n", i, count, len(ts.pending))
		ts.pending = ts.pending[count:]
		if len(ts.pending) == 0 && t.areAllReady() {
			t.DispatchLater()
		}
	}
}
