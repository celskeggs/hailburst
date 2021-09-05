package scans

import (
	"encoding/csv"
	"encoding/hex"
	"errors"
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
	"strings"
)

type IORec struct {
	Timestamp model.VirtualTime
	Body      []byte
}

type IORecords struct {
	Name    string
	Records []IORec
}

var _ ScannedLine = &IORecords{}

func (i *IORecords) Label() string {
	return i.Name
}

func (i *IORecords) LastTime() float64 {
	latest := 0.0
	for _, rec := range i.Records {
		latest = math.Max(latest, rec.Timestamp.Since(model.TimeZero).Seconds())
	}
	return latest
}

func displayBytes(bytes []byte) string {
	strs := make([]string, len(bytes)+1)
	strs[0] = fmt.Sprintf("(%d)", len(bytes))
	for i, b := range bytes {
		strs[i+1] = hex.EncodeToString([]byte{b})
	}
	return strings.Join(strs, " ")
}

func (i *IORecords) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var events []tlplot.Marker

	ioGlyph := draw.GlyphStyle{
		Color:  color.RGBA{0, 0, 0, 255},
		Radius: 5,
		Shape:  draw.CrossGlyph{},
	}

	for _, event := range i.Records {
		events = append(events, tlplot.Marker{
			Time:  event.Timestamp.Since(model.TimeZero).Seconds(),
			Glyph: ioGlyph,
			Label: displayBytes(event.Body),
		})
	}

	return tlplot.NewTimelinePlot(nil, events, location, vg.Points(20), lastTime)
}

func ScanIORecord(path string) ([]*IORecords, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	cr := csv.NewReader(f)
	cr.FieldsPerRecord = 3
	firstRow, err := cr.Read()
	if err != nil {
		if err == io.EOF {
			err = io.ErrUnexpectedEOF
		}
		return nil, err
	}
	if len(firstRow) != 3 || firstRow[0] != "Nanoseconds" || firstRow[1] != "Direction" || firstRow[2] != "Hex Bytes" {
		return nil, fmt.Errorf("invalid first row: %v", firstRow)
	}

	scans := map[string][]IORec{}
	for {
		row, err := cr.Read()
		if err == io.EOF {
			break
		} else if err != nil {
			return nil, err
		}
		// decode timestamp
		eventNs, err := strconv.ParseUint(row[0], 10, 64)
		if err != nil {
			return nil, err
		}
		eventTime, ok := model.FromNanoseconds(eventNs)
		if !ok {
			return nil, fmt.Errorf("invalid timestamp: %v", eventNs)
		}
		// decode direction
		direction := row[1]
		if direction == "" {
			return nil, errors.New("invalid empty string for direction")
		}
		// decode data
		hexBytes, err := hex.DecodeString(row[2])
		if err != nil {
			return nil, err
		}
		// insert into scan
		scans[direction] = append(scans[direction], IORec{
			Timestamp: eventTime,
			Body:      hexBytes,
		})
	}
	var out []*IORecords
	for name, rec := range scans {
		out = append(out, &IORecords{
			Name:    name,
			Records: rec,
		})
	}
	sort.Slice(out, func(i, j int) bool {
		return out[i].Name < out[j].Name
	})
	return out, nil
}
