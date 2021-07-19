package tlplot

import (
	"github.com/hashicorp/go-multierror"
	"gonum.org/v1/plot"
	"gonum.org/v1/plot/vg"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
)

func WritePlot(p *plot.Plot, output io.Writer, format string) error {
	w, err := p.WriterTo(8*vg.Inch, 6*vg.Inch, format)
	if err != nil {
		return err
	}
	_, err = w.WriteTo(output)
	return err
}

func combineErrors(errors ...error) (err error) {
	for _, e := range errors {
		switch {
		case e == nil:
			// ignore
		case err == nil:
			err = e
		default:
			err = multierror.Append(err, e)
		}
	}
	return err
}

func WriteClosePlot(p *plot.Plot, output io.WriteCloser, format string) (err error) {
	defer func() {
		e := output.Close()
		err = combineErrors(err, e)
	}()
	return WritePlot(p, output, format)
}

func SavePlot(p *plot.Plot, path string, format string) error {
	output, err := os.Create(path)
	if err != nil {
		return err
	}
	return WriteClosePlot(p, output, format)
}

func DisplayPlotExternal(p *plot.Plot) (err error) {
	f, err := ioutil.TempFile("", "output-*.png")
	if err != nil {
		return err
	}
	defer func() {
		e := os.Remove(f.Name())
		err = combineErrors(err, e)
	}()
	if err := WriteClosePlot(p, f, "png"); err != nil {
		return err
	}
	return exec.Command("gpicview", f.Name()).Run()
}
