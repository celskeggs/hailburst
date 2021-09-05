package scans

import (
	"encoding/csv"
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/sim/model"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"image/color"
	"io"
	"math"
	"os"
	"sort"
	"strconv"
)

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

	return tlplot.NewTimelinePlot(activities, markers, location, vg.Points(15), lastTime)
}

func ScanRawReqs(path string) ([]*ReqScan, error) {
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
			break
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
	var results []*ReqScan
	for _, s := range reqmap {
		results = append(results, s)
	}
	sort.Slice(results, func(i, j int) bool {
		return results[i].RequirementName > results[j].RequirementName
	})
	return results, nil
}
