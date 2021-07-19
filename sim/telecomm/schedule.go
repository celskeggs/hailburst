package telecomm

import (
	"github.com/celskeggs/hailburst/sim/model"
	"sort"
	"time"
)

type ScheduleItem struct {
	StartTime model.VirtualTime
	EndTime   model.VirtualTime
	Byte      byte
}

// ByteSchedule is a data structure reflecting an unlimited sequence of timestamped bytes.
// Each data item can be considered a 3-tuple of (start-time, end-time, byte-value).
// This sequence is *predicted*... that means that each data item does not become finalized until sim.Now() is strictly
// greater than the data item's start time.
//
// Contract:
//  1. New data items can only be added with start-times no earlier than sim.Now()
//  2. Data items with start-times no earlier than sim.Now() may be deleted.
//  3. Data items may never overlap, though end-time[n] = start-time[n+1] does not qualify as an overlap.
//  4. Sequence changes (deletions, appends) may only occur on suffixes of the unlimited sequence. Changes may not be
//     made in the middle.
//  5. The recipient, however, may excise prefixes with end times no later than sim.Now(), as an optimization.
type ByteSchedule struct {
	ctx model.SimContext

	schedule []ScheduleItem
}

func MakeByteSchedule(ctx model.SimContext) *ByteSchedule {
	return &ByteSchedule{
		ctx:      ctx,
		schedule: nil,
	}
}

// FillBytes appends a suffix of additional bytes.
// Each byte is translated to a data item: (startTime + byteDuration * i, startTime + byteDuration * (i+1), byte[i])
// startTime must be no earlier than sim.Now().
func (cs *ByteSchedule) FillBytes(startTime model.VirtualTime, byteDuration time.Duration, bytes []byte) {
	if startTime < cs.ctx.Now() {
		panic("cannot FillBytes at time before Now")
	}
	if len(cs.schedule) > 0 && startTime < cs.schedule[len(cs.schedule)-1].EndTime {
		panic("cannot FillBytes when overlapping with existing items")
	}
	if byteDuration <= 0 {
		panic("byte duration must be positive")
	}
	for i, b := range bytes {
		cs.schedule = append(cs.schedule, ScheduleItem{
			StartTime: startTime.Add(byteDuration * time.Duration(i)),
			EndTime:   startTime.Add(byteDuration * time.Duration(i+1)),
			Byte:      b,
		})
	}
}

// ClearBytes deletes a suffix of existing bytes. Any data items with start-times equal to or later than startTime
// will be removed from the sequence.
// startTime must be no earlier than sim.Now().
func (cs *ByteSchedule) ClearBytes(startTime model.VirtualTime) {
	if startTime < cs.ctx.Now() {
		panic("cannot ClearBytes at time before Now")
	}
	firstIdx := sort.Search(len(cs.schedule), func(i int) bool {
		return cs.schedule[i].StartTime >= startTime
	})
	cs.schedule = cs.schedule[:firstIdx]
}

// ReceiveBytes extracts the prefix of existing bytes that are finalized and returns them as a byte array.
func (cs *ByteSchedule) ReceiveBytes(endTime model.VirtualTime) []byte {
	if endTime > cs.ctx.Now() {
		panic("cannot ReceiveBytes at time after Now")
	}
	firstIdx := sort.Search(len(cs.schedule), func(i int) bool {
		return cs.schedule[i].EndTime > endTime
	})
	// so all data items BEFORE firstIdx have EndTime <= endTime <= Now()
	output := make([]byte, firstIdx)
	for i := 0; i < firstIdx; i++ {
		output[i] = cs.schedule[i].Byte
	}
	cs.schedule = cs.schedule[firstIdx:]
	return output
}

// LastEndTime determines the last transmission time of the final byte scheduled, or the current time, whichever is
// later.
func (cs *ByteSchedule) LastEndTime() model.VirtualTime {
	if len(cs.schedule) == 0 {
		return cs.ctx.Now()
	} else {
		last := cs.schedule[len(cs.schedule)-1]
		if last.EndTime.After(cs.ctx.Now()) {
			return last.EndTime
		} else {
			return cs.ctx.Now()
		}
	}
}

// PeekAllBytes returns the complete sequence of bytes, including predicted bytes.
func (cs *ByteSchedule) PeekAllBytes() (out []byte) {
	output := make([]byte, len(cs.schedule))
	for i, di := range cs.schedule {
		output[i] = di.Byte
	}
	return output
}

// LookupEndTime determines the end time of the nth byte returned by PeekAllBytes()
func (cs *ByteSchedule) LookupEndTime(nth int) model.VirtualTime {
	return cs.schedule[nth].EndTime
}
