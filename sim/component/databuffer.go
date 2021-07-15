package component

import (
	"sim/model"
)

type dataBuffer struct {
	ctx model.SimContext

	buffered []byte
	capacity int

	writable *EventDispatcher
	readable *EventDispatcher
}

type dataBufferSource dataBuffer
type dataBufferSink dataBuffer

func (dbs *dataBufferSource) TryRead(into []byte) int {
	count := copy(into, dbs.buffered)
	if count > 0 {
		dbs.buffered = dbs.buffered[count:]
		// fmt.Printf("queuing event to write buffer\n")
		dbs.writable.DispatchLater()
	}
	return count
}

func (dbs *dataBufferSource) Subscribe(callback func()) (cancel func()) {
	return dbs.readable.Subscribe(callback)
}

func (dbs *dataBufferSink) TryWrite(from []byte) int {
	toWrite := len(from)
	if toWrite > dbs.capacity-len(dbs.buffered) {
		toWrite = dbs.capacity - len(dbs.buffered)
	}
	if toWrite > 0 {
		dbs.buffered = append(dbs.buffered, from[:toWrite]...)
		// fmt.Printf("queuing event to read buffer\n")
		dbs.readable.DispatchLater()
	}
	// fmt.Printf("Written: %d bytes out of %d attempted: %v\n", toWrite, len(from), from[:toWrite])
	return toWrite
}

func (dbs *dataBufferSink) Subscribe(callback func()) (cancel func()) {
	return dbs.writable.Subscribe(callback)
}

func DataBufferBytes(ctx model.SimContext, capacity int) (model.DataSourceBytes, model.DataSinkBytes) {
	db := &dataBuffer{
		ctx:      ctx,
		buffered: make([]byte, 0, capacity),
		capacity: capacity,
		writable: MakeEventDispatcher(ctx, "sim.component.DataBufferBytes/Writable"),
		readable: MakeEventDispatcher(ctx, "sim.component.DataBufferBytes/Readable"),
	}
	return (*dataBufferSource)(db), (*dataBufferSink)(db)
}
