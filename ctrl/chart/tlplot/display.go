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

type imageAndRegion struct {
	Image     image.Image
	Rectangle vg.Rectangle
}

type SizeGrabber struct {
	Rectangle vg.Rectangle
}

func (s *SizeGrabber) Plot(canvas draw.Canvas, p *plot.Plot) {
	s.Rectangle = canvas.Rectangle
}

type PlotWidget struct {
	Plot        *plot.Plot
	SizeGrabber *SizeGrabber
	BaseX       plot.Axis
	DPI         int
	ExportDir   string
	AdjWidth    vg.Length
	AdjHeight   vg.Length

	SliderMin, SliderMax       float64 // in axis units
	IsSettingNewSlider         bool
	NewSliderA, NewSliderB     float64
	GenSliderMin, GenSliderMax float64

	Busy       bool
	Ready      chan imageAndRegion
	Image      image.Image
	DrawRegion vg.Rectangle
}

func interpolate(low, high, frac float64) float64 {
	if frac < 0 || frac > 1 {
		panic("out of range")
	}
	return low*(1-frac) + high*frac
}

func (p *PlotWidget) GenImage(w, h vg.Length) (image.Image, vg.Rectangle) {
	c := vgimg.NewWith(vgimg.UseWH(w, h), vgimg.UseDPI(p.DPI))
	p.Plot.Draw(draw.New(c))
	return c.Image(), p.SizeGrabber.Rectangle
}

func (p *PlotWidget) OnReady(ready imageAndRegion) {
	if !p.Busy {
		panic("should be busy")
	}
	p.Image = ready.Image
	p.DrawRegion = ready.Rectangle
	p.Busy = false
}

func (p *PlotWidget) ForwardTransform(axisX float64) (pixelX float32, ok bool) {
	fracX := p.Plot.X.Norm(axisX)

	c := p.DrawRegion
	if c.Min.X >= c.Max.X {
		return 0, false
	}
	lenX := vg.Length(fracX)*(c.Max.X-c.Min.X) + c.Min.X

	return float32(float64(lenX) / vg.Inch.Points() * float64(p.DPI)), true
}

func (p *PlotWidget) ReverseTransform(pixelX float32) (axisX float64, ok bool) {
	// first, convert pixels to length units
	lenX := vg.Points(float64(pixelX) * vg.Inch.Points() / float64(p.DPI))

	// second, convert length units to drawing area fraction
	c := p.DrawRegion
	if c.Min.X >= c.Max.X {
		return 0, false
	}
	fracX := float64((lenX - c.Min.X) / (c.Max.X - c.Min.X))

	// clip to [0,1] range
	fracX = math.Max(0, math.Min(1, fracX))

	// finally, convert drawing area fraction to plot units
	if _, ok := p.Plot.X.Scale.(plot.LinearScale); ok {
		return fracX*(p.Plot.X.Max-p.Plot.X.Min) + p.Plot.X.Min, true
	} else {
		// not implemented for other types of plot scale
		return 0, false
	}
}

func (p *PlotWidget) GetImage(size image.Point) image.Image {
	wAdjusted := vg.Points(float64(size.X) * vg.Inch.Points() / float64(p.DPI))
	hAdjusted := vg.Points(float64(size.Y) * vg.Inch.Points() / float64(p.DPI))
	if p.Image == nil {
		p.Plot.X.Min = p.SliderMin
		p.Plot.X.Max = p.SliderMax
		p.Image, p.DrawRegion = p.GenImage(wAdjusted, hAdjusted)
		p.AdjWidth = wAdjusted
		p.AdjHeight = hAdjusted
		p.GenSliderMin = p.SliderMin
		p.GenSliderMax = p.SliderMax
	} else if p.AdjWidth != wAdjusted || p.AdjHeight != hAdjusted || p.SliderMin != p.GenSliderMin || p.SliderMax != p.GenSliderMax {
		if !p.Busy {
			p.Busy = true
			p.Plot.X.Min = p.SliderMin
			p.Plot.X.Max = p.SliderMax
			go func() {
				rendered, region := p.GenImage(wAdjusted, hAdjusted)
				p.Ready <- imageAndRegion{
					Image:     rendered,
					Rectangle: region,
				}
			}()
			p.AdjWidth = wAdjusted
			p.AdjHeight = hAdjusted
			p.GenSliderMin = p.SliderMin
			p.GenSliderMax = p.SliderMax
		}
	}

	return p.Image
}

var layoutTag = new(struct{})

func (p *PlotWidget) drawSliderRect(gtx layout.Context, startAxisX, endAxisX float64, color color.NRGBA) {
	defer op.Save(gtx.Ops).Load()

	minX, ok1 := p.ForwardTransform(startAxisX)
	maxX, ok2 := p.ForwardTransform(endAxisX)
	if !ok1 || !ok2 {
		return
	}
	// need to use RRect to avoid integer rounding
	clip.UniformRRect(f32.Rect(minX, 0, maxX, float32(gtx.Constraints.Max.Y)), 0).Add(gtx.Ops)
	paint.ColorOp{
		Color: color,
	}.Add(gtx.Ops)
	paint.PaintOp{}.Add(gtx.Ops)
}

func (p *PlotWidget) ZoomOut() {
	p.IsSettingNewSlider = false
	size := p.SliderMax - p.SliderMin
	p.SliderMin -= size / 2
	p.SliderMax += size / 2
	if p.SliderMin < p.BaseX.Min {
		p.SliderMin = p.BaseX.Min
	}
	if p.SliderMax > p.BaseX.Max {
		p.SliderMax = p.BaseX.Max
	}
}

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
			// frac := math.Max(0, math.Min(1, float64(x.Position.X)/float64(gtx.Constraints.Max.X)))
			axisX, ok := p.ReverseTransform(x.Position.X)
			if ok {
				switch x.Type {
				case pointer.Press:
					if x.Buttons.Contain(pointer.ButtonSecondary) {
						p.IsSettingNewSlider = false
						p.SliderMin = p.BaseX.Min
						p.SliderMax = p.BaseX.Max
					} else {
						p.IsSettingNewSlider = true
						p.NewSliderA = axisX
						p.NewSliderB = axisX
					}
				case pointer.Drag:
					if !x.Buttons.Contain(pointer.ButtonSecondary) && p.IsSettingNewSlider {
						p.NewSliderB = axisX
					}
				case pointer.Release:
					if !x.Buttons.Contain(pointer.ButtonSecondary) {
						if p.IsSettingNewSlider && p.NewSliderA != p.NewSliderB {
							p.SliderMin = math.Min(p.NewSliderA, p.NewSliderB)
							p.SliderMax = math.Max(p.NewSliderA, p.NewSliderB)
						}
					}
					p.IsSettingNewSlider = false
				}
			}
		}
	}

	pointer.Rect(image.Rectangle{
		Max: gtx.Constraints.Max,
	}).Add(gtx.Ops)
	pointer.InputOp{
		Tag:   layoutTag,
		Types: pointer.Press | pointer.Drag | pointer.Release,
	}.Add(gtx.Ops)

	// clip entire slider region
	clip.Rect{
		Max: image.Point{
			X: gtx.Constraints.Max.X,
			Y: sliderY,
		},
	}.Add(gtx.Ops)

	oldConstraints := gtx.Constraints
	gtx.Constraints.Max.Y = sliderY

	p.drawSliderRect(gtx, p.BaseX.Min, p.BaseX.Max, color.NRGBA{192, 192, 192, 255})
	p.drawSliderRect(gtx, p.SliderMin, p.SliderMax, color.NRGBA{128, 128, 128, 255})
	if p.IsSettingNewSlider {
		p.drawSliderRect(gtx, p.NewSliderA, p.NewSliderB, color.NRGBA{192, 128, 128, 255})
	}

	gtx.Constraints = oldConstraints
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
		Plot:        p,
		SizeGrabber: &SizeGrabber{},
		BaseX:       p.X,
		DPI:         128,
		ExportDir:   exportDir,
		SliderMin:   p.X.Min,
		SliderMax:   p.X.Max,
		Ready:       make(chan imageAndRegion),
	}
	p.Add(plotWidget.SizeGrabber)

	go func() {
		win := app.NewWindow(
			app.Title("Plot Interface"),
			app.Size(
				unit.Px(1300),
				unit.Px(800),
			),
		)
		defer win.Close()

		for {
			select {
			case ready := <-plotWidget.Ready:
				plotWidget.OnReady(ready)
				win.Invalidate()
			case e := <-win.Events():
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
					case "-":
						if e.State == key.Press {
							plotWidget.ZoomOut()
							win.Invalidate()
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
