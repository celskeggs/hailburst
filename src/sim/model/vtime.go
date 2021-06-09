package model

import (
	"fmt"
	"time"
)

type VirtualTime int64

const NanosecondsPerSecond = int64(time.Second / time.Nanosecond)

func (t VirtualTime) String() string {
	if t.TimeExists() {
		ns := int64(t)
		return fmt.Sprintf("[%ds+%09dns]", ns/NanosecondsPerSecond, ns%NanosecondsPerSecond)
	} else {
		return "[never]"
	}
}

func (t VirtualTime) TimeExists() bool {
	return t >= 0
}

func (t VirtualTime) AtOrAfter(t2 VirtualTime) bool {
	if !t.TimeExists() || !t2.TimeExists() {
		panic("times don't exist")
	}
	return t >= t2
}

func (t VirtualTime) After(t2 VirtualTime) bool {
	if !t.TimeExists() || !t2.TimeExists() {
		panic("times don't exist")
	}
	return t > t2
}

func (t VirtualTime) AtOrBefore(t2 VirtualTime) bool {
	if !t.TimeExists() || !t2.TimeExists() {
		panic("times don't exist")
	}
	return t <= t2
}

func (t VirtualTime) Before(t2 VirtualTime) bool {
	if !t.TimeExists() || !t2.TimeExists() {
		panic("times don't exist")
	}
	return t < t2
}

func (t VirtualTime) Add(duration time.Duration) VirtualTime {
	if !t.TimeExists() {
		return t
	}
	t2 := t + VirtualTime(duration.Nanoseconds())
	if (duration > 0 && t2 < t) || (duration < 0 && t2 > t) {
		panic("times wrapped around")
	}
	return t2
}

func (t VirtualTime) Since(base VirtualTime) time.Duration {
	if !t.TimeExists() || !base.TimeExists() {
		panic("times don't exist")
	}
	if base > t {
		panic("cannot compute negative duration in since; expectation is that base is AT or BEFORE t")
	}
	return time.Nanosecond * time.Duration(t - base)
}

func (t VirtualTime) Nanoseconds() uint64 {
	return uint64(t)
}

func FromNanoseconds(t uint64) (VirtualTime, bool) {
	vt := VirtualTime(t)
	return vt, vt.TimeExists()
}

const NeverTimeout VirtualTime = -1
const TimeZero VirtualTime = 0
