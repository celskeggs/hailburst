package component

import "sim/model"

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
	toRead := len(into)
	if toRead > len(dbs.buffered) {
		toRead = len(dbs.buffered)
	}
	if toRead > 0 {
		copy(into, dbs.buffered[:toRead])
		dbs.writable.DispatchLater()
	}
	return toRead
}

func (dbs *dataBufferSource) Subscribe(callback func()) (cancel func()) {
	return dbs.readable.Subscribe(callback)
}

func (dbs *dataBufferSink) TryWrite(from []byte) int {
	toWrite := len(from)
	if toWrite+dbs.capacity > len(dbs.buffered) {
		toWrite = dbs.capacity - len(dbs.buffered)
	}
	if toWrite > 0 {
		dbs.buffered = append(dbs.buffered, from[:toWrite]...)
		dbs.readable.DispatchLater()
	}
	return toWrite
}

func (dbs *dataBufferSink) Subscribe(callback func()) (cancel func()) {
	return dbs.readable.Subscribe(callback)
}

func DataBufferBytes(ctx model.SimContext, capacity int) (model.DataSourceBytes, model.DataSinkBytes) {
	db := &dataBuffer{
		ctx:      ctx,
		buffered: make([]byte, 0, capacity),
		capacity: capacity,
		writable: MakeEventDispatcher(ctx),
		readable: MakeEventDispatcher(ctx),
	}
	return (*dataBufferSource)(db), (*dataBufferSink)(db)
}
