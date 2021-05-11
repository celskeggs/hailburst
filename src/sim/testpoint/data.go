package testpoint

import (
	"sim/component"
	"sim/model"
)

type DataSink struct {
	component.NullEventSource
	Collected []byte
}

var _ model.DataSinkBytes = &DataSink{}

func MakeDataSink(ctx model.SimContext) *DataSink {
	return &DataSink{}
}

func (ds *DataSink) TryWrite(from []byte) int {
	ds.Collected = append(ds.Collected, from...)
	return len(from)
}

func (ds *DataSink) Take() []byte {
	out := ds.Collected
	ds.Collected = nil
	return out
}

type DataSource struct {
	*component.EventDispatcher
	Ready []byte
}

var _ model.DataSourceBytes = &DataSource{}

func MakeDataSource(ctx model.SimContext, ready []byte) *DataSource {
	return &DataSource{
		EventDispatcher: component.MakeEventDispatcher(ctx),
		Ready:           ready,
	}
}

func (ds *DataSource) TryRead(into []byte) int {
	count := copy(into, ds.Ready)
	if count > 0 {
		ds.Ready = ds.Ready[count:]
	}
	return count
}

func (ds *DataSource) Refill(data []byte) {
	if !ds.IsConsumed() {
		panic("cannot refill non-empty testpoint")
	}
	ds.Ready = data
	ds.EventDispatcher.DispatchLater()
}

func (ds *DataSource) IsConsumed() bool {
	return len(ds.Ready) == 0
}
