package component

import (
	"github.com/celskeggs/hailburst/sim/model"
)

func DataPumpBytes(ctx model.SimContext, source model.DataSourceBytes, sink model.DataSinkBytes) {
	// capacity only really impacts efficiency, so not configurable
	capacity := 1024
	buffer := make([]byte, capacity)
	dataLevel := 0

	pump := func() {
		if dataLevel > 0 {
			sunk := sink.TryWrite(buffer[:dataLevel])
			if sunk < 0 || sunk > dataLevel {
				panic("invalid write amount")
			}
			copy(buffer, buffer[sunk:dataLevel])
			dataLevel -= sunk
		} else if dataLevel < capacity {
			sourced := source.TryRead(buffer[dataLevel:])
			if sourced < 0 || sourced > capacity-dataLevel {
				panic("invalid read amount")
			}
			dataLevel += sourced
		}
	}
	source.Subscribe(pump)
	sink.Subscribe(pump)
	// to get things kicked off
	ctx.Later("sim.component.DataPumpBytes/begin", pump)
}

func DataPumpDirect(ctx model.SimContext, source model.DataSourceBytes, sink func([]byte)) {
	// capacity only really impacts efficiency, so not configurable
	buffer := make([]byte, 1024)

	pump := func() {
		count := source.TryRead(buffer)
		if count > 0 {
			sink(buffer[:count])
		}
	}
	source.Subscribe(pump)
	// to get things kicked off
	ctx.Later("sim.component.DataPumpDirect/begin", pump)
}