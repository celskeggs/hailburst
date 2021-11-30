package model

import (
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"
)

type VirtualTime int64

const NanosecondsPerSecond = int64(time.Second / time.Nanosecond)

func (t VirtualTime) String() string {
	if t.TimeExists() {
		ns := int64(t)
		return fmt.Sprintf("%d.%09d", ns/NanosecondsPerSecond, ns%NanosecondsPerSecond)
	} else {
		return "never"
	}
}

func ParseTime(s string) (VirtualTime, error) {
	if s == "never" {
		return TimeNever, nil
	}
	parts := strings.Split(s, ".")
	if len(parts) > 2 {
		return TimeNever, fmt.Errorf("too many decimal points in string: %q", s)
	}
	sec, err := strconv.ParseUint(parts[0], 10, 64)
	if err != nil {
		return TimeNever, err
	}
	var nanosec uint64
	if len(parts) > 1 {
		if len(parts[1]) < 1 || len(parts[1]) > 9 {
			return TimeNever, fmt.Errorf("invalid decimal component: %q", s)
		}
		segment := parts[1]
		for len(segment) < 9 {
			segment += "0"
		}
		nanosec, err = strconv.ParseUint(segment, 10, 64)
		if err != nil {
			return TimeNever, err
		}
		if nanosec >= uint64(NanosecondsPerSecond) {
			panic("inconsistent; should never happen")
		}
	}
	vt, ok := FromNanoseconds(sec * uint64(NanosecondsPerSecond) + nanosec)
	if !ok {
		return TimeNever, errors.New("invalid total value")
	}
	return vt, nil
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
	return time.Nanosecond * time.Duration(t-base)
}

func abs(i64 int64) int64 {
	if i64 < 0 {
		return -i64
	} else {
		return i64
	}
}

func (t VirtualTime) TimeDistance(t2 VirtualTime) time.Duration {
	if !t.TimeExists() || !t2.TimeExists() {
		panic("times don't exist")
	}
	return time.Nanosecond * time.Duration(abs(int64(t-t2)))
}

func (t VirtualTime) Nanoseconds() uint64 {
	if !t.TimeExists() {
		panic("time doesn't exist")
	}
	return uint64(t)
}

func FromNanoseconds(t uint64) (VirtualTime, bool) {
	vt := VirtualTime(t)
	return vt, vt.TimeExists()
}

func FromNanosecondsAssume(t uint64) VirtualTime {
	vt, ok := FromNanoseconds(t)
	if !ok {
		panic("nanoseconds validity assumption failed")
	}
	return vt
}

const TimeNever VirtualTime = -1
const TimeZero VirtualTime = 0
