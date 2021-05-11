package util

import (
	"math/rand"
	"testing"
)

func TestBitConversion(t *testing.T) {
	data := make([]byte, 1024)
	rand.Seed(345)
	_, _ = rand.Read(data)

	t.Logf("data to convert: %q", string(data))

	bits := BytesToBits(data)
	back := BitsToBytes(bits)

	if len(back) != len(data) {
		t.Error("incorrect length")
	}
	for i := 0; i < len(data) && i < len(back); i++ {
		if data[i] != back[i] {
			t.Error("mismatched data values")
		}
	}
}
