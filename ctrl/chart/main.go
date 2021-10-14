package main

import (
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/chart/scans"
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/pkg/errors"
	"gonum.org/v1/plot"
	"log"
	"math"
	"os"
	"path"
	"reflect"
	"strconv"
	"strings"
	"unicode"
)

type Options struct {
	ShowRawReqs       bool
	ShowSummary       bool
	ShowIoRec         bool
	ShowSchedule      bool
	ShowInjections    bool
	ShowIoLog         bool
	ShowSystemDetails bool
}

func DefaultOptions() Options {
	return Options{
		ShowRawReqs:       true,
		ShowSummary:       false,
		ShowSystemDetails: true,
		ShowIoRec:         false,
		ShowSchedule:      false,
		ShowInjections:    true,
		ShowIoLog:         true,
	}
}

func (o *Options) SetOption(name string, value bool) bool {
	for _, c := range name {
		if unicode.IsUpper(c) {
			return false
		}
	}
	parts := strings.Split(name, "-")
	for i, part := range parts {
		parts[i] = strings.ToUpper(part[:1]) + part[1:]
	}
	optionName := "Show" + strings.Join(parts, "")
	v := reflect.ValueOf(o).Elem()
	for i := 0; i < v.NumField(); i++ {
		if optionName == v.Type().Field(i).Name {
			v.Field(i).SetBool(value)
			return true
		}
	}
	return false
}

func ScanAll(dir string, options Options) (out []scans.ScannedLine, err error) {
	if options.ShowSummary {
		records, err := scans.ScanReqSummary(path.Join(dir, "reqs-raw.log"), options.ShowSystemDetails)
		if err != nil {
			return nil, errors.Wrap(err, "while scanning requirements")
		}
		for _, record := range records {
			out = append(out, record)
		}
	} else if options.ShowRawReqs {
		records, err := scans.ScanRawReqs(path.Join(dir, "reqs-raw.log"))
		if err != nil {
			return nil, errors.Wrap(err, "while scanning requirements")
		}
		for _, record := range records {
			out = append(out, record)
		}
	}
	if options.ShowIoRec {
		ioRecs, err := scans.ScanIORecord(path.Join(dir, "io-dump.csv"))
		if err != nil {
			return nil, errors.Wrap(err, "while scanning io dump records")
		}
		for _, ioRec := range ioRecs {
			out = append(out, ioRec)
		}
	}
	if options.ShowSchedule {
		schedule, err := scans.ScanScheduler(path.Join(dir, "guest.log"))
		if err != nil {
			return nil, errors.Wrap(err, "while scanning guest.log for schedule")
		}
		for _, priority := range schedule {
			out = append(out, priority)
		}
	}
	if options.ShowInjections {
		injections, err := scans.ScanInjections(path.Join(dir, "injections.csv"))
		if err != nil {
			return nil, errors.Wrap(err, "while scanning injections")
		}
		out = append(out,
			injections,
		)
	}
	if options.ShowIoLog {
		ioLog, err := scans.ScanIOLog(path.Join(dir, "timesync.sock.log"))
		if err != nil {
			return nil, errors.Wrap(err, "while scanning timesync log")
		}
		out = append(out,
			ioLog,
		)
	}
	return out, nil
}

type PreciseTicks struct{}

func (p PreciseTicks) Ticks(min, max float64) []plot.Tick {
	ticks := plot.DefaultTicks{}.Ticks(min, max)
	for i := range ticks {
		ticks[i].Label = strconv.FormatFloat(ticks[i].Value, 'f', 6, 64)
	}
	return ticks
}

func GeneratePlot(dirs []string, options Options) (*plot.Plot, error) {
	p := plot.New()
	if len(dirs) == 1 {
		p.Title.Text = "Timeline: " + path.Base(dirs[0])
	} else {
		p.Title.Text = fmt.Sprintf("Timelines from %d Trials", len(dirs))
	}
	p.X.Label.Text = "Virtual Time"
	p.Y.Label.Text = "Requirement"

	var lines []scans.ScannedLine
	for _, dir := range dirs {
		scanResults, err := ScanAll(dir, options)
		if err != nil {
			return nil, err
		}
		lines = append(lines, scanResults...)
	}

	lastTime := 0.0
	for _, scan := range lines {
		lastTime = math.Max(lastTime, scan.LastTime())
	}
	var names []string
	for i, scan := range lines {
		p.Add(scan.BuildPlot(lastTime, float64(i)))
		names = append(names, scan.Label())
	}
	p.NominalY(names...)
	p.X.Tick.Marker = PreciseTicks{}

	return p, nil
}

func main() {
	options := DefaultOptions()
	var trialDirs []string
	var usage bool
	for i := 1; i < len(os.Args); i++ {
		if strings.HasPrefix(os.Args[i], "--show-") {
			if !options.SetOption(os.Args[i][7:], true) {
				log.Fatalf("Invalid option: %q", os.Args[i][7:])
			}
		} else if strings.HasPrefix(os.Args[i], "--hide-") {
			if !options.SetOption(os.Args[i][7:], false) {
				log.Fatalf("Invalid option: %q", os.Args[i][7:])
			}
		} else if strings.HasPrefix(os.Args[i], "-") {
			usage = true
			break
		} else if os.Args[i] != "" {
			trialDirs = append(trialDirs, os.Args[i])
		}
	}
	if len(trialDirs) == 0 {
		usage = true
	}
	if usage {
		log.Fatalf("Usage: %s (--show X | --hide X)* <trial-dir> <trial-dir> ...", path.Base(os.Args[0]))
	}

	p, err := GeneratePlot(trialDirs, options)
	if err != nil {
		log.Fatal("error while generating plot: ", err)
	}

	exportDir := "."
	if len(trialDirs) == 1 {
		exportDir = trialDirs[0]
	}

	if err := tlplot.DisplayPlotExportable(p, exportDir); err != nil {
		log.Fatal("error while displaying plot: ", err)
	}
}
