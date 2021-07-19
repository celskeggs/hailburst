package charlink

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/model"
)

type fwBuffer struct {
	ctx model.SimContext

	buffered []fwmodel.FWChar
	capacity int

	writable *component.EventDispatcher
	readable *component.EventDispatcher
}

type fwBufferSource fwBuffer
type fwBufferSink fwBuffer

func (dbs *fwBufferSource) TryRead(into []fwmodel.FWChar) int {
	count := copy(into, dbs.buffered)
	if count > 0 {
		dbs.buffered = dbs.buffered[count:]
		dbs.writable.DispatchLater()
	}
	return count
}

func (dbs *fwBufferSource) Subscribe(callback func()) (cancel func()) {
	return dbs.readable.Subscribe(callback)
}

func (dbs *fwBufferSink) TryWrite(from []fwmodel.FWChar) int {
	toWrite := len(from)
	if toWrite > dbs.capacity-len(dbs.buffered) {
		toWrite = dbs.capacity - len(dbs.buffered)
	}
	if toWrite > 0 {
		dbs.buffered = append(dbs.buffered, from[:toWrite]...)
		dbs.readable.DispatchLater()
	}
	return toWrite
}

func (dbs *fwBufferSink) Subscribe(callback func()) (cancel func()) {
	return dbs.writable.Subscribe(callback)
}

func DataBufferFWChar(ctx model.SimContext, capacity int) (fwmodel.DataSourceFWChar, fwmodel.DataSinkFWChar) {
	db := &fwBuffer{
		ctx:      ctx,
		buffered: make([]fwmodel.FWChar, 0, capacity),
		capacity: capacity,
		writable: component.MakeEventDispatcher(ctx, "sim.fakewire.charlink.DataBufferFWChar/Writable"),
		readable: component.MakeEventDispatcher(ctx, "sim.fakewire.charlink.DataBufferFWChar/Readable"),
	}
	return (*fwBufferSource)(db), (*fwBufferSink)(db)
}
