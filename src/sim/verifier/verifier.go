package verifier

import (
	"log"
	"sim/model"
	"sim/telecomm/transport"
	"sim/verifier/collector"
	"time"
)

type verifier struct {
	sim     model.SimContext
	tracker tracker
	rqt     *ReqTracker
}

func (v *verifier) checkReq(req string, checkAt model.VirtualTime, ok func() bool) {
	success, failure := v.rqt.Start(req)
	v.sim.SetTimer(checkAt, "sim.verifier.Verifier/CheckReq", func() {
		if ok() {
			success()
		} else {
			failure()
		}
	})
}

func (v *verifier) checkExactlyOne(req string, interval time.Duration, predicate func(e Event) bool) {
	start := v.sim.Now()
	end := start.Add(interval)
	v.checkReq(req, end, func() bool {
		found := v.tracker.search(start, end, predicate)
		return len(found) == 1
	})
}

func (v *verifier) checkExactlyOneTelemetry(req string, interval time.Duration, predicate func(t transport.Telemetry) bool) {
	start := v.sim.Now()
	end := start.Add(interval)
	v.checkExactlyOne(req, interval, func(e Event) bool {
		tde, ok := e.(TelemetryDownlinkEvent)
		return ok && tde.RemoteTimestamp.AtOrAfter(start) && tde.RemoteTimestamp.Before(end) && predicate(tde.Telemetry)
	})
}

func (v *verifier) OnCommandUplink(command transport.Command, sendTimestamp model.VirtualTime) {
	v.tracker.OnCommandUplink(command, sendTimestamp)

	// we should receive telemetry of receipt within 100 milliseconds
	v.checkExactlyOneTelemetry(ReqReceipt, time.Millisecond*100, func(t transport.Telemetry) bool {
		// should be a CmdReceived packet with the correct original CID and timestamp
		cr, ok := t.(*transport.CmdReceived)
		return ok && cr.OriginalCommandId == command.CmdId() && cr.OriginalTimestamp == sendTimestamp.Nanoseconds()
	})

	if pingCmd, ok := command.(transport.Ping); ok {
		v.checkExactlyOneTelemetry(ReqCompletePing, time.Millisecond*200, func(t transport.Telemetry) bool {
			// should be a CmdCompleted packet with the correct original CID and timestamp
			cc, ok := t.(*transport.CmdCompleted)
			return ok && cc.OriginalCommandId == command.CmdId() && cc.OriginalTimestamp == sendTimestamp.Nanoseconds()
		})
		v.checkExactlyOneTelemetry(ReqPingPong, time.Millisecond*200, func(t transport.Telemetry) bool {
			// should be a CmdReceived packet with the correct original CID and timestamp
			pongTlm, ok := t.(*transport.Pong)
			return ok && pongTlm.PingID == pingCmd.PingID
		})
	}

	// TODO: need to implement several additional requirements in this file
}

func (v *verifier) OnTelemetryErrors(byteErrors int, packetErrors int) {
	v.tracker.OnTelemetryErrors(byteErrors, packetErrors)
}

func (v *verifier) OnTelemetryDownlink(telemetry transport.Telemetry, remoteTimestamp model.VirtualTime) {
	v.tracker.OnTelemetryDownlink(telemetry, remoteTimestamp)
}

func (v *verifier) OnSetMagnetometerPower(powered bool) {
	v.tracker.OnSetMagnetometerPower(powered)
}

func (v *verifier) OnMeasureMagnetometer(x, y, z int16) {
	v.tracker.OnMeasureMagnetometer(x, y, z)
}

func (v *verifier) startPeriodicValidation(nbe, npe int) {
	succeed, fail := v.rqt.Start(ReqNoTelemErrs)
	v.sim.SetTimer(v.sim.Now().Add(time.Second), "sim.verifier.ActivityVerifier/Periodic", func() {
		if v.tracker.TelemetryByteErrors > nbe || v.tracker.TelemetryPacketErrors > npe {
			fail()
		} else {
			succeed()
		}
		v.startPeriodicValidation(v.tracker.TelemetryByteErrors, v.tracker.TelemetryPacketErrors)
	})
}

func MakeActivityVerifier(sim model.SimContext, onFailure func(explanation string)) collector.ActivityCollector {
	v := &verifier{
		sim: sim,
		tracker: tracker{
			sim: sim,
		},
		rqt: MakeReqTracker(sim),
	}
	hasReportedFailure := false
	var prevExplanation string
	var lsuccess int
	v.rqt.Subscribe(func() {
		if v.rqt.Failed() || v.rqt.CountSuccesses() >= lsuccess + 5 {
			explanation := v.rqt.ExplainFailure()
			if v.rqt.Failed() && !hasReportedFailure {
				hasReportedFailure = true
				onFailure(explanation)
			}
			if explanation != prevExplanation {
				if v.rqt.Failed() {
					log.Printf("[%v] Hit requirement failure condition:\n%s", sim.Now(), explanation)
				} else {
					log.Printf("[%v] Requirement success report:\n%s", sim.Now(), explanation)
				}
				prevExplanation = explanation
				lsuccess = v.rqt.CountSuccesses()
			}
		}
	})
	v.startPeriodicValidation(0, 0)
	return v
}
