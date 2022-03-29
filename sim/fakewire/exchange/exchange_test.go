package exchange

import (
	"fmt"
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/packetlink"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/testpoint"
	"log"
	"math/rand"
	"testing"
	"time"
)

// metered also means "use charlink to encode to bytes"
func InstantiateLink(ctx model.SimContext, metered bool, name string) (sink model.DataSinkBytes, source model.DataSourceBytes) {
	if metered {
		serialOut, serialIn := component.DataBufferBytes(ctx, 160)
		meteredIn := component.MakeMeteredSink(ctx, serialIn, 100, time.Microsecond*100)

		// fwDecodeIn = charlink.TeeDataSinksFW(ctx, fwDecodeIn, testpoint.MakeLoggerFW(ctx, name + ".OUT"))
		// fwEncodeIn = charlink.TeeDataSinksFW(ctx, fwEncodeIn, testpoint.MakeLoggerFW(ctx, name + ".IN"))

		return meteredIn, serialOut
	} else {
		source, sink = component.DataBufferBytes(ctx, 16)
		// sink = component.TeeDataSinks(ctx, sink, testpoint.MakeLogger(ctx, name, time.Millisecond))
		return sink, source
	}
}

func InstantiateExchanges(ctx model.SimContext, metered bool) (leftSink, rightSink fwmodel.PacketSink, leftSource, rightSource fwmodel.PacketSource) {
	leftOut, rightIn := InstantiateLink(ctx, metered, "LinkL2R")
	rightOut, leftIn := InstantiateLink(ctx, metered, "LinkR2L")

	leftRecv := packetlink.MakePacketNode(ctx)
	leftSend := packetlink.MakePacketNode(ctx)
	rightRecv := packetlink.MakePacketNode(ctx)
	rightSend := packetlink.MakePacketNode(ctx)
	log.Printf("PACKET NODES: %p %p %p %p", leftRecv, leftSend, rightRecv, rightSend)
	FakeWire(ctx, leftOut, leftIn, leftRecv.Sink(), leftSend.Source(), "   Left")
	FakeWire(ctx, rightOut, rightIn, rightRecv.Sink(), rightSend.Source(), "  Right")

	return leftSend.Sink(), rightSend.Sink(), leftRecv.Source(), rightRecv.Source()
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

func LogSim(sim model.SimContext, fmtStr string, args ...interface{}) {
	log.Printf("%v [HARNESS] %s", sim.Now(), fmt.Sprintf(fmtStr, args...))
}

func ValidatorSingle(sim model.SimContext, out fwmodel.PacketSink, in fwmodel.PacketSource, label string, onError func(string), done func()) (start func()) {
	var step1, step2, step3, step4 StepFunc
	packetData := testpoint.RandPacket(sim.Rand())
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
		LogSim(sim, "[%s] Transmitting packet...", label)
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
			mismatches, lengthOk := testpoint.ComparePackets(packet, packetData)
			if !lengthOk {
				onError(fmt.Sprintf("Expected packet of length %d, but got packet of length %d", len(packetData), len(packet)))
				fmt.Printf("Wanted: %v\n", packetData)
				fmt.Printf("Actual: %v\n", packet)
			}
			if mismatches != 0 {
				onError(fmt.Sprintf("Out of packet of length %d, encountered %d mismatches", len(packetData), mismatches))
			}
			return time.Second, step4
		} else if sim.Now().After(timeout) {
			onError(fmt.Sprintf("Timed out while waiting to receive packet"))
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

func Validator(sim model.SimContext, out fwmodel.PacketSink, in fwmodel.PacketSource, count int, label string, onError func(int, string), done func()) {
	nextAction := done
	var anyErrors bool
	for i := count - 1; i >= 0; i-- {
		i := i
		savedAction := nextAction
		nextAction = ValidatorSingle(
			sim, out, in, label,
			func(s string) {
				anyErrors = true
				onError(i, s)
			},
			func() {
				if anyErrors {
					done()
				} else {
					savedAction()
				}
			},
		)
	}
	sim.Later("sim.fakewire.exchange.Validator/FirstStep", nextAction)
}

func InnerTestExchanges(t *testing.T, metered bool, count int, singleDir bool, duration time.Duration) {
	sim := component.MakeSimControllerSeeded(12, model.TimeZero)

	lsink, rsink, lsource, rsource := InstantiateExchanges(sim, metered)

	var finishedL2R, finishedR2L, errors bool
	Validator(sim, lsink, rsource, count, "left-to-right", func(i int, s string) {
		t.Errorf("Error in left-to-right validator packet %d: %s", i, s)
		errors = true
	}, func() {
		finishedL2R = true
	})
	if !singleDir {
		Validator(sim, rsink, lsource, count, "right-to-left", func(i int, s string) {
			t.Errorf("Error in right-to-left validator packet %d: %s", i, s)
			errors = true
		}, func() {
			finishedR2L = true
		})
	}

	// run for test duration
	_ = sim.Advance(model.TimeZero.Add(duration))
	// NOTE: we can't check for whether a timer is still running,
	// because we DO expect regular transmissions of FCTs and KATs,
	// so there will definitely be a timer still running.
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

func TestExchangesUnmeteredSingleDirectionShort(t *testing.T) {
	InnerTestExchanges(t, false, 1, true, time.Second*10)
}

func TestExchangesUnmeteredSingleDirection(t *testing.T) {
	InnerTestExchanges(t, false, 10, true, time.Second*30)
}

func TestExchangesUnmetered(t *testing.T) {
	InnerTestExchanges(t, false, 10, false, time.Second*30)
}

func TestExchangesMetered(t *testing.T) {
	InnerTestExchanges(t, true, 10, false, time.Minute)
}

func TestExchangesLong(t *testing.T) {
	InnerTestExchanges(t, true, 1000, false, time.Second*2000)
}
