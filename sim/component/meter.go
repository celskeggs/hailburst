package component

import (
	"github.com/celskeggs/hailburst/sim/model"
	"time"
)

type meterBytes struct {
	*EventDispatcher
	sink         model.DataSinkBytes
	bytesAllowed int
	interval     time.Duration

	currentAllowance int
	cancelTimer      func()
}

func (b *meterBytes) TryWrite(from []byte) int {
	if len(from) > b.currentAllowance {
		from = from[:b.currentAllowance]
	}
	count := b.sink.TryWrite(from)
	if count > 0 {
		if b.cancelTimer == nil {
			b.cancelTimer = b.ctx.SetTimer(b.ctx.Now().Add(b.interval), "sim.component.MeteredSink/Refill", b.refill)
		}
		b.currentAllowance -= count
	}
	return count
}

func (b *meterBytes) refill() {
	b.cancelTimer = nil
	// if we were out of transferable bytes, let any prospective writers know that we can take more data
	if b.currentAllowance == 0 {
		b.DispatchLater()
	}
	b.currentAllowance = b.bytesAllowed
}

func (b *meterBytes) underlyingReady() {
	// if the underlying sink is ready for more data, and it was potentially the reason we didn't write more, let our
	// prospective writers know
	if b.currentAllowance > 0 {
		b.DispatchLater()
	}
}

func MakeMeteredSink(ctx model.SimContext, sink model.DataSinkBytes, bytesAllowed int, perInterval time.Duration) model.DataSinkBytes {
	meter := &meterBytes{
		EventDispatcher:  MakeEventDispatcher(ctx, "sim.component.MeteredSink"),
		sink:             sink,
		bytesAllowed:     bytesAllowed,
		interval:         perInterval,
		currentAllowance: bytesAllowed,
		cancelTimer:      nil,
	}
	sink.Subscribe(meter.underlyingReady)
	return meter
}
