package main

import (
	"encoding/csv"
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/sim/model"
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"image/color"
	"io"
	"math"
	"os"
	"path"
	"sort"
	"strconv"
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

type ReqScan struct {
	RequirementName string
	Failures        []model.VirtualTime
	Successes       []model.VirtualTime
	Intervals       []Interval
}

func (rs *ReqScan) Label() string {
	return rs.RequirementName
}

func (rs *ReqScan) LastTime() float64 {
	latest := 0.0
	for _, failure := range rs.Failures {
		latest = math.Max(latest, failure.Since(model.TimeZero).Seconds())
	}
	for _, success := range rs.Successes {
		latest = math.Max(latest, success.Since(model.TimeZero).Seconds())
	}
	for _, interval := range rs.Intervals {
		if interval.End.TimeExists() {
			latest = math.Max(latest, interval.End.Since(model.TimeZero).Seconds())
		} else {
			latest = math.Max(latest, interval.Start.Since(model.TimeZero).Seconds())
		}
	}
	return latest
}

func (rs *ReqScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var activities []tlplot.Activity
	var markers []tlplot.Marker

	failGlyph := draw.GlyphStyle{
		Color:  color.RGBA{255, 0, 0, 255},
		Radius: 5,
		Shape:  draw.TriangleGlyph{},
	}
	succeedGlyph := draw.GlyphStyle{
		Color:  color.RGBA{0, 255, 0, 255},
		Radius: 4,
		Shape:  draw.RingGlyph{},
	}

	for _, failure := range rs.Failures {
		markers = append(markers, tlplot.Marker{
			Time:  failure.Since(model.TimeZero).Seconds(),
			Glyph: failGlyph,
		})
	}
	for _, success := range rs.Successes {
		markers = append(markers, tlplot.Marker{
			Time:  success.Since(model.TimeZero).Seconds(),
			Glyph: succeedGlyph,
		})
	}

	for _, intervals := range rs.Intervals {
		start := intervals.Start.Since(model.TimeZero).Seconds()
		end := lastTime
		if intervals.End.TimeExists() {
			end = intervals.End.Since(model.TimeZero).Seconds()
		}
		activities = append(activities, tlplot.Activity{
			Start: start,
			End:   end,
			Color: color.RGBA{255, 255, 0, 255},
		})
	}

	return tlplot.NewTimelinePlot(activities, markers, location, vg.Points(20))
}

func ScanRawReqs(path string) (map[string]*ReqScan, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer func() {
		_ = f.Close()
	}()
	cr := csv.NewReader(f)
	cr.FieldsPerRecord = -1

	// first, determine the full list of requirements
	firstRow, err := cr.Read()
	if err != nil {
		if err == io.EOF {
			err = io.ErrUnexpectedEOF
		}
		return nil, err
	}
	if len(firstRow) < 2 || firstRow[0] != "REQUIREMENTS" {
		return nil, fmt.Errorf("invalid first row: %v", firstRow)
	}
	reqmap := map[string]*ReqScan{}
	for _, req := range firstRow[1:] {
		reqmap[req] = &ReqScan{
			RequirementName: req,
		}
	}

	// now parse in all the events
	for {
		row, err := cr.Read()
		if err == io.EOF {
			return reqmap, nil
		} else if err != nil {
			return nil, err
		}
		expectedNum := 3
		if len(row) > 0 && row[0] == "RETIRE" {
			expectedNum = 4
		}
		if len(row) != expectedNum {
			return nil, fmt.Errorf("wrong number of fields in row: %v", row)
		}

		requirement := row[len(row)-1]
		record := reqmap[requirement]
		if record == nil {
			return nil, fmt.Errorf("unexpected requirement name: %v", requirement)
		}

		timestamp, err := strconv.ParseUint(row[1], 10, 64)
		if err != nil {
			return nil, err
		}
		timestampV, ok := model.FromNanoseconds(timestamp)
		if !ok {
			return nil, fmt.Errorf("invalid timestamp: %v", timestamp)
		}

		switch row[0] {
		case "BEGIN":
			record.Intervals = append(record.Intervals, Interval{
				Start: timestampV,
				End:   model.TimeNever,
			})
		case "RETIRE":
			timestampEnd, err := strconv.ParseUint(row[2], 10, 64)
			if err != nil {
				return nil, err
			}
			timestampEndV, ok := model.FromNanoseconds(timestampEnd)
			if !ok {
				return nil, fmt.Errorf("invalid ending timestamp: %v", timestampEnd)
			}
			if timestampEndV < timestampV {
				return nil, fmt.Errorf("out of order timestamps in row: %v", row)
			}

			matched := false
			for i := len(record.Intervals) - 1; i >= 0; i-- {
				// find a matching record without the second timestamp filled in
				if !record.Intervals[i].End.TimeExists() && record.Intervals[i].Start == timestampV {
					record.Intervals[i].End = timestampEndV
					matched = true
					break
				}
			}
			if !matched {
				return nil, fmt.Errorf("could not find a matching timestamp for: %v", row)
			}
		case "SUCCEED":
			record.Successes = append(record.Successes, timestampV)
		case "FAIL":
			record.Failures = append(record.Failures, timestampV)
		default:
			return nil, fmt.Errorf("invalid event code in row: %v", row)
		}
	}
}

type InjectionScan struct {
	Injections []model.VirtualTime
}

func (i *InjectionScan) Label() string {
	return "Injections"
}

func (i *InjectionScan) LastTime() float64 {
	latest := 0.0
	for _, injection := range i.Injections {
		latest = math.Max(latest, injection.Since(model.TimeZero).Seconds())
	}
	return latest
}

func (i *InjectionScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var injections []tlplot.Marker

	injectGlyph := draw.GlyphStyle{
		Color:  color.RGBA{255, 0, 0, 255},
		Radius: 8,
		Shape:  draw.CrossGlyph{},
	}

	for _, injection := range i.Injections {
		injections = append(injections, tlplot.Marker{
			Time:  injection.Since(model.TimeZero).Seconds(),
			Glyph: injectGlyph,
		})
	}

	return tlplot.NewTimelinePlot(nil, injections, location, vg.Points(20))
}

func ScanInjections(path string) (*InjectionScan, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	cr := csv.NewReader(f)
	cr.FieldsPerRecord = 5

	firstRow, err := cr.Read()
	if err != nil {
		if err == io.EOF {
			err = io.ErrUnexpectedEOF
		}
		return nil, err
	}
	if firstRow[1] != "Injection Time" {
		return nil, fmt.Errorf("invalid first row: %v", firstRow)
	}

	var scan InjectionScan
	for {
		row, err := cr.Read()
		if err == io.EOF {
			return &scan, nil
		} else if err != nil {
			return nil, err
		}
		injectNs, err := strconv.ParseUint(row[1], 10, 64)
		if err != nil {
			return nil, err
		}
		injectV, ok := model.FromNanoseconds(injectNs)
		if !ok {
			return nil, fmt.Errorf("invalid timestamp: %v", injectNs)
		}
		scan.Injections = append(scan.Injections, injectV)
	}
}

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

	return tlplot.NewTimelinePlot(nil, events, location, vg.Points(20))
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
		eventTime, ok := model.FromNanoseconds(eventNs)
		if !ok {
			return nil, fmt.Errorf("invalid timestamp: %v", eventNs)
		}
		*out = append(*out, eventTime)
	}
}

func ScanAll(dir string) (out []ScannedLine, err error) {
	records, err := ScanRawReqs(path.Join(dir, "reqs-raw.log"))
	if err != nil {
		return nil, err
	}
	injections, err := ScanInjections(path.Join(dir, "injections.csv"))
	if err != nil {
		return nil, err
	}
	ioLog, err := ScanIOLog(path.Join(dir, "timesync.sock.log"))
	if err != nil {
		return nil, err
	}
	out = []ScannedLine{
		injections,
		ioLog,
	}
	for _, record := range records {
		out = append(out, record)
	}
	return out, nil
}

func GeneratePlot(dir string) (*plot.Plot, error) {
	p := plot.New()
	p.Title.Text = "Timeline: " + path.Base(dir)
	p.X.Label.Text = "Virtual Time"
	p.Y.Label.Text = "Requirement"

	scans, err := ScanAll(dir)
	if err != nil {
		return nil, err
	}

	sort.Slice(scans, func(i, j int) bool {
		return scans[i].Label() > scans[j].Label()
	})

	lastTime := 0.0
	for _, scan := range scans {
		lastTime = math.Max(lastTime, scan.LastTime())
	}
	var names []string
	for i, scan := range scans {
		p.Add(scan.BuildPlot(lastTime, float64(i)))
		names = append(names, scan.Label())
	}
	p.NominalY(names...)

	return p, nil
}
