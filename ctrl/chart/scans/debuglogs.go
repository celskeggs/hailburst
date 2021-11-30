package scans

import (
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/ctrl/debuglog/readlog"
	"github.com/celskeggs/hailburst/sim/model"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"image/color"
	"log"
	"math"
	"os"
)

type DebugLogScan struct {
	Messages []model.VirtualTime
}

func (i *DebugLogScan) Label() string {
	return "Debug Log Events"
}

func (i *DebugLogScan) LastTime() float64 {
	latest := 0.0
	for _, msg := range i.Messages {
		latest = math.Max(latest, msg.Since(model.TimeZero).Seconds())
	}
	return latest
}

func (i *DebugLogScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var events []tlplot.Marker

	msgGlyph := draw.GlyphStyle{
		Color:  color.RGBA{0, 255, 255, 255},
		Radius: 5,
		Shape:  draw.PlusGlyph{},
	}

	for _, event := range i.Messages {
		events = append(events, tlplot.Marker{
			Time:  event.Since(model.TimeZero).Seconds(),
			Glyph: msgGlyph,
		})
	}

	return tlplot.NewTimelinePlot(nil, events, location, vg.Points(20), lastTime)
}

func ScanDebugLog(path string, elfPaths []string) (*DebugLogScan, error) {
	log.Printf("start scan")
	defer log.Printf("end scan")
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	records := make(chan readlog.Record)
	var parseErr error
	go func() {
		defer close(records)
		parseErr = readlog.Parse(elfPaths, f, records, false)
	}()

	var scan DebugLogScan
	for record := range records {
		scan.Messages = append(scan.Messages, record.Timestamp)
	}
	if parseErr != nil {
		return nil, parseErr
	}
	return &scan, nil
}
