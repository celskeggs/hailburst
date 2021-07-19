package main

import (
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/plotter"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"image/color"
	"log"
	"math/rand"
)

func main() {
	n := 10
	uniform := make(plotter.Values, n)
	normal := make(plotter.Values, n)
	expon := make(plotter.Values, n)
	for i := 0; i < n; i++ {
		uniform[i] = rand.Float64()
		normal[i] = rand.NormFloat64()
		expon[i] = rand.ExpFloat64()
	}

	p := plot.New()
	p.Title.Text = "Timeline Example"
	p.X.Label.Text = "Time Increases"

	act := []Activity{
		{
			Start: 5.0,
			End:   30.0,
			Color: color.RGBA{64, 192, 64, 255},
			Label: "Initializing",
		},
		{
			Start: 31.0,
			End:   40.0,
			Color: color.RGBA{128, 128, 255, 255},
			Label: "Running",
		},
		{
			Start: 40.0,
			End:   43.0,
			Color: color.RGBA{192, 192, 64, 255},
			Label: "Tearing Down",
		},
	}

	mark := []Marker{
		{
			Time: 34.0,
			Glyph: draw.GlyphStyle{
				Color:  color.Black,
				Radius: vg.Points(5),
				Shape:  draw.PyramidGlyph{},
			},
		},
		{
			Time: 40.0,
			Glyph: draw.GlyphStyle{
				Color:  color.Black,
				Radius: vg.Points(5),
				Shape:  draw.PyramidGlyph{},
			},
		},
	}

	tp := NewTimelinePlot(act, mark, 0, vg.Points(20))
	p.Add(tp)

	// Set the Y axis of the plot to nominal with
	// the given names for y=0, y=1 and y=2.
	p.NominalY("Single Plot")

	if err := DisplayPlot(p); err != nil {
		log.Fatal(err)
	}
}
