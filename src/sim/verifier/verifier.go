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

const (
	MaxMagMeasTimeVariance = 500 * time.Microsecond
)

func (v *verifier) checkReq(req string, checkAt model.VirtualTime, ok func() bool) {
	completion := v.rqt.Start(req)
	v.sim.SetTimer(checkAt, "sim.verifier.Verifier/CheckReq", func() {
		completion(ok())
	})
}

func (v *verifier) checkReqProgressive(req string, firstCheck model.VirtualTime, interval time.Duration, check func() (passed, cont bool)) {
	completion := v.rqt.Start(req)
	v.sim.SetTimer(firstCheck, "sim.verifier.Verifier/CheckReq", func() {
		passed, cont := check()
		completion(passed)
		// keep trying for as long as we're told to
		if cont {
			v.checkReqProgressive(req, firstCheck.Add(interval), interval, check)
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

	if magCmd, ok := command.(transport.MagSetPwrState); ok {
		v.checkExactlyOneTelemetry(ReqMagSetPwrComplete, time.Millisecond*300, func(t transport.Telemetry) bool {
			// should be a CmdCompleted packet with the correct original CID and timestamp
			cc, ok := t.(*transport.CmdCompleted)
			return ok && cc.OriginalCommandId == command.CmdId() && cc.OriginalTimestamp == sendTimestamp.Nanoseconds()
		})
		v.checkExactlyOne(ReqMagSetPwr, time.Millisecond*200, func(e Event) bool {
			mpe, ok := e.(MagnetometerPowerEvent)
			return ok && mpe.Powered == magCmd.PowerState
		})
	}
}

func (v *verifier) OnTelemetryErrors(byteErrors int, packetErrors int) {
	v.tracker.OnTelemetryErrors(byteErrors, packetErrors)
}

func (v *verifier) OnTelemetryDownlink(telemetry transport.Telemetry, remoteTimestamp model.VirtualTime) {
	now := v.sim.Now()
	// first, validate assumption that LocalTimestamp values are NEVER the same, by the structure of the telecomm
	// simulation.
	lastTDE := v.tracker.searchLast(func(e Event) bool {
		_, ok := e.(TelemetryDownlinkEvent)
		return ok
	})
	if lastTDE != nil && lastTDE.(TelemetryDownlinkEvent).LocalTimestamp == now {
		panic("should never be any duplicate local timestamps")
	}
	// now check ReqTelemOrdered, which is about remote timestamps
	if lastTDE != nil {
		orderOk := lastTDE.(TelemetryDownlinkEvent).RemoteTimestamp.Before(remoteTimestamp)
		v.rqt.Immediate(ReqTelemOrdered, orderOk)
	}

	// actually insert current event into tracker
	v.tracker.OnTelemetryDownlink(telemetry, remoteTimestamp)

	// check ReqTelemRecent
	recentOk := now.Add(-10*time.Millisecond).Before(remoteTimestamp) && remoteTimestamp.Before(now)
	v.rqt.Immediate(ReqTelemRecent, recentOk)

	// check ReqCmdRecvExpected
	if recv, ok := telemetry.(*transport.CmdReceived); ok {
		uplinks := v.tracker.search(now.Add(-time.Millisecond*100), now, func(e Event) bool {
			cue, ok := e.(CommandUplinkEvent)
			return ok && cue.SendTimestamp.Nanoseconds() == recv.OriginalTimestamp && cue.Command.CmdId() == recv.OriginalCommandId
		})
		if len(uplinks) > 1 {
			panic("can only be at most one valid uplink, because original timestamps would not match")
		}
		v.rqt.Immediate(ReqCmdRecvExpected, len(uplinks) == 1)
	}

	// check ReqCmdCompleteExpected and ReqCmdSuccess
	if complete, ok := telemetry.(*transport.CmdCompleted); ok {
		uplinks := v.tracker.search(now.Add(-time.Millisecond*300), now, func(e Event) bool {
			cue, ok := e.(CommandUplinkEvent)
			return ok && cue.SendTimestamp.Nanoseconds() == complete.OriginalTimestamp && cue.Command.CmdId() == complete.OriginalCommandId
		})
		if len(uplinks) > 1 {
			panic("can only be at most one valid uplink, because original timestamps would not match")
		}
		v.rqt.Immediate(ReqCmdCompleteExpected, len(uplinks) == 1)

		v.rqt.Immediate(ReqCmdSuccess, complete.Success)
	}

	// check ReqOrderedMagReadings and ReqCorrectMagReadings
	if readingsArray, ok := telemetry.(*transport.MagReadingsArray); ok {
		// first, find the previous readings
		lastReadingsArray := v.tracker.searchLast(func(e Event) bool {
			tde, ok1 := e.(TelemetryDownlinkEvent)
			_, ok2 := tde.Telemetry.(*transport.MagReadingsArray)
			return ok1 && ok2 && tde.LocalTimestamp != now
		})
		inOrder := true
		lastReadingTime := model.TimeZero
		if lastReadingsArray != nil {
			lastReadings := lastReadingsArray.(TelemetryDownlinkEvent).Telemetry.(*transport.MagReadingsArray).Readings
			// guaranteed to have at least one element by telemetry decoder
			lastReadingTime, inOrder = model.FromNanoseconds(lastReadings[len(lastReadings)-1].ReadingTime)
		}

		// now validate the order
		for _, reading := range readingsArray.Readings {
			readingTime, rtOk := model.FromNanoseconds(reading.ReadingTime)
			if !rtOk || !readingTime.AtOrAfter(lastReadingTime.Add(time.Millisecond*95)) {
				inOrder = false
				break
			}
			lastReadingTime = readingTime
		}

		v.rqt.Immediate(ReqOrderedMagReadings, inOrder)

		// validate that there was enough spacing between reading downlinks
		if lastReadingsArray != nil {
			lastLocalTimestamp := lastReadingsArray.(TelemetryDownlinkEvent).LocalTimestamp
			lastRemoteTimestamp := lastReadingsArray.(TelemetryDownlinkEvent).RemoteTimestamp
			v.rqt.Immediate(ReqBatchedMagReadings,
				now.AtOrAfter(lastLocalTimestamp.Add(time.Second*5)) &&
					remoteTimestamp.AtOrAfter(lastRemoteTimestamp.Add(time.Second*5)))
		}

		if inOrder {
			// now make sure that everything matches a real measurement
			firstReadingTime := model.FromNanosecondsAssume(readingsArray.Readings[0].ReadingTime)
			samePeriodMeasurements := v.tracker.search(
				firstReadingTime.Add(-MaxMagMeasTimeVariance),
				lastReadingTime.Add(MaxMagMeasTimeVariance),
				func(e Event) bool {
					_, ok := e.(MagnetometerMeasureEvent)
					return ok
				},
			)

			matchOk := true
			if len(samePeriodMeasurements) != len(readingsArray.Readings) {
				matchOk = false
			} else {
				for i, reading := range readingsArray.Readings {
					mme := samePeriodMeasurements[i].(MagnetometerMeasureEvent)
					timeDiff := model.FromNanosecondsAssume(reading.ReadingTime).TimeDistance(mme.MeasTimestamp)
					if timeDiff >= MaxMagMeasTimeVariance || mme.X != reading.MagX || mme.Y != reading.MagY || mme.Z != reading.MagZ {
						matchOk = false
					}
				}
			}
			v.rqt.Immediate(ReqCorrectMagReadings, matchOk)
		}
	}
}

func (v *verifier) OnSetMagnetometerPower(powered bool) {
	v.tracker.OnSetMagnetometerPower(powered)

	now := v.sim.Now()
	events := v.tracker.search(now.Add(-time.Millisecond*200), now, func(e Event) bool {
		cue, ok1 := e.(CommandUplinkEvent)
		msp, ok2 := cue.Command.(transport.MagSetPwrState)
		return ok1 && ok2 && msp.PowerState == powered
	})
	changeOk := len(events) >= 1
	if changeOk {
		// make sure that not only was there a command, but that the most recent command had not already been carried out
		recencyThreshold := events[len(events)-1].(CommandUplinkEvent).ReceiveTimestamp
		previous := v.tracker.search(recencyThreshold, now, func(e Event) bool {
			mpe, ok := e.(MagnetometerPowerEvent)
			return ok && mpe.Powered == powered
		})
		changeOk = len(previous) == 0
	}
	v.rqt.Immediate(ReqUnchangedPwr, changeOk)

	if powered {
		// now... after powering up, there needs to be a measurement every tenth of a second
		lastReading := now
		v.checkReqProgressive(ReqCollectMagReadings, now.Add(time.Millisecond*105), time.Millisecond*100, func() (passed, cont bool) {
			pollAt := v.sim.Now()
			readings := v.tracker.search(pollAt.Add(-time.Millisecond*10), pollAt, func(e Event) bool {
				_, ok := e.(MagnetometerMeasureEvent)
				return ok
			})
			if len(readings) != 1 {
				depowered := v.tracker.search(lastReading, pollAt, func(e Event) bool {
					_, ok := e.(MagnetometerPowerEvent)
					return ok
				})
				if len(depowered) == 1 {
					if depowered[0].(MagnetometerPowerEvent).Powered {
						panic("should not transition powered->powered")
					}
					// passed, but no further requirement to check.
					return true, false
				} else {
					// not depowered, so should have continued to collect readings
					return false, false
				}
			}
			measuredAt := readings[0].(MagnetometerMeasureEvent).MeasTimestamp
			depowered := v.tracker.search(lastReading, measuredAt, func(e Event) bool {
				mpe, ok := e.(MagnetometerPowerEvent)
				return ok && mpe.ActionTimestamp != now
			})
			if len(depowered) > 0 {
				if depowered[0].(MagnetometerPowerEvent).Powered {
					panic("should not transition powered->powered")
				}
				// should not have depowered if measurement was going to occur afterwards
				return false, false
			}
			lastReading = measuredAt
			// reading found, so make sure to check for the next reading
			return true, true
		})
	}
}

func (v *verifier) OnMeasureMagnetometer(x, y, z int16) {
	v.tracker.OnMeasureMagnetometer(x, y, z)

	now := v.sim.Now()

	// confirm no unexpected collection... should be guaranteed by the structure of the magnetometer simulation, so
	// we check just for the sake of coherency
	lastPowerEvent := v.tracker.searchLast(func(e Event) bool {
		_, ok := e.(MagnetometerPowerEvent)
		return ok
	})
	if lastPowerEvent == nil || lastPowerEvent.(MagnetometerPowerEvent).Powered == false {
		panic("should not be able to measure magnetometer if not currently powered")
	}

	// confirm that each of these measurements gets incorporated into at least one downlink packet
	v.checkReq(ReqDownlinkMagReadings, now.Add(time.Second*10), func() bool {
		events := v.tracker.search(now, now.Add(time.Second*10), func(e Event) bool {
			tde, ok1 := e.(TelemetryDownlinkEvent)
			mra, ok2 := tde.Telemetry.(*transport.MagReadingsArray)
			if !ok1 || !ok2 {
				return false
			}
			// make sure the reading is found somewhere in here
			for _, reading := range mra.Readings {
				if reading.MagX == x && reading.MagY == y && reading.MagZ == z {
					readingTime, ok := model.FromNanoseconds(reading.ReadingTime)
					if ok && now.TimeDistance(readingTime) < MaxMagMeasTimeVariance {
						return true
					}
				}
			}
			return false
		})
		return len(events) > 0
	})
}

func (v *verifier) startPeriodicValidation(nbe, npe int) {
	completion := v.rqt.Start(ReqNoTelemErrs)
	v.sim.SetTimer(v.sim.Now().Add(time.Second), "sim.verifier.ActivityVerifier/Periodic", func() {
		completion(v.tracker.TelemetryByteErrors == nbe && v.tracker.TelemetryPacketErrors == npe)
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
		if v.rqt.Failed() || v.rqt.CountSuccesses() >= lsuccess+5 {
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
