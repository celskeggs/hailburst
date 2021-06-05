package packet

import (
	"bytes"
	"math/rand"
	"reflect"
	"sim/fakewire/router"
	"testing"
)

func RandPathInternal(r *rand.Rand) []byte {
	path := make([]byte, r.Intn(21))
	for j := 0; j < len(path); j++ {
		switch r.Intn(4) {
		case 0: // zeros should be common, because they're the biggest challenge
			path[j] = 0
		default: // small numbers (physical paths) should be the most common outputs
			path[j] = uint8(r.Intn(router.MaxPhysicalPorts + 1))
		case 1: // larger numbers should also be tested
			path[j] = uint8(r.Intn(256))
		}
	}
	return path
}

func RandPath(r *rand.Rand) []byte {
	expectedOk := r.Intn(15) > 0
	for {
		path := RandPathInternal(r)
		pathOk := isSourcePathValid(path) && len(path) <= 12
		if pathOk == expectedOk {
			return path
		}
	}
}

func RandLogicalAddr(r *rand.Rand) uint8 {
	lowest := router.MaxPhysicalPorts + 1
	highest := 254
	return uint8(r.Intn(highest-lowest+1) + lowest)
}

func TestSourcePathEncoding(t *testing.T) {
	r := rand.New(rand.NewSource(5))
	const trace = false
	// run many tests for coverage
	trials := 1000
	countFails := 0
	for i := 0; i < trials; i++ {
		// generate path
		path := RandPath(r)
		// encode path
		pathOk := isSourcePathValid(path)
		encoded, err := encodeSourcePath(append([]byte{}, path...))
		if err != nil {
			if pathOk {
				t.Errorf("should not have failed in this case; err = %v", err)
			}
			// can't encode this one, so skip it
			countFails++
			continue
		}
		if !pathOk {
			t.Errorf("expected failure to encode in this case: %v", path)
		}
		if len(encoded)%4 != 0 {
			t.Error("invalid length")
		}
		if trace {
			t.Logf("path -> encoded: %v -> %v", path, encoded)
		}
		// decode path
		decoded := decodeSourcePath(encoded)
		// compare paths
		mismatched := len(path) != len(decoded)
		if !mismatched {
			for j := 0; j < len(path) && j < len(decoded); j++ {
				if path[j] != decoded[j] {
					mismatched = true
					break
				}
			}
		}
		if mismatched {
			t.Errorf("path mismatch: %v instead of %v", decoded, path)
		}
	}
	if countFails > 100 {
		t.Errorf("too many failures: %d of total %d", countFails, trials)
	}
}

// helpers for testing packet codecs

func testValidPacketCodec(packet Packet, t *testing.T) {
	// encode
	encoding, err := packet.Encode()
	if err != nil {
		t.Error(err)
		return
	}
	// strip off address bytes
	routing, npath := packet.PathBytes()
	if len(encoding) < len(routing) || !bytes.Equal(encoding[:len(routing)], routing) {
		// error if not routable
		t.Error("destination route not correctly encoded")
		return
	}
	received := encoding[npath:]
	// decode packet
	decoded, err := DecodePacket(received)
	if err != nil {
		t.Error(err)
		return
	}
	// insert destination path from network routing, just so that they compare correctly
	decoded, err = decoded.MergePath(encoding[:npath])
	if err != nil {
		// error if there was incorrectly already a path present
		t.Error(err)
		return
	}
	// compare packets using DeepEqual, which checks types and compares all fields
	if !reflect.DeepEqual(packet, decoded) {
		t.Errorf("packets did not match\nreceived: %v\nexpected: %v", decoded, packet)
	}
}

func testPacketIterations(t *testing.T, generator func(*rand.Rand) Packet, r *rand.Rand) {
	// run many tests for coverage
	trials := 1000
	countFails := 0
	for i := 0; i < trials; i++ {
		packet := generator(r)
		if packet.IsValid() {
			testValidPacketCodec(packet, t)
		} else {
			countFails++
			if _, err := packet.Encode(); err == nil {
				t.Error("packet encoding should have encountered an error, but didn't")
			}
		}
	}
	if countFails > 100 {
		t.Errorf("too many failures: %d of total %d", countFails, trials)
	}
}

func testPacketCorruptions(t *testing.T, generator func(*rand.Rand) Packet, r *rand.Rand) {
	// run many tests for coverage
	for i := 0; i < 10000; i++ {
		packet := generator(r)
		if packet.IsValid() {
			// encode
			encoding, err := packet.Encode()
			if err != nil {
				t.Error(err)
				continue
			}
			_, npath := packet.PathBytes()
			delivered := encoding[npath:]
			// make sure baseline decodes correctly
			decoded, err := DecodePacket(append([]byte{}, delivered...))
			if err != nil {
				t.Error(err)
				continue
			}
			if !decoded.VerifyData() {
				t.Error("expected data to be verifiable")
				continue
			}
			// corrupt packet
			delivered[r.Intn(len(delivered))] ^= 1 << r.Intn(8)
			// decode packet
			decodedCorrupt, err := DecodePacket(delivered)
			if err == nil && decodedCorrupt.VerifyData() {
				t.Error("should have encountered an error while decoding this corrupted packet")
				continue
			}
		}
	}
}
