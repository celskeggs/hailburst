package scans

import (
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/ctrl/chart/viterbi"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/spacecraft"
	"gonum.org/v1/plot/vg"
	"image/color"
	"log"
	"math"
	"time"
)

const ViterbiGranularity = time.Millisecond * 50

type ReqViterbiInterval struct {
	Mode  viterbi.HiddenState
	Start model.VirtualTime
	End   model.VirtualTime
}

func (ri *ReqViterbiInterval) Duration() time.Duration {
	return ri.End.Since(ri.Start)
}

type ReqViterbiIntervalScan struct {
	RequirementName string
	Intervals       []ReqViterbiInterval
}

func (r *ReqViterbiIntervalScan) Label() string {
	return r.RequirementName
}

func (r *ReqViterbiIntervalScan) LastTime() float64 {
	latest := 0.0
	for _, interval := range r.Intervals {
		latest = math.Max(latest, interval.End.Since(model.TimeZero).Seconds())
	}
	return latest
}

func ViterbiRender(state viterbi.HiddenState) (c color.RGBA, label string) {
	switch state {
	case viterbi.FullyWorking: // 78c679
		return color.RGBA{0x78, 0xC6, 0x79, 255}, "Working"
	case viterbi.PartiallyWorking:
		return color.RGBA{0xFF, 0xFF, 0xB2, 255}, "Partial"
	case viterbi.CompletelyBroken:
		return color.RGBA{0xE3, 0x1A, 0x1C, 255}, "Broken"
	default:
		panic("invalid mode")
	}
}

func (r *ReqViterbiIntervalScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var activities []tlplot.Activity

	for _, interval := range r.Intervals {
		c, label := ViterbiRender(interval.Mode)
		activities = append(activities, tlplot.Activity{
			Start: interval.Start.Since(model.TimeZero).Seconds(),
			End:   interval.End.Since(model.TimeZero).Seconds(),
			Color: c,
			Label: label,
		})
	}

	return tlplot.NewTimelinePlot(activities, nil, location, vg.Points(10), lastTime)
}

func ScanReqSummaryViterbi(path string) ([]*ReqViterbiIntervalScan, error) {
	raw, err := ScanRawReqs(path)
	if err != nil {
		return nil, err
	}
	log.Printf("Beginning Viterbi analysis...")
	startTimeVT := spacecraft.MissionStartTime
	startTime := startTimeVT.Since(model.TimeZero).Seconds()
	latest := 0.0
	for _, req := range raw {
		latest = math.Max(latest, req.LastTime())
	}
	if latest <= startTime {
		panic("invalid time spread")
	}
	periods := int(math.Ceil((latest - startTime + time.Nanosecond.Seconds()) / ViterbiGranularity.Seconds()))
	if periods < 1 {
		panic("too few periods")
	}
	buckets := make([][]viterbi.ViterbiObservation, periods)
	for _, req := range raw {
		for i, category := range [2][]model.VirtualTime{req.Failures, req.Successes} {
			for _, timestamp := range category {
				bucket := int(timestamp.Since(startTimeVT) / ViterbiGranularity)
				buckets[bucket] = append(buckets[bucket], viterbi.ViterbiObservation{
					Requirement: req.RequirementName,
					Success:     i == 1,
				})
			}
		}
	}

	state := viterbi.InitialViterbiState()
	for _, bucket := range buckets {
		state.NextPeriod(bucket)
	}
	viterbiPath := state.ExtractPath()
	if len(viterbiPath) != periods + 1 {
		panic("invalid path")
	}
	var intervals []ReqViterbiInterval
	startPoint := 0
	for startPoint < len(viterbiPath) {
		endPoint := startPoint + 1
		for endPoint < len(viterbiPath) && viterbiPath[startPoint] == viterbiPath[endPoint] {
			endPoint += 1
		}
		intervals = append(intervals, ReqViterbiInterval{
			Mode:  viterbiPath[startPoint],
			Start: startTimeVT.Add(ViterbiGranularity * time.Duration(startPoint - 1)),
			End:   startTimeVT.Add(ViterbiGranularity * time.Duration(endPoint - 1)),
		})
		startPoint = endPoint
	}
	log.Printf("Viterbi analysis complete!")

	return []*ReqViterbiIntervalScan{
		{
			RequirementName: "Viterbi Status",
			Intervals:       intervals,
		},
	}, nil
}
