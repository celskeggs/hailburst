package testpoint

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/model"
)

type DataSinkFW struct {
	component.NullEventSource
	Collected []fwmodel.FWChar
}

var _ fwmodel.DataSinkFWChar = &DataSinkFW{}

func MakeDataSinkFW(ctx model.SimContext) *DataSinkFW {
	return &DataSinkFW{}
}

func (ds *DataSinkFW) TryWrite(from []fwmodel.FWChar) int {
	ds.Collected = append(ds.Collected, from...)
	return len(from)
}

func (ds *DataSinkFW) Take() []fwmodel.FWChar {
	out := ds.Collected
	ds.Collected = nil
	return out
}

type DataSourceFW struct {
	*component.EventDispatcher
	Ready []fwmodel.FWChar
}

var _ fwmodel.DataSourceFWChar = &DataSourceFW{}

func MakeDataSourceFW(ctx model.SimContext, ready []fwmodel.FWChar) *DataSourceFW {
	return &DataSourceFW{
		EventDispatcher: component.MakeEventDispatcher(ctx, "sim.testpoint.DataSourceFW"),
		Ready:           ready,
	}
}

func (ds *DataSourceFW) TryRead(into []fwmodel.FWChar) int {
	count := copy(into, ds.Ready)
	if count > 0 {
		ds.Ready = ds.Ready[count:]
	}
	return count
}

func (ds *DataSourceFW) Refill(data []fwmodel.FWChar) {
	if !ds.IsConsumed() {
		panic("cannot refill non-empty testpoint")
	}
	ds.Ready = data
	ds.EventDispatcher.DispatchLater()
}

func (ds *DataSourceFW) IsConsumed() bool {
	return len(ds.Ready) == 0
}
