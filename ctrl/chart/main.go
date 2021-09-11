package main

import (
	"github.com/celskeggs/hailburst/ctrl/chart/scans"
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/pkg/errors"
	"gonum.org/v1/plot"
	"log"
	"math"
	"os"
	"path"
	"strconv"
)

// This rendering didn't actually turn out to be useful...
const ShowIORecords = false

func ScanAll(dir string) (out []scans.ScannedLine, err error) {
	records, err := scans.ScanRawReqs(path.Join(dir, "reqs-raw.log"))
	if err != nil {
		return nil, errors.Wrap(err, "while scanning requirements")
	}
	for _, record := range records {
		out = append(out, record)
	}
	if ShowIORecords {
		ioRecs, err := scans.ScanIORecord(path.Join(dir, "io-dump.csv"))
		if err != nil {
			return nil, errors.Wrap(err, "while scanning io dump records")
		}
		for _, ioRec := range ioRecs {
			out = append(out, ioRec)
		}
	}
	schedule, err := scans.ScanScheduler(path.Join(dir, "guest.log"))
	if err != nil {
		return nil, errors.Wrap(err, "while scanning guest.log for schedule")
	}
	for _, priority := range schedule {
		out = append(out, priority)
	}
	injections, err := scans.ScanInjections(path.Join(dir, "injections.csv"))
	if err != nil {
		return nil, errors.Wrap(err, "while scanning injections")
	}
	ioLog, err := scans.ScanIOLog(path.Join(dir, "timesync.sock.log"))
	if err != nil {
		return nil, errors.Wrap(err, "while scanning timesync log")
	}
	out = append(out,
		injections,
		ioLog,
	)
	return out, nil
}

type PreciseTicks struct {}

func (p PreciseTicks) Ticks(min, max float64) []plot.Tick {
	ticks := plot.DefaultTicks{}.Ticks(min, max)
	for i := range ticks {
		ticks[i].Label = strconv.FormatFloat(ticks[i].Value, 'f', 6, 64)
	}
	return ticks
}

func GeneratePlot(dir string) (*plot.Plot, error) {
	p := plot.New()
	p.Title.Text = "Timeline: " + path.Base(dir)
	p.X.Label.Text = "Virtual Time"
	p.Y.Label.Text = "Requirement"

	scanResults, err := ScanAll(dir)
	if err != nil {
		return nil, err
	}

	lastTime := 0.0
	for _, scan := range scanResults {
		lastTime = math.Max(lastTime, scan.LastTime())
	}
	var names []string
	for i, scan := range scanResults {
		p.Add(scan.BuildPlot(lastTime, float64(i)))
		names = append(names, scan.Label())
	}
	p.NominalY(names...)
	p.X.Tick.Marker = PreciseTicks{}

	return p, nil
}

func main() {
	if len(os.Args) != 2 {
		log.Fatalf("Usage: %s <trial-dir>", path.Base(os.Args[0]))
	}
	trialDir := os.Args[1]

	p, err := GeneratePlot(trialDir)
	if err != nil {
		log.Fatal("error while generating plot: ", err)
	}

	if err := tlplot.DisplayPlotExportable(p, trialDir); err != nil {
		log.Fatal("error while displaying plot: ", err)
	}
}
