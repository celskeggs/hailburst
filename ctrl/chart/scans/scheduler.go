package scans

import (
	"bufio"
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/sim/model"
	"gonum.org/v1/plot/vg"
	"image/color"
	"io"
	"math"
	"os"
	"strconv"
	"strings"
	"time"
)

type SchedEntry struct {
	Timestamp model.VirtualTime
	TaskName string
	Priority int
}

type SchedScan struct {
	Entries []SchedEntry
}

func (rs *SchedScan) Label() string {
	return "Schedule"
}

func (rs *SchedScan) LastTime() float64 {
	latest := 0.0
	for _, entry := range rs.Entries {
		latest = math.Max(latest, entry.Timestamp.Since(model.TimeZero).Seconds())
	}
	return latest
}

func (rs *SchedScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var activities []tlplot.Activity

	endTime := lastTime
	for i := len(rs.Entries) - 1; i >= 0; i-- {
		entry := rs.Entries[i]
		start := entry.Timestamp.Since(model.TimeZero).Seconds()
		activities = append(activities, tlplot.Activity{
			Start: start,
			End:   endTime,
			Color: color.RGBA{128, 255, 128, 255},
			Label: fmt.Sprintf("[%d] %s", entry.Priority, entry.TaskName),
		})
		endTime = start
	}

	return tlplot.NewTimelinePlot(activities, nil, location, vg.Points(15), lastTime)
}

func ScanScheduler(guestLogPath string) (*SchedScan, error) {
	f, err := os.Open(guestLogPath)
	if err != nil {
		return nil, err
	}
	defer func() {
		_ = f.Close()
	}()
	br := bufio.NewReader(f)
	var results SchedScan
	for {
		line, err := br.ReadString('\n')
		if err == io.EOF && len(line) == 0 {
			return &results, nil
		}
		if err != nil {
			return nil, err
		}
		if strings.HasPrefix(line, "[") {
			parts := strings.Split(line[1:len(line) - 1], "] FreeRTOS scheduling ")
			timestampSec, err := strconv.ParseFloat(parts[0], 64)
			if len(parts) == 2 && err == nil {
				p2 := strings.Split(parts[1], " at priority ")
				taskName := strings.TrimSpace(p2[0])
				if len(p2) == 2 {
					priority, err := strconv.ParseUint(p2[1], 10, 31)
					if err == nil {
						timestampV := model.TimeZero.Add(time.Duration(float64(time.Second) * timestampSec))
						results.Entries = append(results.Entries, SchedEntry{
							Timestamp: timestampV,
							TaskName:  taskName,
							Priority:  int(priority),
						})
					}
				}
			}
		}
	}
}
