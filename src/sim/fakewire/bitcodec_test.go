package fakewire

import (
	"math/rand"
	"testing"
)

func TestBitCodec(t *testing.T) {
	inputData := make([]byte, 1024)

	// deterministically generate some sample test vectors
	rand.Seed(123)
	if _, err := rand.Read(inputData); err != nil {
		t.Error(err)
	}

	t.Logf("Raw data: %q", string(inputData))

	// stick an encoder and decoder end-to-end
	chIn := make(chan uint8)
	chMid := make(chan bool)
	chOut := make(chan uint8)

	go encodeToBits(chMid, chIn)
	go decodeFromBits(chOut, chMid)

	// feed in data
	go func() {
		for _, b := range inputData {
			chIn <- b
		}
		close(chIn)
	}()

	// read out the data and confirm that it's correct
	i := 0
	for b := range chOut {
		if i >= len(inputData) || inputData[i] != b {
			t.Errorf("Mismatch on byte %d", i)
		}
		i += 1
	}
	if i != len(inputData) {
		t.Errorf("Expected length %d in output, but actually length %d", len(inputData), i)
	}
}
