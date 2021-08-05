package verifier

import (
	"encoding/csv"
	"fmt"
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/model"
	"io"
	"log"
	"os"
	"strconv"
	"strings"
)

// "Receive point" refers to the moment in time when all bytes of a command have been delivered to the spacecraft radio.
// "Indicate" means to have finished downlinking all bytes in the telemetry packet containing a message.

const (
	// ReqInitClock requires:
	// No later than 5s after simulation start, the flight software shall indicate that the clock is calibrated.
	ReqInitClock = "ReqInitClock"
	// ReqReceipt requires:
	// No later than 100ms after each command's receive point, the flight software shall indicate the command’s receipt
	// exactly once.
	ReqReceipt = "ReqReceipt"
	// ReqCmdRecvExpected requires:
	// The flight software shall not indicate command receipt if a corresponding command was not uplinked to the
	// spacecraft within the last 100ms.
	ReqCmdRecvExpected = "ReqCmdRecvExpected"
	// ReqCmdCompleteExpected requires:
	// The flight software shall not indicate command completion if a corresponding command was not uplinked to the
	// spacecraft within the last 300ms.
	ReqCmdCompleteExpected = "ReqCmdCompleteExpected"
	// ReqCompletePing requires:
	// No later than 200ms after each Ping command's receive point, the flight software shall indicate the command's
	// completion.
	ReqCompletePing = "ReqCompletePing"
	// ReqPingPong requires:
	// No later than 200ms after each Ping command's receive point, the flight software shall indicate a Pong with a
	// matching PingID.
	ReqPingPong = "ReqPingPong"
	// ReqMagSetPwr requires:
	// No later than 200ms after each MagSetPwrState command's receive point, the flight software shall have updated
	// the power state of the magnetometer device to the specified state, if it differs.
	ReqMagSetPwr = "ReqMagSetPwr"
	// ReqUnchangedPwr requires:
	// The power state of the magnetometer device shall not be changed unless specifically commanded.
	ReqUnchangedPwr = "ReqUnchangedPwr"
	// ReqMagSetPwrComplete requires:
	// No later than 300ms after each MagSetPwrState command's receive point, the flight software shall indicate the
	// command's completion.
	ReqMagSetPwrComplete = "ReqMagSetPwrComplete"
	// ReqCollectMagReadings requires:
	// While the magnetometer is powered, the flight software shall collect readings from the magnetometer once every
	// 100ms, with a jitter of less than 5ms.
	ReqCollectMagReadings = "ReqCollectMagReadings"
	// ReqDownlinkMagReadings requires:
	// Every reading from the magnetometer shall be downlinked and received as part of a telemetry packet in less than
	// 10s after the collection point, with a timestamp reflecting the actual collection time to within 500us, and
	// the correct readings for each of the three axes.
	ReqDownlinkMagReadings = "ReqDownlinkMagReadings"
	// ReqOrderedMagReadings requires:
	// Magnetometer readings shall be downlinked in strictly increasing temporal order, with a minimum of 95ms between
	// subsequent measurement times.
	ReqOrderedMagReadings = "ReqOrderedMagReadings"
	// ReqCorrectMagReadings requires:
	// The flight software shall not downlink any reading that does not match one produced by the magnetometer.
	ReqCorrectMagReadings = "ReqCorrectMagReadings"
	// ReqBatchedMagReadings requires:
	// The flight software shall not downlink magnetometer readings more frequently than once every five seconds.
	ReqBatchedMagReadings = "ReqBatchedMagReadings"
	// ReqNoTelemErrs requires:
	// The flight software shall not downlink any telemetry sequences containing validation errors.
	ReqNoTelemErrs = "ReqNoTelemErrs"
	// ReqTelemOrdered requires:
	// The telemetry packets received shall be in strictly increasing temporal order. (In particular, telemetry packets
	// shall not have identical timestamps.)
	ReqTelemOrdered = "ReqTelemOrdered"
	// ReqTelemRecent requires:
	// Every downlinked telemetry packet shall have a remote timestamp within the last 15ms.
	ReqTelemRecent = "ReqTelemRecent"
	// ReqCmdSuccess requires:
	// Every command shall succeed.
	ReqCmdSuccess = "ReqCmdSuccess"
	// ReqHeartbeat requires:
	// The flight software shall downlink a heartbeat at least once every 150 milliseconds period after time clock
	// initialization occurs.
	ReqHeartbeat = "ReqHeartbeat"
)

var requirements = []string{
	ReqInitClock,
	ReqReceipt,
	ReqCmdRecvExpected,
	ReqCmdCompleteExpected,
	ReqCompletePing,
	ReqPingPong,
	ReqMagSetPwr,
	ReqUnchangedPwr,
	ReqMagSetPwrComplete,
	ReqCollectMagReadings,
	ReqDownlinkMagReadings,
	ReqOrderedMagReadings,
	ReqCorrectMagReadings,
	ReqBatchedMagReadings,
	ReqNoTelemErrs,
	ReqTelemOrdered,
	ReqTelemRecent,
	ReqCmdSuccess,
	ReqHeartbeat,
}

type ReqTracker struct {
	sim         model.SimContext
	outstanding map[string]int
	succeeded   map[string]int
	failed      map[string]int
	disp        *component.EventDispatcher
	logFile     io.Closer
	logFileCSV  *csv.Writer
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

func (rt *ReqTracker) Start(req string) (complete func(success bool)) {
	assertReq(req)
	origTime := rt.sim.Now().Nanoseconds()
	rt.log("BEGIN", strconv.FormatUint(origTime, 10), req)
	rt.outstanding[req] += 1
	var done bool
	return func(success bool) {
		if done {
			panic("cannot complete twice")
		}
		endTime := rt.sim.Now().Nanoseconds()
		rt.log("RETIRE", strconv.FormatUint(origTime, 10), strconv.FormatUint(endTime, 10), req)
		rt.outstanding[req] -= 1
		rt.Immediate(req, success)
		done = true
	}
}

func (rt *ReqTracker) Immediate(req string, success bool) {
	assertReq(req)
	if success {
		rt.log("SUCCEED", strconv.FormatUint(rt.sim.Now().Nanoseconds(), 10), req)
		rt.succeeded[req] += 1
	} else {
		rt.log("FAIL", strconv.FormatUint(rt.sim.Now().Nanoseconds(), 10), req)
		rt.failed[req] += 1
	}
	rt.disp.DispatchLater()
}

func (rt *ReqTracker) Failed() bool {
	for _, v := range rt.failed {
		if v > 0 {
			return true
		}
	}
	return false
}

func (rt *ReqTracker) CountSuccesses() (n int) {
	for _, c := range rt.succeeded {
		n += c
	}
	return n
}

func leftPad(x string, width int) string {
	if len(x) < width {
		return strings.Repeat(" ", width-len(x)) + x
	} else {
		return x
	}
}

func (rt *ReqTracker) ExplainFailure() string {
	lines := []string{"Requirements tracked:"}
	maxReqLen := 1
	for _, req := range requirements {
		if len(req) > maxReqLen {
			maxReqLen = len(req)
		}
	}
	for _, req := range requirements {
		line := fmt.Sprintf(
			"  [%s] Succeeded: %5d, Failed: %5d, Outstanding: %5d",
			leftPad(req, maxReqLen), rt.succeeded[req], rt.failed[req], rt.outstanding[req])
		lines = append(lines, line)
	}
	return strings.Join(lines, "\n")
}

func (rt *ReqTracker) log(parts ...string) {
	if rt.logFile != nil {
		err := rt.logFileCSV.Write(parts)
		if err == nil {
			rt.logFileCSV.Flush()
			err = rt.logFileCSV.Error()
		}
		if err != nil {
			log.Printf("Logging error: %v", err)
			err = rt.logFile.Close()
			if err != nil {
				log.Printf("Logfile closing error: %v", err)
			}
			rt.logFile = nil
			rt.logFileCSV = nil
		}
	}
}

func (rt *ReqTracker) LogToPath(outputPath string) {
	if rt.logFile != nil {
		panic("already set up for logging")
	}
	f, err := os.Create(outputPath)
	if err != nil {
		panic(err)
	}
	rt.logFile = f
	rt.logFileCSV = csv.NewWriter(f)
	rt.log(append([]string{"REQUIREMENTS"}, requirements...)...)
}

func MakeReqTracker(ctx model.SimContext) *ReqTracker {
	return &ReqTracker{
		sim:         ctx,
		outstanding: map[string]int{},
		succeeded:   map[string]int{},
		failed:      map[string]int{},
		disp:        component.MakeEventDispatcher(ctx, "sim.verifier.ReqTracker"),
	}
}
