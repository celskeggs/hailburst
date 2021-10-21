package tlplot

import (
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/font"
	"gonum.org/v1/plot/plotter"
	"gonum.org/v1/plot/text"
	"gonum.org/v1/plot/vg"
	"gonum.org/v1/plot/vg/draw"
	"image/color"
	"log"
	"math"
	"sort"
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
	Label string
}

type TimelinePlot struct {
	Activities []Activity
	Markers    []Marker
	Location   float64
	Height     vg.Length
	LastX      float64
	BoxStyle   draw.LineStyle
	TextStyle  draw.TextStyle
}

var _ plot.Plotter = &TimelinePlot{}

func NewTimelinePlot(activities []Activity, markers []Marker, loc float64, height vg.Length, lastX float64) *TimelinePlot {
	// render markers right-to-left
	sort.Slice(markers, func(i, j int) bool {
		return markers[i].Time > markers[j].Time
	})
	return &TimelinePlot{
		Activities: activities,
		Markers:    markers,
		Location:   loc,
		Height:     height,
		LastX:      lastX,
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

func splitRows(text string, nrows int) (out []string) {
	out = make([]string, nrows)
	for i := 0; i < nrows; i++ {
		out[i] = text[len(text)*i/nrows : len(text)*(i+1)/nrows]
	}
	return out
}

func maxWidths(style draw.TextStyle, rows []string) (mw vg.Length) {
	for _, row := range rows {
		mw = vg.Length(math.Max(float64(mw), float64(style.Width(row))))
	}
	return mw
}

func searchTextSize(style draw.TextStyle, text string, maxWidth vg.Length) (result draw.TextStyle, rows []string) {
	log.Printf("searching style for text of length %d", len(text))
	defer log.Printf("finished searching style")
	rows = []string{text}
	for maxWidths(style, rows) > maxWidth {
		style.Font.Size *= 0.5
		if maxWidths(style, rows) > maxWidth {
			rows = splitRows(text, len(rows)*2)
		}
	}
	return style, rows
}

func fillSingleRow(c draw.Canvas, style draw.TextStyle, text string, posX vg.Length, posY vg.Length) {
	c.FillText(style, vg.Point{
		X: posX + style.Width(text)/2,
		Y: posY,
	}, text)
}

func fillMultiRow(c draw.Canvas, style draw.TextStyle, rows []string, posX vg.Length, posYMin vg.Length, posYMax vg.Length) {
	for i, row := range rows {
		var frac float64
		if len(rows) == 1 {
			frac = 0.5
		} else {
			frac = float64(i) / float64(len(rows)-1)
		}
		fillSingleRow(c, style, row, posX, posYMin+(posYMax-posYMin)*vg.Length(frac))
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
		if activity.Label != "" && t.TextStyle.Width(activity.Label)+xStart <= xEnd && c.ContainsX(xStart) {
			c.FillText(t.TextStyle, vg.Point{
				X: (xStart + xEnd) / 2,
				Y: y,
			}, activity.Label)
		}
	}

	lastClipX := t.LastX
	for _, marker := range t.Markers {
		xPos := trX(marker.Time)
		c.DrawGlyph(marker.Glyph, vg.Point{
			X: xPos,
			Y: y,
		})
		startPos := xPos + marker.Glyph.Radius
		endPos := trX(lastClipX) - marker.Glyph.Radius
		if marker.Label != "" && startPos < endPos {
			style, rows := searchTextSize(t.TextStyle, marker.Label, endPos-startPos)
			fillMultiRow(c, style, rows, startPos, y-t.Height/2, y+t.Height/2)
		}
		lastClipX = marker.Time
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
