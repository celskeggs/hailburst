package tlplot

import (
	"gioui.org/app"
	"gioui.org/f32"
	"gioui.org/io/key"
	"gioui.org/io/pointer"
	"gioui.org/io/system"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"gonum.org/v1/plot/vg/vgimg"
	"image"
	"image/color"
	"image/png"
	"log"
	"math"
	"os"
	"path"
)

type PlotWidget struct {
	Plot      *plot.Plot
	DPI       int
	ExportDir string
	AdjWidth  vg.Length
	AdjHeight vg.Length

	SliderStart, SliderEnd       float64 // out of 1.0
	NewSliderStart, NewSliderEnd float64

	Busy  bool
	Ready chan image.Image
	Image image.Image
}

func (p *PlotWidget) GenImage(w, h vg.Length) image.Image {
	c := vgimg.NewWith(vgimg.UseWH(w, h), vgimg.UseDPI(p.DPI))
	p.Plot.Draw(draw.New(c))
	return c.Image()
}

func (p *PlotWidget) OnReady(ready image.Image) {
	if !p.Busy {
		panic("should be busy")
	}
	p.Image = ready
	p.Busy = false
}

func (p *PlotWidget) GetImage(size image.Point) image.Image {
	wAdjusted := vg.Points(float64(size.X) * vg.Inch.Points() / float64(p.DPI))
	hAdjusted := vg.Points(float64(size.Y) * vg.Inch.Points() / float64(p.DPI))
	if p.Image == nil {
		p.Image = p.GenImage(wAdjusted, hAdjusted)
		p.AdjWidth = wAdjusted
		p.AdjHeight = hAdjusted
	} else if p.AdjWidth != wAdjusted || p.AdjHeight != hAdjusted {
		if !p.Busy {
			p.Busy = true
			go func() {
				p.Ready <- p.GenImage(wAdjusted, hAdjusted)
			}()
			p.AdjWidth = wAdjusted
			p.AdjHeight = hAdjusted
		}
	}

	return p.Image
}

var layoutTag = new(struct{})

func (p *PlotWidget) Layout(gtx layout.Context) layout.Dimensions {
	defer op.Save(gtx.Ops).Load()

	sliderY := 20
	if sliderY > gtx.Constraints.Max.Y/4 {
		sliderY = gtx.Constraints.Max.Y / 4
	}

	base := op.Save(gtx.Ops)

	// input for slider
	for _, ev := range gtx.Queue.Events(layoutTag) {
		if x, ok := ev.(pointer.Event); ok {
			frac := math.Max(0, math.Min(1, float64(x.Position.X)/float64(gtx.Constraints.Max.X)))
			switch x.Type {
			case pointer.Press:
				if x.Buttons.Contain(pointer.ButtonSecondary) {
					p.NewSliderStart = -1
					p.NewSliderEnd = -1
					p.SliderStart = 0
					p.SliderEnd = 1
				} else {
					p.NewSliderStart = frac
					p.NewSliderEnd = -1
				}
			case pointer.Drag:
				if !x.Buttons.Contain(pointer.ButtonSecondary) {
					p.NewSliderEnd = frac
				}
			case pointer.Release:
				if !x.Buttons.Contain(pointer.ButtonSecondary) {
					if p.NewSliderEnd != -1 {
						p.SliderStart = p.NewSliderStart
						p.SliderEnd = p.NewSliderEnd
					}
					p.NewSliderStart = -1
					p.NewSliderEnd = -1
				}
			}
		}
	}

	pointer.Rect(image.Rectangle{
		Max: image.Point{
			X: gtx.Constraints.Max.X,
			Y: sliderY,
		},
	}).Add(gtx.Ops)
	pointer.InputOp{
		Tag:   layoutTag,
		Types: pointer.Press | pointer.Drag | pointer.Release,
	}.Add(gtx.Ops)

	// render slider background
	clip.Rect{
		Max: image.Point{
			X: gtx.Constraints.Max.X,
			Y: sliderY,
		},
	}.Add(gtx.Ops)
	paint.ColorOp{
		Color: color.NRGBA{192, 192, 192, 255},
	}.Add(gtx.Ops)
	paint.PaintOp{}.Add(gtx.Ops)

	base.Load()

	// render slider foreground
	clip.Rect{
		Min: image.Point{
			X: int(float64(gtx.Constraints.Max.X) * p.SliderStart),
			Y: 0,
		},
		Max: image.Point{
			X: int(float64(gtx.Constraints.Max.X) * p.SliderEnd),
			Y: sliderY,
		},
	}.Add(gtx.Ops)
	paint.ColorOp{
		Color: color.NRGBA{128, 128, 128, 255},
	}.Add(gtx.Ops)
	paint.PaintOp{}.Add(gtx.Ops)

	if p.NewSliderStart != -1 {
		base.Load()
		// render substitute slider
		start := p.NewSliderStart
		end := p.NewSliderEnd
		if end == -1 {
			end = math.Min(1, start+0.05)
			start = math.Max(0, start-0.05)
		}
		clip.Rect{
			Min: image.Point{
				X: int(float64(gtx.Constraints.Max.X) * start),
				Y: 0,
			},
			Max: image.Point{
				X: int(float64(gtx.Constraints.Max.X) * end),
				Y: sliderY,
			},
		}.Add(gtx.Ops)
		paint.ColorOp{
			Color: color.NRGBA{192, 128, 128, 255},
		}.Add(gtx.Ops)
		paint.PaintOp{}.Add(gtx.Ops)
	}

	base.Load()

	op.Offset(f32.Point{Y: float32(sliderY * 2)}).Add(gtx.Ops)

	// render everything else
	clip.Rect{
		Min: image.Point{
			X: 0,
			Y: 0,
		},
		Max: image.Point{
			X: gtx.Constraints.Max.X,
			Y: gtx.Constraints.Max.Y - sliderY*2,
		},
	}.Add(gtx.Ops)
	paint.NewImageOp(p.GetImage(image.Point{
		X: gtx.Constraints.Max.X,
		Y: gtx.Constraints.Max.Y - sliderY*2,
	})).Add(gtx.Ops)
	paint.PaintOp{}.Add(gtx.Ops)

	return layout.Dimensions{Size: gtx.Constraints.Max}
}

func (p *PlotWidget) Export() {
	if p.ExportDir != "" {
		filepath := path.Join(p.ExportDir, "timeline.png")
		f, err := os.Create(filepath)
		if err != nil {
			log.Fatal(err)
		}
		err = png.Encode(f, p.Image)
		if err != nil {
			log.Fatal(err)
		}
		err = f.Close()
		if err != nil {
			log.Fatal(err)
		}
		log.Printf("Image exported!")
	}
}

func DisplayPlot(p *plot.Plot) error {
	return DisplayPlotExportable(p, "")
}

func DisplayPlotExportable(p *plot.Plot, exportDir string) error {
	plotWidget := &PlotWidget{
		Plot:           p,
		DPI:            128,
		ExportDir:      exportDir,
		Ready:          make(chan image.Image),
		SliderStart:    0,
		SliderEnd:      1,
		NewSliderStart: -1,
		NewSliderEnd:   -1,
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

		// var nth = 0

		for {
			select {
			case ready := <-plotWidget.Ready:
				plotWidget.OnReady(ready)
				win.Invalidate()
			case e := <-win.Events():
				switch e := e.(type) {
				case system.FrameEvent:
					/*
						var f io.WriteCloser
						if nth == 1 {
							var err error
							f, err = os.Create("cpu.profile")
							if err != nil {
								log.Fatal(err)
							}
							err = pprof.StartCPUProfile(f)
							if err != nil {
								log.Fatal(err)
							}
						}
						start := time.Now()
						log.Printf("Start: %v", start)
					*/
					ops := new(op.Ops)
					gtx := layout.NewContext(ops, e)
					layout.UniformInset(unit.Dp(30)).Layout(gtx, plotWidget.Layout)
					e.Frame(ops)
					/*
						end := time.Now()
						log.Printf("Duration: %v", end.Sub(start))
						if nth == 1 {
							pprof.StopCPUProfile()
							err := f.Close()
							if err != nil {
								log.Fatal(err)
							}
							log.Printf("Wrote cpu profile")
						}
						nth += 1
					*/
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
		}
	}()

	app.Main()
	return nil
}
