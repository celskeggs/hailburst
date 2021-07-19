package tlplot

import (
	"gioui.org/app"
	"gioui.org/io/key"
	"gioui.org/io/system"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/unit"
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"gonum.org/v1/plot/vg/vggio"
	"log"
	"os"
	"path"
)

type PlotWidget struct {
	Plot      *plot.Plot
	DPI       int
	ExportDir string
	AdjWidth  vg.Length
	AdjHeight vg.Length
}

func (p *PlotWidget) Layout(gtx layout.Context) layout.Dimensions {
	size := gtx.Constraints.Max
	wAdjusted := vg.Points(float64(size.X) * vg.Inch.Points() / float64(p.DPI))
	hAdjusted := vg.Points(float64(size.Y) * vg.Inch.Points() / float64(p.DPI))
	p.AdjWidth = wAdjusted
	p.AdjHeight = hAdjusted
	cnv := vggio.New(gtx, wAdjusted, hAdjusted, vggio.UseDPI(p.DPI))
	p.Plot.Draw(draw.New(cnv))
	return layout.Dimensions{Size: size}
}

func (p *PlotWidget) Export() {
	if p.ExportDir != "" {
		filepath := path.Join(p.ExportDir, "timeline.png")
		err := SavePlot(p.Plot, p.AdjWidth, p.AdjHeight, filepath, "png")
		if err != nil {
			log.Printf("While trying to export plot: %v", err)
		} else {
			log.Printf("Exported plot to %s", filepath)
		}
	}
}

func DisplayPlot(p *plot.Plot) error {
	return DisplayPlotExportable(p, "")
}

func DisplayPlotExportable(p *plot.Plot, exportDir string) error {
	plotWidget := &PlotWidget{
		Plot:      p,
		DPI:       128,
		ExportDir: exportDir,
	}

	go func() {
		win := app.NewWindow(
			app.Title("Plot Interface"),
			app.Size(
				unit.Px(1024),
				unit.Px(768),
			),
		)
		defer win.Close()

		for e := range win.Events() {
			switch e := e.(type) {
			case system.FrameEvent:
				ops := new(op.Ops)
				gtx := layout.NewContext(ops, e)
				layout.UniformInset(unit.Dp(30)).Layout(gtx, plotWidget.Layout)
				e.Frame(ops)

			case key.Event:
				switch e.Name {
				case "Q", key.NameEscape:
					win.Close()
				case "E":
					if e.State == key.Press {
						plotWidget.Export()
					}
				}

			case system.DestroyEvent:
				os.Exit(0)
			}
		}
	}()

	app.Main()
	return nil
}
