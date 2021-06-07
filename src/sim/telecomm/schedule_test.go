package telecomm

import (
	"bytes"
	"sim/component"
	"sim/model"
	"sim/testpoint"
	"sort"
	"testing"
	"time"
)

func TestByteScheduleSimple(t *testing.T) {
	sim := component.MakeSimControllerSeeded(222)
	bsch := MakeByteSchedule(sim)

	data := make([]byte, 4000)
	sim.Rand().Read(data)
	bsch.FillBytes(sim.Now().Add(time.Millisecond * 1000), time.Millisecond, data)
	sim.Advance(sim.Now().Add(time.Millisecond * 2500))
	bsch.ClearBytes(sim.Now().Add(time.Millisecond * 500))
	sim.Advance(sim.Now().Add(time.Millisecond * 10000))
	received := bsch.ReceiveBytes(sim.Now())
	if len(received) != 2000 {
		t.Errorf("wrong number of received bytes: %d instead of %d", len(received), 2000)
	}
	if !bytes.Equal(received, data[:len(received)]) {
		t.Errorf("bytes received did not match bytes transmitted")
	}
}

type mapSched map[model.VirtualTime]byte

func (ms mapSched) LastTime() model.VirtualTime {
	latest := model.NeverTimeout
	for k := range ms {
		if !latest.TimeExists() || k.After(latest) {
			latest = k
		}
	}
	return latest
}

func TestByteScheduleComplex(t *testing.T) {
	sim := component.MakeSimControllerSeeded(444)
	bsch := MakeByteSchedule(sim)

	byteDuration := time.Microsecond * 137
	bytesByStartTime := mapSched{}

	r := sim.Rand()
	trials := 1000
	for i := 0; i < trials && !t.Failed(); i++ {
		c := r.Intn(4)
		if i == trials - 1 {
			// last trial! so fast-forward past the end...
			minTargetTime := bytesByStartTime.LastTime()
			if !minTargetTime.TimeExists() || minTargetTime.Before(sim.Now()) {
				minTargetTime = sim.Now()
			}
			sim.Advance(minTargetTime.Add(time.Second * 100))
			// and make sure we receive everything remaining
			c = 2
		}
		switch c {
		case 0: // fill
			minTargetTime := bytesByStartTime.LastTime().Add(byteDuration)
			if !minTargetTime.TimeExists() || minTargetTime.Before(sim.Now()) {
				minTargetTime = sim.Now()
			}
			actualTargetTime := minTargetTime
			if r.Intn(3) != 0 {
				actualTargetTime = actualTargetTime.Add(time.Microsecond * time.Duration(r.Intn(12345)))
			}
			byteData := testpoint.RandPacket(r)
			for i, b := range byteData {
				bytesByStartTime[actualTargetTime.Add(byteDuration * time.Duration(i))] = b
			}
			t.Logf("FillBytes(%v, %v, {%v})", actualTargetTime, byteDuration, len(byteData))
			bsch.FillBytes(actualTargetTime, byteDuration, byteData)
			if len(byteData) > 0 {
				let := bsch.LastEndTime()
				elet := actualTargetTime.Add(byteDuration * time.Duration(len(byteData)))
				if let != elet {
					t.Errorf("last end time not as expected: %v instead of %v", let, elet)
				}
			}
		case 1: // clear
			actualTargetTime := sim.Now()
			if r.Intn(3) != 0 {
				actualTargetTime = actualTargetTime.Add(time.Microsecond * time.Duration(r.Intn(12345)))
			}
			for ti := range bytesByStartTime {
				if ti.AtOrAfter(actualTargetTime) {
					delete(bytesByStartTime, ti)
				}
			}
			t.Logf("ClearBytes(%v)", actualTargetTime)
			bsch.ClearBytes(actualTargetTime)
		case 2: // receive
			actualTargetTime := sim.Now()
			if r.Intn(3) != 0 {
				actualTargetTime = actualTargetTime.Add(-time.Microsecond * time.Duration(r.Intn(12345)))
			}
			latestStartTime := actualTargetTime.Add(-byteDuration)
			type tbpair struct {
				t model.VirtualTime
				b byte
			}
			var expected []tbpair
			for t, b := range bytesByStartTime {
				if t.AtOrBefore(latestStartTime) {
					expected = append(expected, tbpair{t, b})
					delete(bytesByStartTime, t)
				}
			}
			sort.Slice(expected, func(i, j int) bool {
				return expected[i].t.Before(expected[j].t)
			})
			dataOut := bsch.ReceiveBytes(actualTargetTime)
			t.Logf("ReceiveBytes(%v) -> {%v}", actualTargetTime, len(dataOut))
			if len(dataOut) != len(expected) {
				t.Errorf("wrong length of received bytes: %d instead of %d", len(dataOut), len(expected))
			}
			var mismatches int
			for i := 0; i < len(dataOut) && i < len(expected); i++ {
				if dataOut[i] != expected[i].b {
					mismatches += 1
				}
			}
			if mismatches > 0 {
				t.Errorf("detected %d mismatches out of total (%d or %d) when validating received bytes", mismatches, len(dataOut), len(expected))
			}
		case 3: // advance
			lastTime := sim.Now()
			sim.Advance(sim.Now().Add(time.Microsecond * time.Duration(r.Intn(12345))))
			t.Logf("Advance %v ---> %v", lastTime, sim.Now())
		}
		// t.Logf("Current schedule length: %d. Current byte count: %d.", len(bsch.schedule), len(bytesByStartTime))
	}
	if len(bytesByStartTime) > 0 {
		t.Error("expected no bytes on exit")
	}
	if len(bsch.schedule) > 0 {
		t.Error("expected empty schedule on exit")
	}
}
