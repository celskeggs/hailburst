package fakewire

import (
	"math/rand"
	"sim/component"
	"sim/fakewire/fwmodel"
	"sim/model"
	"sim/testpoint"
	"testing"
	"time"
)

func randFWChar() fwmodel.FWChar {
	// send a control character about once every fifteen data characters, at random
	if rand.Intn(15) == 0 {
		switch rand.Intn(4) {
		case 0:
			return fwmodel.CtrlFCT
		case 1:
			return fwmodel.CtrlEOP
		case 2:
			return fwmodel.CtrlEEP
		case 3:
			return fwmodel.CtrlESC
		default:
			panic("impossible result")
		}
	} else {
		return fwmodel.DataChar(uint8(rand.Uint32()))
	}
}

func TestFakeWireCodecs(t *testing.T) {
	sim := component.MakeSimController()

	inputData := make([]fwmodel.FWChar, 1024)

	// deterministically generate some sample test vectors
	rand.Seed(456789)
	for i := 0; i < len(inputData); i++ {
		inputData[i] = randFWChar()
	}

	t.Logf("Raw input: %v", inputData)

	inputDataCopy := append([]fwmodel.FWChar{}, inputData...)
	testSource := testpoint.MakeDataSourceFW(sim, inputDataCopy)
	testSink := testpoint.MakeDataSinkFW(sim)
	midSource, midSink := component.DataBufferBytes(sim, 100)
	midCopy := testpoint.MakeDataSink(sim)

	EncodeFakeWire(sim, component.TeeDataSinks(sim, midSink, midCopy), testSource)
	DecodeFakeWire(sim, testSink, midSource)

	sim.Advance(model.TimeZero.Add(time.Second))

	if !testSource.IsConsumed() {
		t.Fatal("expected test source to be completely consumed")
	}
	received := testSink.Collected

	t.Logf("Raw output: %v", received)
	// t.Logf("Raw bytes: %s", util.StringBits(midCopy.Take()))

	// we might not have received the last one or two characters yet, depending on exact bitpacking
	if len(received) < len(inputData) - 2 || len(received) > len(inputData) {
		t.Errorf("received %d bytes instead of expected %d", len(received), len(inputData))
	}
	for i := 0; i < len(received) && i < len(inputData); i++ {
		if inputData[i] != received[i] {
			t.Errorf("mismatch on fwchar %d: %v instead of %v", i, received[i], inputData[i])
		}
	}
	if t.Failed() {
		t.FailNow()
	}

	testSource.Refill([]fwmodel.FWChar{fwmodel.ParityFail})
	sim.Advance(model.TimeZero.Add(time.Second))

	// t.Logf("Raw bytes: %s", util.StringBits(midCopy.Take()))

	if !testSource.IsConsumed() {
		t.Fatal("expected test source to be completely consumed")
	}
	received = testSink.Collected
	if len(received) != len(inputData) + 1 {
		t.Errorf("received %d bytes instead of expected %d", len(received), len(inputData) + 1)
	} else if received[len(received) - 1] != fwmodel.ParityFail {
		t.Error("expected parity failure as very final character")
	}
	for i := 0; i < len(received) && i < len(inputData); i++ {
		if inputData[i] != received[i] {
			t.Errorf("mismatch on fwchar %d: %v instead of %v", i, received[i], inputData[i])
		}
	}
}
