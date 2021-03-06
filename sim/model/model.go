package model

import "math/rand"

type SimContext interface {
	Now() VirtualTime
	Rand() *rand.Rand
	SetTimer(expireAt VirtualTime, name string, callback func()) (cancel func())
	Later(name string, callback func()) (cancel func())
}

type EventSource interface {
	Subscribe(callback func()) (cancel func())
}

type DataSourceBytes interface {
	EventSource
	// TryRead attempts to read a series of bytes from a source.
	// If TryRead reads less than requested, the caller should assume it will not succeed in reading more until an
	// event is sent to the source's subscribers -- and even then, it's not guaranteed.
	TryRead(into []byte) int
}

type DataSinkBytes interface {
	EventSource
	// TryWrite attempts to write a series of bytes to a sink.
	// If TryWrite writes less than requested, the caller should assume it will not succeed in writing more until an
	// event is sent to the source's subscribers -- and even then, it's not guaranteed.
	TryWrite(from []byte) int
}
