package main

import (
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"log"
	"os"
)

func main() {
	if len(os.Args) != 2 {
		log.Fatalf("Usage: %s <trial-dir>")
	}
	p, err := GeneratePlot(os.Args[1])
	if err != nil {
		log.Fatal(err)
	}
	if err := tlplot.DisplayPlot(p); err != nil {
		log.Fatal(err)
	}
}
