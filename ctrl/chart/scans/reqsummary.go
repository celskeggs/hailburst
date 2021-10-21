package scans

import (
	"github.com/celskeggs/hailburst/ctrl/chart/tlplot"
	"github.com/celskeggs/hailburst/sim/model"
	"gonum.org/v1/plot/vg"
	"image/color"
	"log"
	"math"
	"sort"
	"time"
)

type IntervalMode string

const (
	IntervalReliable     IntervalMode = "Reliable"
	IntervalIntermittent IntervalMode = "Intermittent"
	IntervalLimited      IntervalMode = "Limited" // only used for system status
	IntervalBroken       IntervalMode = "Broken"
)

func (i IntervalMode) Color() color.RGBA {
	switch i {
	case IntervalReliable:
		return color.RGBA{0, 255, 0, 255}
	case IntervalIntermittent:
		return color.RGBA{255, 255, 0, 255}
	case IntervalLimited:
		return color.RGBA{255, 255, 0, 255}
	case IntervalBroken:
		return color.RGBA{255, 0, 0, 255}
	default:
		panic("invalid mode")
	}
}

func (i IntervalMode) Label() string {
	return string(i)
}

type ReqInterval struct {
	Mode       IntervalMode
	Start      model.VirtualTime
	End        model.VirtualTime
	PointCount int
}

func (ri *ReqInterval) Duration() time.Duration {
	return ri.End.Since(ri.Start)
}

type ReqIntervalScan struct {
	RequirementName string
	Intervals       []ReqInterval
}

func (r *ReqIntervalScan) Label() string {
	return r.RequirementName
}

func (r *ReqIntervalScan) LastTime() float64 {
	latest := 0.0
	for _, interval := range r.Intervals {
		latest = math.Max(latest, interval.End.Since(model.TimeZero).Seconds())
	}
	return latest
}

func (r *ReqIntervalScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var activities []tlplot.Activity

	for _, interval := range r.Intervals {
		activities = append(activities, tlplot.Activity{
			Start: interval.Start.Since(model.TimeZero).Seconds(),
			End:   interval.End.Since(model.TimeZero).Seconds(),
			Color: interval.Mode.Color(),
			Label: interval.Mode.Label(),
		})
	}

	return tlplot.NewTimelinePlot(activities, nil, location, vg.Points(10), lastTime)
}

func PreliminaryIntervals(successes []model.VirtualTime, failures []model.VirtualTime) (intervals []ReqInterval) {
	var local *ReqInterval

	if !sort.SliceIsSorted(successes, func(i, j int) bool {
		return successes[i] < successes[j]
	}) || !sort.SliceIsSorted(failures, func(i, j int) bool {
		return failures[i] < failures[j]
	}) {
		panic("expected successes and failures to be sorted")
	}

	for len(successes) > 0 || len(failures) > 0 {
		mode := IntervalReliable
		if len(failures) > 0 && (len(successes) == 0 || successes[0].After(failures[0])) {
			mode = IntervalBroken
		}
		var nextTime model.VirtualTime
		if mode == IntervalReliable {
			nextTime = successes[0]
			successes = successes[1:]
		} else {
			nextTime = failures[0]
			failures = failures[1:]
		}
		if local != nil &&
			(nextTime.Since(local.Start) >= time.Second*10 || local.PointCount >= 5 ||
				(local.Mode != IntervalIntermittent && local.Mode != mode &&
					len(intervals) > 0 && intervals[len(intervals)-1].Mode == local.Mode)) {
			intervals = append(intervals, *local)
			local = nil
		}
		if local == nil {
			local = &ReqInterval{
				Mode:       mode,
				Start:      nextTime,
				End:        nextTime,
				PointCount: 1,
			}
		} else {
			if nextTime.Before(local.End) {
				panic("misordered")
			}
			local.End = nextTime
			local.PointCount += 1
			if local.Mode != mode {
				local.Mode = IntervalIntermittent
			}
		}
	}
	if local != nil {
		intervals = append(intervals, *local)
	}
	return intervals
}

func ShouldMerge(first, second ReqInterval) bool {
	threshold := first.Duration()
	if second.Duration() > threshold {
		threshold = second.Duration()
	}
	if threshold >= time.Second*30 {
		threshold = time.Second * 30
	}
	return first.Mode == second.Mode && second.Start.Since(first.End) <= threshold
}

func Merge(first, second ReqInterval) ReqInterval {
	var mode = first.Mode
	if mode != second.Mode {
		mode = IntervalIntermittent
	}
	if first.End.After(second.Start) {
		panic("intervals misordered")
	}
	return ReqInterval{
		Mode:       mode,
		Start:      first.Start,
		End:        second.End,
		PointCount: first.PointCount + second.PointCount,
	}
}

func MergeIntervals(in []ReqInterval) (out []ReqInterval) {
	for len(in) > 0 {
		// start off another interval
		current := in[0]
		in = in[1:]
		// now repeatedly try to expand it
		for {
			if len(in) > 0 && ShouldMerge(current, in[0]) {
				current = Merge(current, in[0])
				in = in[1:]
			} else if len(out) > 0 && ShouldMerge(out[len(out)-1], current) {
				current = Merge(out[len(out)-1], current)
				out = out[:len(out)-1]
			} else {
				// no possible merges right now
				break
			}
		}
		out = append(out, current)
	}
	return out
}

func AlignIntervals(in []ReqInterval) (out []ReqInterval) {
	for _, interval := range in {
		if len(out) > 0 {
			last := out[len(out)-1]
			spacing := interval.Start.Since(last.End)
			if spacing <= last.Duration()/5 || spacing <= interval.Duration()/5 {
				last.End = interval.Start
			}
			out[len(out)-1] = last
		}
		out = append(out, interval)
	}
	return out
}

func Summarize(r *ReqScan) *ReqIntervalScan {
	intervals := PreliminaryIntervals(r.Successes, r.Failures)
	intervals = MergeIntervals(intervals)
	intervals = AlignIntervals(intervals)
	return &ReqIntervalScan{
		RequirementName: r.RequirementName,
		Intervals:       intervals,
	}
}

type ScanEntry struct {
	RequirementName string
	Timestamp       model.VirtualTime
	NewMode         IntervalMode
}

func BuildScanEntries(rs *ReqIntervalScan) (out []ScanEntry) {
	for _, in := range rs.Intervals {
		start := ScanEntry{
			RequirementName: rs.RequirementName,
			Timestamp:       in.Start,
			NewMode:         in.Mode,
		}
		for len(out) > 0 && out[len(out)-1].Timestamp >= in.Start {
			last := out[len(out)-1]
			if last.NewMode != start.NewMode && last.NewMode != "" {
				start.NewMode = IntervalIntermittent
			}
			out = out[:len(out)-1]
		}
		out = append(out, start)

		// for broken requirements, don't let the brokenness be discarded
		if in.Mode != IntervalBroken {
			out = append(out, ScanEntry{
				RequirementName: rs.RequirementName,
				Timestamp:       in.End.Add(time.Second), /* this extra second will get eliminated in most cases */
				NewMode:         "",
			})
		}
	}
	return out
}

func ComputeSystemStatus(states map[string]IntervalMode) (status IntervalMode) {
	var nReliable, nIntermittent, nBroken int
	for reqName, reqStatus := range states {
		switch reqStatus {
		case IntervalReliable:
			if reqName != "ReqNoTelemErrs" {
				nReliable += 1
			}
		case IntervalIntermittent:
			nIntermittent += 1
		case IntervalBroken:
			nBroken += 1
		default:
			panic("invalid status")
		}
	}
	switch {
	case nIntermittent == 0 && nBroken == 0:
		return IntervalReliable
	case nReliable == 0 && nIntermittent == 0:
		return IntervalBroken
	default:
		return IntervalLimited
	}
}

func CollectiveSummary(raw []*ReqIntervalScan) *ReqIntervalScan {
	var allEntries []ScanEntry
	for _, scan := range raw {
		allEntries = append(allEntries, BuildScanEntries(scan)...)
	}
	sort.Slice(allEntries, func(i, j int) bool {
		return allEntries[i].Timestamp.Before(allEntries[j].Timestamp)
	})
	states := map[string]IntervalMode{}
	var intervals []ReqInterval

	for len(allEntries) > 0 {
		nextTime := allEntries[0].Timestamp
		for len(allEntries) > 0 && allEntries[0].Timestamp == nextTime {
			next := allEntries[0]
			if next.NewMode == "" {
				delete(states, next.RequirementName)
			} else {
				states[next.RequirementName] = next.NewMode
			}
			allEntries = allEntries[1:]
		}
		status := ComputeSystemStatus(states)
		if len(intervals) == 0 || intervals[len(intervals)-1].Mode != status {
			if len(intervals) > 0 {
				intervals[len(intervals)-1].End = nextTime
			}
			intervals = append(intervals, ReqInterval{
				Mode:  status,
				Start: nextTime,
				End:   nextTime,
			})
		} else {
			intervals[len(intervals)-1].End = nextTime
		}
	}
	if len(intervals) > 0 && intervals[len(intervals)-1].Duration() == 0 {
		intervals = intervals[:len(intervals)-1]
	}

	return &ReqIntervalScan{
		RequirementName: "System Status",
		Intervals:       intervals,
	}
}

func MeanDuration(durations []time.Duration) time.Duration {
	var total time.Duration
	if len(durations) == 0 {
		return time.Duration(math.NaN())
	}
	for _, duration := range durations {
		total += duration
	}
	return total / time.Duration(len(durations))
}

func CalcStats(summary *ReqIntervalScan) {
	var failureTimes []time.Duration
	var successTimes []time.Duration

	wasFailure := false
	for i := 0; i < len(summary.Intervals)-1; i++ {
		interval := summary.Intervals[i]
		if interval.Mode == IntervalReliable {
			successTimes = append(successTimes, interval.Duration())
			wasFailure = false
		} else {
			if wasFailure {
				failureTimes[len(failureTimes)-1] += interval.Duration()
			} else {
				failureTimes = append(failureTimes, interval.Duration())
			}
			wasFailure = true
		}
	}

	log.Printf("STATS: mttf=%v, mttr=%v", MeanDuration(successTimes), MeanDuration(failureTimes))
}

func ScanReqSummary(path string, details bool) ([]*ReqIntervalScan, error) {
	raw, err := ScanRawReqs(path)
	if err != nil {
		return nil, err
	}
	var ri []*ReqIntervalScan
	for _, r := range raw {
		ri = append(ri, Summarize(r))
	}
	summary := CollectiveSummary(ri)
	CalcStats(summary)
	if !details {
		ri = nil
	}
	ri = append(ri, summary)
	return ri, nil
}
