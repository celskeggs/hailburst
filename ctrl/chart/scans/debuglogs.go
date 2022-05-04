package scans

import (
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/ctrl/debuglog/readlog"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/spacecraft"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"log"
	"math"
	"os"
	"time"
)

type DebugLogCorruption struct {
	Start model.VirtualTime
	End   model.VirtualTime
}

type DebugLogScan struct {
	Level       readlog.LogLevel
	StableID    string
	Messages    []model.VirtualTime
	Corruptions []DebugLogCorruption
}

func (i *DebugLogScan) Label() string {
	if i.StableID != "" {
		return i.StableID
	} else {
		return "DEVICE " + i.Level.String()
	}
}

func (i *DebugLogScan) LastTime() float64 {
	latest := 0.0
	for _, event := range i.Messages {
		latest = math.Max(latest, event.Since(model.TimeZero).Seconds())
	}
	for _, corruption := range i.Corruptions {
		if corruption.End.TimeExists() {
			latest = math.Max(latest, corruption.End.Since(model.TimeZero).Seconds())
		} else {
			latest = math.Max(latest, corruption.Start.Since(model.TimeZero).Seconds())
		}
	}
	return latest
}

func (i *DebugLogScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var activities []tlplot.Activity
	var events []tlplot.Marker

	glyph := draw.GlyphStyle{
		Color:  readlog.LogColorRGB(i.Level),
		Radius: 5,
	}

	if i.StableID == "" {
		glyph.Shape = draw.TriangleGlyph{}
	} else {
		glyph.Shape = draw.PyramidGlyph{}
	}

	for _, event := range i.Messages {
		events = append(events, tlplot.Marker{
			Time:  event.Since(model.TimeZero).Seconds(),
			Glyph: glyph,
		})
	}

	for _, corruption := range i.Corruptions {
		var end float64 = lastTime
		if corruption.End.TimeExists() {
			end = corruption.End.Since(model.TimeZero).Seconds()
		}
		activities = append(activities, tlplot.Activity{
			Start: corruption.Start.Since(model.TimeZero).Seconds(),
			End:   end,
			Color: glyph.Color,
			Label: "corrupt",
		})
	}

	return tlplot.NewTimelinePlot(activities, events, location, vg.Points(20), lastTime)
}

func ScanDebugLog(path string, elfPaths []string) ([]*DebugLogScan, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	records := make(chan readlog.Record)
	var parseErr error
	go func() {
		defer close(records)
		parseErr = readlog.Parse(elfPaths, f, records, false, true)
	}()

	levelScans := map[readlog.LogLevel]*DebugLogScan{}
	stableScans := map[string]*DebugLogScan{}
	levels := readlog.LogLevels()
	for _, level := range levels {
		levelScans[level] = &DebugLogScan{
			Level: level,
		}
	}
	isInvalid := false
	prevRegion := spacecraft.MissionStartTime
	for record := range records {
		if record.Timestamp.TimeExists() && record.Timestamp.AtOrAfter(prevRegion) && record.Timestamp.AtOrBefore(prevRegion.Add(time.Second * 50)) {
			if isInvalid {
				levelScans[readlog.LogCritical].Corruptions = append(
					levelScans[readlog.LogCritical].Corruptions,
					DebugLogCorruption{
						Start: prevRegion,
						End:   record.Timestamp,
					},
				)
				isInvalid = false
			}
			prevRegion = record.Timestamp
		} else {
			isInvalid = true
			continue
		}
		if record.Metadata.StableID != "" {
			scan := stableScans[record.Metadata.StableID]
			if scan == nil {
				scan = &DebugLogScan{
					Level:    record.Metadata.LogLevel,
					StableID: record.Metadata.StableID,
				}
				stableScans[record.Metadata.StableID] = scan
			}
			scan.Messages = append(scan.Messages, record.Timestamp)
		} else {
			scan := levelScans[record.Metadata.LogLevel]
			if scan == nil {
				log.Panicf("invalid log level: %v", record.Metadata.LogLevel)
			}
			scan.Messages = append(scan.Messages, record.Timestamp)
		}
	}
	if parseErr != nil {
		return nil, parseErr
	}
	if isInvalid {
		levelScans[readlog.LogCritical].Corruptions = append(
			levelScans[readlog.LogCritical].Corruptions,
			DebugLogCorruption{
				Start: prevRegion,
				End:   model.TimeNever,
			},
		)
		isInvalid = false
	}
	scansOrdered := make([]*DebugLogScan, len(levels))
	for i, level := range levels {
		scan := levelScans[level]
		if len(scan.Messages) > 0 {
			scansOrdered[len(scansOrdered)-i-1] = scan
		}
	}
	var scansOut []*DebugLogScan
	for _, scan := range scansOrdered {
		if scan != nil {
			scansOut = append(scansOut, scan)
		}
	}
	for _, scan := range stableScans {
		scansOut = append(scansOut, scan)
	}
	return scansOut, nil
}
