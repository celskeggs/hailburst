package fwmodel

import "sim/model"

type DataSourceFWChar interface {
	model.EventSource
	// TryRead attempts to read a series of fwchars from a source.
	// If TryRead reads less than requested, the caller should assume it will not succeed in reading more until an
	// event is sent to the source's subscribers -- and even then, it's not guaranteed.
	TryRead(into []FWChar) int
}

type DataSinkFWChar interface {
	model.EventSource
	// TryWrite attempts to write a series of fwchars to a sink.
	// If TryWrite writes less than requested, the caller should assume it will not succeed in writing more until an
	// event is sent to the source's subscribers -- and even then, it's not guaranteed.
	TryWrite(from []FWChar) int
}
