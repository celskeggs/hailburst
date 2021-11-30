package scans

import (
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/ctrl/debuglog/readlog"
	"github.com/celskeggs/hailburst/sim/model"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"log"
	"math"
	"os"
)

type DebugLogScan struct {
	Level    readlog.LogLevel
	Messages []model.VirtualTime
}

func (i *DebugLogScan) Label() string {
	return i.Level.String() + " Device Logs"
}

func (i *DebugLogScan) LastTime() float64 {
	latest := 0.0
	for _, event := range i.Messages {
		latest = math.Max(latest, event.Since(model.TimeZero).Seconds())
	}
	return latest
}

func (i *DebugLogScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var events []tlplot.Marker

	levelGlyph := draw.GlyphStyle{
		Color:  readlog.LogColorRGB(i.Level),
		Radius: 5,
		Shape:  draw.PlusGlyph{},
	}

	for _, event := range i.Messages {
		events = append(events, tlplot.Marker{
			Time:  event.Since(model.TimeZero).Seconds(),
			Glyph: levelGlyph,
		})
	}

	return tlplot.NewTimelinePlot(nil, events, location, vg.Points(20), lastTime)
}

func ScanDebugLog(path string, elfPaths []string) ([]*DebugLogScan, error) {
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

	scans := map[readlog.LogLevel]*DebugLogScan{}
	levels := readlog.LogLevels()
	for _, level := range levels {
		scans[level] = &DebugLogScan{
			Level:    level,
		}
	}
	for record := range records {
		scan := scans[record.Metadata.LogLevel]
		if scan == nil {
			log.Panicf("invalid log level: %v", record.Metadata.LogLevel)
		}
		scan.Messages = append(scan.Messages, record.Timestamp)
	}
	if parseErr != nil {
		return nil, parseErr
	}
	scansOut := make([]*DebugLogScan, len(levels))
	for i, level := range levels {
		scan := scans[level]
		if len(scan.Messages) > 0 {
			scansOut[len(scansOut) - i - 1] = scan
		}
	}
	return scansOut, nil
}
