package fakewire

import (
	"math/rand"
	"testing"
)

func randFWChar() FWChar {
	// send a control character about once every fifteen data characters, at random
	if rand.Intn(15) == 0 {
		switch rand.Intn(4) {
		case 0: return CtrlFCT
		case 1: return CtrlEOP
		case 2: return CtrlEEP
		case 3: return CtrlESC
		default: panic("impossible result")
		}
	} else {
		return DataChar(uint8(rand.Uint32()))
	}
}

func TestFakeWireCodecs(t *testing.T) {
	inputData := make([]FWChar, 1024)

	// deterministically generate some sample test vectors
	rand.Seed(456)
	for i := 0; i < len(inputData); i++ {
		inputData[i] = randFWChar()
	}

	t.Logf("Raw data: %v", inputData)

	// stick an encoder and decoder end-to-end
	chIn := make(chan FWChar)
	chMid := make(chan uint8)
	chOut := make(chan FWChar)

	go EncodeFakeWire(chMid, chIn)
	go DecodeFakeWire(chOut, chMid)

	// feed in data
	go func() {
		for _, ch := range inputData {
			chIn <- ch
		}
		close(chIn)
	}()

	// read out the data and confirm that it's correct
	i := 0
	for b := range chOut {
		if i >= len(inputData) || inputData[i] != b {
			t.Errorf("Mismatch on character %d", i)
		}
		i += 1
	}
	// we should expect one or two characters to be lost to parity errors, right at the end
	if i < len(inputData) - 2 || i > len(inputData) {
		t.Errorf("Expected length %d (optionally minus one or two) in output, but actually length %d", len(inputData), i)
	}
}
