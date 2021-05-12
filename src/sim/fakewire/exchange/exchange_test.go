package exchange

import (
	"fmt"
	"math/rand"
	"sim/component"
	"sim/fakewire/charlink"
	"sim/fakewire/fwmodel"
	"sim/model"
	"testing"
	"time"
)

// metered also means "use charlink to encode to bytes"
func InstantiateLink(ctx model.SimContext, metered bool, name string) (sink fwmodel.DataSinkFWChar, source fwmodel.DataSourceFWChar) {
	if metered {
		serialOut, serialIn := component.DataBufferBytes(ctx, 160)
		meteredIn := component.MakeMeteredSink(ctx, serialIn, 100, time.Microsecond * 100)

		fwDecodeOut, fwDecodeIn := charlink.DataBufferFWChar(ctx, 16)
		// fwDecodeIn = charlink.TeeDataSinksFW(ctx, fwDecodeIn, testpoint.MakeLoggerFW(ctx, name + ".OUT"))
		charlink.DecodeFakeWire(ctx, fwDecodeIn, serialOut)
		fwEncodeOut, fwEncodeIn := charlink.DataBufferFWChar(ctx, 16)
		charlink.EncodeFakeWire(ctx, meteredIn, fwEncodeOut)

		// fwEncodeIn = charlink.TeeDataSinksFW(ctx, fwEncodeIn, testpoint.MakeLoggerFW(ctx, name + ".IN"))

		return fwEncodeIn, fwDecodeOut
	} else {
		source, sink := charlink.DataBufferFWChar(ctx, 16)
		// sink = charlink.TeeDataSinksFW(ctx, sink, testpoint.MakeLoggerFW(ctx, name))
		return sink, source
	}
}

func InstantiateExchanges(ctx model.SimContext, metered bool) (leftSink, rightSink fwmodel.PacketSink, leftSource, rightSource fwmodel.PacketSource) {
	leftOut, rightIn := InstantiateLink(ctx, metered, "LinkL2R")
	rightOut, leftIn := InstantiateLink(ctx, metered, "LinkR2L")

	leftSink, leftSource = FakeWireExchange(ctx, leftOut, leftIn)
	rightSink, rightSource = FakeWireExchange(ctx, rightOut, rightIn)

	return leftSink, rightSink, leftSource, rightSource
}

type StepFunc func() (time.Duration, StepFunc)

func IterativeStepper(sim model.SimContext, step StepFunc, done func()) (start func()) {
	var stepper func()
	stepper = func() {
		if step == nil {
			done()
		} else {
			pause, nextStep := step()
			sim.SetTimer(sim.Now().Add(pause), "sim.fakewire.exchange.IterativeStepper/NextStep", stepper)
			step = nextStep
		}
	}
	return stepper
}

func RandPacket() []byte {
	data := make([]byte, rand.Intn(4000))
	_, _ = rand.Read(data)
	return data
}

func LogSim(sim model.SimContext, fmtStr string, args... interface{}) {
	fmt.Printf("%v %s\n", sim.Now(), fmt.Sprintf(fmtStr, args...))
}

func ValidatorSingle(sim model.SimContext, out fwmodel.PacketSink, in fwmodel.PacketSource, onError func(string), done func()) (start func()) {
	var step1, step2, step3, step4 StepFunc
	packetData := RandPacket()
	var timeout model.VirtualTime
	step1 = func() (time.Duration, StepFunc) {
		// LogSim(sim, "Performing validation at step1")
		if in.HasPacketAvailable() {
			onError("packet available before it should have been")
		}
		if rand.Intn(2) == 0 {
			timeout = sim.Now().Add(time.Second * 10)
			return 10 * time.Millisecond, step1
		} else {
			return 10 * time.Millisecond, step2
		}
	}
	step2 = func() (time.Duration, StepFunc) {
		LogSim(sim, "Transmitting packet...")
		if out.CanAcceptPacket() {
			out.SendPacket(append([]byte{}, packetData...))
			timeout = sim.Now().Add(time.Second * 10)
			return 10 * time.Millisecond, step3
		} else if sim.Now().After(timeout) {
			onError(fmt.Sprintf("Timed out while waiting to be able to send packet"))
			return 0, nil
		} else {
			return 10 * time.Millisecond, step2
		}
	}
	step3 = func() (time.Duration, StepFunc) {
		// LogSim(sim, "Performing validation at step3")
		if in.HasPacketAvailable() {
			packet := in.ReceivePacket()
			mismatches := 0
			for i := 0; i < len(packet) && i < len(packetData); i++ {
				if packet[i] != packetData[i] {
					mismatches += 1
				}
			}
			if len(packet) != len(packetData) {
				onError(fmt.Sprintf("Expected packet of length %d, but got packet of length %d", len(packetData), len(packet)))
				fmt.Printf("Wanted: %v\n", packetData)
				fmt.Printf("Actual: %v\n", packet)
			}
			if mismatches != 0 {
				onError(fmt.Sprintf("Out of packet of length %d, encountered %d mismatches", len(packetData), mismatches))
			}
			return time.Second, step4
		} else if sim.Now().After(timeout) {
			onError(fmt.Sprintf("Timed out while waiting for packet"))
			return 0, nil
		} else {
			return 10 * time.Millisecond, step3
		}
	}
	step4 = func() (time.Duration, StepFunc) {
		// LogSim(sim, "Performing validation at step4")
		if in.HasPacketAvailable() {
			onError("packet available after it already was available")
		}
		return 0, nil
	}
	return IterativeStepper(sim, step1, done)
}

func Validator(sim model.SimContext, out fwmodel.PacketSink, in fwmodel.PacketSource, count int, onError func(int, string), done func()) {
	nextAction := done
	var anyErrors bool
	for i := count - 1; i >= 0; i-- {
		i := i
		savedAction := nextAction
		nextAction = ValidatorSingle(sim, out, in, func(s string) {
			anyErrors = true
			onError(i, s)
		}, func() {
			if anyErrors {
				done()
			} else {
				savedAction()
			}
		})
	}
	sim.Later("sim.fakewire.exchange.Validator/FirstStep", nextAction)
}

func InnerTestExchanges(t *testing.T, metered bool, singleDir bool) {
	sim := component.MakeSimController()

	rand.Seed(12)

	lsink, rsink, lsource, rsource := InstantiateExchanges(sim, metered)

	var finishedL2R, finishedR2L, errors bool
	Validator(sim, lsink, rsource, 10, func(i int, s string) {
		t.Errorf("Error in left-to-right validator packet %d: %s", i, s)
		errors = true
	}, func() {
		finishedL2R = true
	})
	if !singleDir {
		Validator(sim, rsink, lsource, 10, func(i int, s string) {
			t.Errorf("Error in right-to-left validator packet %d: %s", i, s)
			errors = true
		}, func() {
			finishedR2L = true
		})
	}

	// run for ten virtual minutes
	nextTimer := sim.Advance(model.TimeZero.Add(time.Minute * 10))
	if nextTimer.TimeExists() {
		t.Errorf("Unexpectedly found that there was still another timer left after ten minutes: %v", nextTimer)
	}
	if !finishedL2R {
		t.Errorf("Did not finish left-to-right validation")
	}
	if !singleDir && !finishedR2L {
		t.Errorf("Did not finish right-to-left validation")
	}
	if finishedL2R && (finishedR2L || singleDir) && !errors {
		t.Log("Finished validation successfully!")
	}
}

func TestExchangesUnmeteredSingleDirection(t *testing.T) {
	InnerTestExchanges(t, false, true)
}

func TestExchangesUnmetered(t *testing.T) {
	InnerTestExchanges(t, false, false)
}

func TestExchangesMetered(t *testing.T) {
	InnerTestExchanges(t, true, false)
}
