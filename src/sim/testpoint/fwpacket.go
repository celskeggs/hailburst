package testpoint

import (
	"math/rand"
	"testing"
)

func RandPacket(r *rand.Rand) []byte {
	data := make([]byte, r.Intn(4000))
	_, _ = r.Read(data)
	return data
}

func ComparePackets(actual []byte, expected []byte) (mismatches int, lengthOk bool) {
	for i := 0; i < len(actual) && i < len(expected); i++ {
		if actual[i] != expected[i] {
			mismatches += 1
		}
	}
	return mismatches, len(actual) == len(expected)
}

func AssertPacketsMatch(t *testing.T, actual []byte, expected []byte) {
	mismatches, lengthOk := ComparePackets(actual, expected)
	if !lengthOk {
		t.Errorf("Packets did not match: expected len=%d, found len=%d", len(expected), len(actual))
	}
	if mismatches != 0 {
		t.Errorf("Packets did not match: %d bytes (out of %d) were mismatched", mismatches, len(expected))
	}
}
