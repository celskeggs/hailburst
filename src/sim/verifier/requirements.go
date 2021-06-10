package verifier

import (
	"fmt"
	"sim/component"
	"sim/model"
	"strings"
)

// "Receive point" refers to the moment in time when all bytes of a command have been delivered to the spacecraft radio.
// "Indicate" means to have finished downlinking all bytes in the telemetry packet containing a message.

const (
	// ReqReceipt indicates:
	// No later than 100ms after each command's receive point, the flight software shall indicate the command’s receipt.
	ReqReceipt = "ReqReceipt"
	// ReqCompletePing indicates:
	// No later than 200ms after the Ping command's receive point, the flight software shall indicate the command's
	// completion.
	ReqCompletePing = "ReqCompletePing"
	// ReqPingPong indicates:
	// No later than 200ms after the Ping command's receive point, the flight software shall indicate a Pong with a
	// matching PingID.
	ReqPingPong = "ReqPingPong"
)

var requirements = []string{ReqReceipt, ReqCompletePing, ReqPingPong}

type ReqTracker struct {
	outstanding map[string]int
	succeeded   map[string]int
	failed      map[string]int
	disp        *component.EventDispatcher
}

func (rt *ReqTracker) Subscribe(callback func()) (cancel func()) {
	return rt.disp.Subscribe(callback)
}

func assertReq(req string) {
	for _, check := range requirements {
		if check == req {
			return
		}
	}
	panic("not a valid requirement: " + req)
}

func (rt *ReqTracker) Start(req string) (succeed, fail func()) {
	assertReq(req)
	rt.outstanding[req] += 1
	var done bool
	return func() {
			if done {
				panic("cannot complete twice")
			}
			rt.outstanding[req] -= 1
			rt.succeeded[req] += 1
			rt.disp.DispatchLater()
			done = true
		}, func() {
			if done {
				panic("cannot complete twice")
			}
			rt.outstanding[req] -= 1
			rt.failed[req] += 1
			rt.disp.DispatchLater()
			done = true
		}
}

func (rt *ReqTracker) Failed() bool {
	for _, v := range rt.failed {
		if v > 0 {
			return true
		}
	}
	return false
}

func (rt *ReqTracker) ExplainFailure() string {
	lines := []string{"Requirements tracked:"}
	for _, req := range requirements {
		line := fmt.Sprintf(
			"  [%16s] Succeeded: %5d, Failed: %5d, Outstanding: %5d",
			req, rt.succeeded[req], rt.failed[req], rt.outstanding[req])
		lines = append(lines, line)
	}
	return strings.Join(lines, "\n")
}

func MakeReqTracker(ctx model.SimContext) *ReqTracker {
	return &ReqTracker{
		outstanding: map[string]int{},
		succeeded:   map[string]int{},
		failed:      map[string]int{},
		disp: component.MakeEventDispatcher(ctx, "sim.verifier.ReqTracker"),
	}
}
