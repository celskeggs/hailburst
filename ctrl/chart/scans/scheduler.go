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
	EndTime   model.VirtualTime
	TaskName  string
	Priority  uint
}

type SchedScan struct {
	Priority uint
	Entries  []SchedEntry
}

func (rs *SchedScan) Label() string {
	return fmt.Sprintf("Priority %d", rs.Priority)
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

	for _, entry := range rs.Entries {
		startTime := entry.Timestamp.Since(model.TimeZero).Seconds()
		var endTime float64
		if entry.EndTime.TimeExists() {
			endTime = entry.EndTime.Since(model.TimeZero).Seconds()
		} else {
			endTime = lastTime
		}
		activities = append(activities, tlplot.Activity{
			Start: startTime,
			End:   endTime,
			Color: color.RGBA{128, 255, 128, 255},
			Label: entry.TaskName,
		})
		endTime = startTime
	}

	return tlplot.NewTimelinePlot(activities, nil, location, vg.Points(15), lastTime)
}

func ScanScheduler(guestLogPath string) ([]*SchedScan, error) {
	f, err := os.Open(guestLogPath)
	if err != nil {
		return nil, err
	}
	defer func() {
		_ = f.Close()
	}()
	br := bufio.NewReader(f)
	results := map[uint64]*SchedScan{}
	var lastPriority uint64 = math.MaxUint64
	for {
		line, err := br.ReadString('\n')
		if err == io.EOF && len(line) == 0 {
			break
		}
		if err != nil {
			return nil, err
		}
		if strings.HasPrefix(line, "[") {
			parts := strings.Split(line[1:len(line)-1], "] FreeRTOS scheduling ")
			timestampSec, err := strconv.ParseFloat(parts[0], 64)
			if len(parts) == 2 && err == nil {
				p2 := strings.Split(parts[1], " at priority ")
				taskName := strings.TrimSpace(p2[0])
				if len(p2) == 2 {
					priority, err := strconv.ParseUint(p2[1], 10, 31)
					if err == nil {
						timestampV := model.TimeZero.Add(time.Duration(float64(time.Second) * timestampSec))
						if results[priority] == nil {
							results[priority] = &SchedScan{
								Priority: uint(priority),
							}
						}
						if lastPriority != math.MaxUint64 {
							lastPri := results[lastPriority]
							lastPri.Entries[len(lastPri.Entries)-1].EndTime = timestampV
						}
						results[priority].Entries = append(results[priority].Entries, SchedEntry{
							Timestamp: timestampV,
							TaskName:  taskName,
							Priority:  uint(priority),
							EndTime:   model.TimeNever,
						})
						lastPriority = priority
					}
				}
			}
		}
	}
	if len(results) == 0 {
		return nil, nil
	}
	var min, max uint64 = math.MaxUint64, 0
	for priority, _ := range results {
		if priority < min {
			min = priority
		}
		if priority > max {
			max = priority
		}
	}
	if min > max {
		panic("invalid comparison")
	}
	flattened := make([]*SchedScan, max-min+1)
	for priority := min; priority <= max; priority++ {
		if results[priority] == nil {
			flattened[priority-min] = &SchedScan{
				Priority: uint(priority),
			}
		} else {
			flattened[priority-min] = results[priority]
		}
	}
	return flattened, nil
}
