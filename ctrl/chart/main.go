package main

import (
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"log"
	"os"
	"path"
)

func main() {
	if len(os.Args) != 2 {
		log.Fatalf("Usage: %s <trial-dir>", path.Base(os.Args[0]))
	}
	trialDir := os.Args[1]
	p, err := GeneratePlot(trialDir)
	if err != nil {
		log.Fatal(err)
	}
	if err := tlplot.DisplayPlotExportable(p, trialDir); err != nil {
		log.Fatal(err)
	}
}
