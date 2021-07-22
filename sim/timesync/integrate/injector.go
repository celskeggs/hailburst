package integrate

import (
	"github.com/celskeggs/hailburst/sim/model"
	"math"
)

type InjectedSink struct {
	base            model.DataSinkBytes
	sim             model.SimContext
	pending         []byte
	nextInjectDelay int
	frequency       float64
}

var _ model.DataSinkBytes = &InjectedSink{}

func (i *InjectedSink) Subscribe(callback func()) (cancel func()) {
	return i.base.Subscribe(callback)
}

func (i *InjectedSink) pump() {
	if len(i.pending) > 0 {
		count := i.base.TryWrite(i.pending)
		i.pending = i.pending[count:]
	}
}

func (i *InjectedSink) randomizeDelay() {
	// inject an error about once every N bytes
	i.nextInjectDelay = int(math.Floor(i.sim.Rand().ExpFloat64() * i.frequency))
}

func (i *InjectedSink) rawInject(data []byte) {
	if len(i.pending) != 0 {
		panic("should not try to inject right now")
	}
	r := i.sim.Rand()

	for i.nextInjectDelay < len(data) {
		// transfer unchanged portion directly
		i.pending = append(i.pending, data[:i.nextInjectDelay]...)
		data = data[i.nextInjectDelay:]

		// decide which type of error to inject
		switch r.Intn(4) {
		case 0: // bit error
			i.pending = append(i.pending, data[0]^byte(1+r.Intn(255)))
			data = data[1:]
		case 1: // duplicated byte
			i.pending = append(i.pending, data[0])
		case 2: // inserted byte
			i.pending = append(i.pending, uint8(r.Uint32()))
		case 3: // dropped byte
			data = data[1:]
		}

		// determine next point to inject into
		i.randomizeDelay()
	}
	// transfer remaining portion unchanged
	i.pending = append(i.pending, data...)
	i.nextInjectDelay -= len(data)
}

func (i *InjectedSink) TryWrite(from []byte) int {
	i.pump()
	if len(i.pending) > 0 {
		return 0
	}
	i.rawInject(from)
	i.pump()
	return len(from)
}

func InjectErrors(sim model.SimContext, output model.DataSinkBytes, frequency float64) model.DataSinkBytes {
	is := &InjectedSink{
		base:      output,
		sim:       sim,
		frequency: frequency,
	}
	is.randomizeDelay()
	output.Subscribe(is.pump)
	return is
}
