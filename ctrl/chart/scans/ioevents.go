package scans

import (
	"encoding/csv"
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/spacecraft"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"image/color"
	"io"
	"math"
	"os"
	"strconv"
	"time"
)

type IOScan struct {
	Reads  []model.VirtualTime
	Writes []model.VirtualTime
}

func (i *IOScan) Label() string {
	return "I/O Events"
}

func (i *IOScan) LastTime() float64 {
	latest := 0.0
	for _, read := range i.Reads {
		latest = math.Max(latest, read.Since(model.TimeZero).Seconds())
	}
	for _, write := range i.Writes {
		latest = math.Max(latest, write.Since(model.TimeZero).Seconds())
	}
	return latest
}

func (i *IOScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var events []tlplot.Marker

	readGlyph := draw.GlyphStyle{
		Color:  color.RGBA{0, 128, 255, 255},
		Radius: 5,
		Shape:  draw.SquareGlyph{},
	}
	writeGlyph := draw.GlyphStyle{
		Color:  color.RGBA{255, 128, 0, 255},
		Radius: 5,
		Shape:  draw.RingGlyph{},
	}

	for _, event := range i.Reads {
		events = append(events, tlplot.Marker{
			Time:  event.Since(model.TimeZero).Seconds(),
			Glyph: readGlyph,
		})
	}
	for _, event := range i.Writes {
		events = append(events, tlplot.Marker{
			Time:  event.Since(model.TimeZero).Seconds(),
			Glyph: writeGlyph,
		})
	}

	return tlplot.NewTimelinePlot(nil, events, location, vg.Points(20), lastTime)
}

func ScanIOLog(path string) (*IOScan, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	cr := csv.NewReader(f)
	cr.FieldsPerRecord = 5

	var scan IOScan
	for {
		row, err := cr.Read()
		if err == io.EOF {
			return &scan, nil
		} else if err != nil {
			return nil, err
		}
		// first, we only want to record these as events if they involve I/O
		transferBytes, err := strconv.ParseUint(row[3], 10, 64)
		if err != nil {
			return nil, err
		}
		if transferBytes == 0 {
			continue
		}
		// determine if read or write
		var out *[]model.VirtualTime
		if row[0] == "start" {
			// transmitted from flight software to simulation
			out = &scan.Writes
		} else if row[0] == "end" {
			// transmitted from simulation to flight software
			out = &scan.Reads
		} else {
			return nil, fmt.Errorf("invalid mode: %q", row[0])
		}
		eventNs, err := strconv.ParseUint(row[1], 10, 64)
		if err != nil {
			return nil, err
		}
		eventTime := spacecraft.MissionStartTime.Add(time.Duration(eventNs) * time.Nanosecond)
		*out = append(*out, eventTime)
	}
}
