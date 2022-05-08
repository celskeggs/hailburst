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

type ReqIndivViterbiInterval struct {
	Mode  viterbi.IndivHiddenState
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
	case viterbi.Working: // 78c679
		return color.RGBA{0x78, 0xC6, 0x79, 255}, "Working"
	case viterbi.Broken:
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

type ReqIndivViterbiIntervalScan struct {
	RequirementName string
	Intervals       []ReqIndivViterbiInterval
}

func (r *ReqIndivViterbiIntervalScan) Label() string {
	return r.RequirementName
}

func (r *ReqIndivViterbiIntervalScan) LastTime() float64 {
	latest := 0.0
	for _, interval := range r.Intervals {
		latest = math.Max(latest, interval.End.Since(model.TimeZero).Seconds())
	}
	return latest
}

func IndivViterbiRender(state viterbi.IndivHiddenState) (c color.RGBA, label string) {
	switch state {
	case viterbi.Passing:
		return color.RGBA{0x78, 0xC6, 0x79, 255}, "Passing"
	case viterbi.Degrading:
		return color.RGBA{0xA3, 0x0A, 0x0C, 255}, "Degrading"
	case viterbi.Partial:
		return color.RGBA{0xFF, 0xFF, 0xB2, 255}, "Partial"
	case viterbi.Failing:
		return color.RGBA{0xE3, 0x1A, 0x1C, 255}, "Failing"
	case viterbi.Recovering:
		return color.RGBA{0x48, 0x86, 0x49, 255}, "Recovering"
	case viterbi.Absent:
		return color.RGBA{0xFF, 0xFF, 0xFF, 255}, "Absent"
	default:
		panic("invalid mode")
	}
}

func (r *ReqIndivViterbiIntervalScan) BuildPlot(lastTime float64, location float64) *tlplot.TimelinePlot {
	var activities []tlplot.Activity

	for _, interval := range r.Intervals {
		c, label := IndivViterbiRender(interval.Mode)
		activities = append(activities, tlplot.Activity{
			Start: interval.Start.Since(model.TimeZero).Seconds(),
			End:   interval.End.Since(model.TimeZero).Seconds(),
			Color: c,
			Label: label,
		})
	}

	return tlplot.NewTimelinePlot(activities, nil, location, vg.Points(10), lastTime)
}

func MinDuration(durations []time.Duration) time.Duration {
	var total time.Duration = -1
	for _, duration := range durations {
		if duration < total || total == -1 {
			total = duration
		}
	}
	return total
}

func MaxDuration(durations []time.Duration) time.Duration {
	var total time.Duration
	for _, duration := range durations {
		if duration > total {
			total = duration
		}
	}
	return total
}

func TotalDuration(durations []time.Duration) time.Duration {
	var total time.Duration
	for _, duration := range durations {
		total += duration
	}
	return total
}

type ViterbiStats struct {
	MeanTimeToFailure   time.Duration
	MeanTimeToRecovery  time.Duration
	BestTimeToFailure   time.Duration
	BestTimeToRecovery  time.Duration
	WorstTimeToFailure  time.Duration
	WorstTimeToRecovery time.Duration
	OkPercent           float64
}

func (v ViterbiStats) Print() {
	log.Printf("STATS: mttf=%v, mttr=%v, bttf=%v, bttr=%v, wttf=%v, wttr=%v, ok=%.1f%%",
		v.MeanTimeToFailure, v.MeanTimeToRecovery, v.BestTimeToFailure, v.BestTimeToRecovery,
		v.WorstTimeToFailure, v.WorstTimeToRecovery, v.OkPercent)
}

func CalcViterbiStats(intervals []ReqViterbiInterval) ViterbiStats {
	var failureTimes []time.Duration
	var successTimes []time.Duration

	wasFailure := false
	for i := 0; i < len(intervals); i++ {
		interval := intervals[i]
		if interval.Mode == viterbi.Working {
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

	ok := 100 * float64(TotalDuration(successTimes)) / float64(TotalDuration(successTimes)+TotalDuration(failureTimes))
	return ViterbiStats{
		MeanTimeToFailure:   MeanDuration(successTimes),
		MeanTimeToRecovery:  MeanDuration(failureTimes),
		BestTimeToFailure:   MaxDuration(successTimes),
		BestTimeToRecovery:  MinDuration(failureTimes),
		WorstTimeToFailure:  MinDuration(successTimes),
		WorstTimeToRecovery: MaxDuration(failureTimes),
		OkPercent:           ok,
	}
}

func reqForeach(req *ReqScan, cb func(model.VirtualTime, bool)) {
	fi, si := 0, 0
	for fi < len(req.Failures) || si < len(req.Successes) {
		if fi == len(req.Failures) || (si < len(req.Successes) && req.Successes[si].Before(req.Failures[fi])) {
			cb(req.Successes[si], true)
			si += 1
		} else {
			cb(req.Failures[fi], false)
			fi += 1
		}
	}
}

func IndividualViterbi(req *ReqScan, lastTime model.VirtualTime) *ReqIndivViterbiIntervalScan {
	state := viterbi.IndividualRequirementState()
	//state.Debug = req.RequirementName == "ReqHeartbeat"
	var times []model.VirtualTime
	prev := spacecraft.MissionStartTime
	reqForeach(req, func(t model.VirtualTime, success bool) {
		if !t.TimeExists() {
			panic("invalid time")
		}
		if t.Before(prev.Add(time.Millisecond)) {
			t = prev.Add(time.Millisecond)
		}
		for t.After(prev.Add(ViterbiGranularity)) {
			prev = prev.Add(ViterbiGranularity)
			times = append(times, prev)
			state.NextPeriod(nil, prev)
		}
		times = append(times, t)
		prev = t
		state.NextPeriod(success, t)
	})
	for prev.Before(lastTime) {
		prev = prev.Add(ViterbiGranularity)
		times = append(times, prev)
		state.NextPeriod(nil, prev)
	}
	// compute boundaries
	boundaries := make([]model.VirtualTime, len(times)+1)
	boundaries[0] = spacecraft.MissionStartTime
	boundaries[len(times)] = lastTime.Add(time.Second)
	for i := 1; i < len(times); i++ {
		boundaries[i] = (times[i-1] + times[i]) / 2
	}
	path := state.ExtractPath()
	if len(path) != len(boundaries) {
		panic("invalid path length")
	}
	r := &ReqIndivViterbiIntervalScan{
		RequirementName: req.RequirementName,
	}
	for i := 1; i < len(path); i++ {
		cur := viterbi.IndivHiddenState(path[i])
		if len(r.Intervals) > 0 && r.Intervals[len(r.Intervals)-1].Mode == cur {
			r.Intervals[len(r.Intervals)-1].End = boundaries[i]
		} else {
			r.Intervals = append(r.Intervals, ReqIndivViterbiInterval{
				Mode:  cur,
				Start: boundaries[i-1],
				End:   boundaries[i],
			})
		}
	}
	return r
}

func SummarizeIndiv(scans []*ReqIndivViterbiIntervalScan, lastTime model.VirtualTime) (*ReqViterbiIntervalScan, ViterbiStats) {
	startTime := spacecraft.MissionStartTime
	if lastTime.AtOrBefore(startTime) {
		panic("invalid time spread")
	}
	periods := int(math.Ceil(float64((lastTime.Since(startTime)+time.Second)/ViterbiGranularity))) + 1
	buckets := make([][]viterbi.SummaryObservation, periods)
	for _, req := range scans {
		for _, interval := range req.Intervals {
			startBucket := int(interval.Start.Since(startTime) / ViterbiGranularity)
			endBucket := int(interval.End.Since(startTime) / ViterbiGranularity)
			if startBucket == endBucket {
				frac := float64(interval.End.Since(interval.Start)) / float64(ViterbiGranularity)
				if frac > 0 {
					buckets[startBucket] = append(buckets[startBucket], viterbi.SummaryObservation{
						Requirement: req.RequirementName,
						Mode:        interval.Mode,
						Duration:    frac,
					})
				}
			} else {
				startFrac := float64(startTime.Add(ViterbiGranularity*time.Duration(startBucket+1)).Since(interval.Start)) / float64(ViterbiGranularity)
				endFrac := float64(interval.End.Since(startTime.Add(ViterbiGranularity*time.Duration(endBucket)))) / float64(ViterbiGranularity)
				if startFrac > 0 {
					buckets[startBucket] = append(buckets[startBucket], viterbi.SummaryObservation{
						Requirement: req.RequirementName,
						Mode:        interval.Mode,
						Duration:    startFrac,
					})
				}
				for bucket := startBucket + 1; bucket < endBucket; bucket++ {
					buckets[bucket] = append(buckets[bucket], viterbi.SummaryObservation{
						Requirement: req.RequirementName,
						Mode:        interval.Mode,
						Duration:    1.0,
					})
				}
				if endFrac > 0 {
					buckets[endBucket] = append(buckets[endBucket], viterbi.SummaryObservation{
						Requirement: req.RequirementName,
						Mode:        interval.Mode,
						Duration:    endFrac,
					})
				}
			}
		}
	}

	state := viterbi.InitialViterbiState()
	//state.Debug = true
	for i, bucket := range buckets {
		state.NextPeriod(bucket, startTime.Add(ViterbiGranularity*time.Duration(i)))
	}
	viterbiPath := state.ExtractPath()
	if len(viterbiPath) != periods+1 {
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
			Mode:  viterbi.HiddenState(viterbiPath[startPoint]),
			Start: startTime.Add(ViterbiGranularity * time.Duration(startPoint-1)),
			End:   startTime.Add(ViterbiGranularity * time.Duration(endPoint-1)),
		})
		startPoint = endPoint
	}
	stats := CalcViterbiStats(intervals)

	return &ReqViterbiIntervalScan{
		RequirementName: "Summary Status",
		Intervals:       intervals,
	}, stats
}

func ScanReqSummaryViterbi(path string) ([]ScannedLine, ViterbiStats, error) {
	raw, err := ScanRawReqs(path)
	if err != nil {
		return nil, ViterbiStats{}, err
	}
	latest := model.TimeZero
	for _, req := range raw {
		lt := req.LastTimeVT()
		if lt.After(latest) {
			latest = lt
		}
	}
	var reqs []*ReqIndivViterbiIntervalScan
	var scansOut []ScannedLine
	for _, req := range raw {
		v := IndividualViterbi(req, latest)
		reqs = append(reqs, v)
		//scansOut = append(scansOut, v)
	}
	summary, stats := SummarizeIndiv(reqs, latest)
	scansOut = append(scansOut, summary)
	return scansOut, stats, nil
}
