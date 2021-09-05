package scans

import (
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/sim/model"
)

type Interval struct {
	Start model.VirtualTime
	End   model.VirtualTime
}

type ScannedLine interface {
	Label() string
	LastTime() float64
	BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot
}
