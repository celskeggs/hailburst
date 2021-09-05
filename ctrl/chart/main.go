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
)

func ScanAll(dir string) (out []scans.ScannedLine, err error) {
	records, err := scans.ScanRawReqs(path.Join(dir, "reqs-raw.log"))
	if err != nil {
		return nil, errors.Wrap(err, "while scanning requirements")
	}
	injections, err := scans.ScanInjections(path.Join(dir, "injections.csv"))
	if err != nil {
		return nil, errors.Wrap(err, "while scanning injections")
	}
	ioLog, err := scans.ScanIOLog(path.Join(dir, "timesync.sock.log"))
	if err != nil {
		return nil, errors.Wrap(err, "while scanning timesync log")
	}
	// This rendering didn't actually turn out to be useful...
	//ioRecs, err := scans.ScanIORecord(path.Join(dir, "io-dump.csv"))
	//if err != nil {
	//	return nil, errors.Wrap(err, "while scanning io dump records")
	//}
	for _, record := range records {
		out = append(out, record)
	}
	//for _, ioRec := range ioRecs {
	//	out = append(out, ioRec)
	//}
	out = append(out,
		injections,
		ioLog,
	)
	return out, nil
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
