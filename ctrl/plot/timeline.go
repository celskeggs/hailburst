package main

import (
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/font"
	"gonum.org/v1/plot/plotter"
	"gonum.org/v1/plot/text"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"image/color"
)

type Activity struct {
	Start float64
	End   float64
	Color color.Color
	Label string
}

type Marker struct {
	Time  float64
	Glyph draw.GlyphStyle
}

type TimelinePlot struct {
	Activities []Activity
	Markers    []Marker
	Location   float64
	Height     vg.Length
	BoxStyle   draw.LineStyle
	TextStyle  draw.TextStyle
}

var _ plot.Plotter = &TimelinePlot{}

func NewTimelinePlot(activities []Activity, markers []Marker, loc float64, height vg.Length) *TimelinePlot {
	return &TimelinePlot{
		Activities: activities,
		Markers:    markers,
		Location:   loc,
		Height:     height,
		BoxStyle:   plotter.DefaultLineStyle,
		TextStyle: text.Style{
			Font:     font.From(plotter.DefaultFont, plotter.DefaultFontSize),
			Rotation: 0,
			XAlign:   draw.XCenter,
			YAlign:   draw.YCenter,
			Handler:  plot.DefaultTextHandler,
		},
	}
}

func (t *TimelinePlot) Plot(c draw.Canvas, plt *plot.Plot) {
	trX, trY := plt.Transforms(&c)
	y := trY(t.Location)
	if !c.ContainsY(y) {
		return
	}

	for _, activity := range t.Activities {
		xStart, xEnd := trX(activity.Start), trX(activity.End)
		pts := []vg.Point{
			{X: xStart, Y: y - t.Height/2},
			{X: xEnd, Y: y - t.Height/2},
			{X: xEnd, Y: y + t.Height/2},
			{X: xStart, Y: y + t.Height/2},
			{X: xStart, Y: y - t.Height/2},
		}
		c.FillPolygon(activity.Color, c.ClipPolygonX(pts[0:4]))
		c.StrokeLines(t.BoxStyle, c.ClipLinesX(pts)...)
		if activity.Label != "" {
			c.FillText(t.TextStyle, vg.Point{
				X: (xStart + xEnd) / 2,
				Y: y,
			}, activity.Label)
		}
	}

	for _, marker := range t.Markers {
		c.DrawGlyph(marker.Glyph, vg.Point{
			X: trX(marker.Time),
			Y: y,
		})
	}
}

type xyconv TimelinePlot

func (t *xyconv) Len() int {
	return len(t.Markers) + len(t.Activities)*2
}

func (t *xyconv) XY(i int) (x, y float64) {
	if i < len(t.Markers) {
		return t.Markers[i].Time, t.Location
	} else {
		i -= len(t.Markers)
	}
	if i < len(t.Activities) {
		return t.Activities[i].Start, t.Location
	} else {
		i -= len(t.Activities)
	}
	if i < len(t.Activities) {
		return t.Activities[i].End, t.Location
	} else {
		panic("invalid index")
	}
}

func (t *TimelinePlot) DataRange() (xmin, xmax, ymin, ymax float64) {
	return plotter.XYRange((*xyconv)(t))
}

/*
func (t *TimelinePlot) GlyphBoxes(plt *plot.Plot) (boxes []plot.GlyphBox) {
	for _, act := range t.Activities {
		rect := vg.Rectangle{
			Min: vg.Point{
				X: -t.BoxStyle.Width,
				Y: -t.Height / 2,
			},
			Max: vg.Point{
				X: t.BoxStyle.Width,
				Y: t.Height / 2,
			},
		}
		boxes = append(boxes,
			plot.GlyphBox{
				X:         plt.X.Norm(act.Start),
				Y:         plt.Y.Norm(t.Location),
				Rectangle: rect,
			},
			plot.GlyphBox{
				X:         plt.X.Norm(act.End),
				Y:         plt.Y.Norm(t.Location),
				Rectangle: rect,
			},
		)
	}
	return boxes
}
*/
