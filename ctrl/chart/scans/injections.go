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
	"strconv"
)

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

	return tlplot.NewTimelinePlot(nil, injections, location, vg.Points(20), lastTime)
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
