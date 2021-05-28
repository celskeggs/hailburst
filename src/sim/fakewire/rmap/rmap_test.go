package rmap

import (
	"bytes"
	"math/rand"
	"reflect"
	"sim/fakewire/router"
	"sim/testpoint"
	"testing"
)

func RandPath(r *rand.Rand) []byte {
	path := make([]byte, r.Intn(20))
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

func RandLogicalAddr(r *rand.Rand) uint8 {
	lowest := router.MaxPhysicalPorts + 1
	highest := 254
	return uint8(r.Intn(highest-lowest+1) + lowest)
}

func IsSourcePathOk(path []byte) bool {
	return len(path) <= 1 || path[0] != 0
}

func TestSourcePathEncoding(t *testing.T) {
	r := rand.New(rand.NewSource(5))
	const trace = false
	// run many tests for coverage
	for i := 0; i < 1000; i++ {
		// generate path
		path := RandPath(r)
		// encode path
		pathOk := IsSourcePathOk(path)
		encoded, err := EncodeSourcePath(append([]byte{}, path...))
		if err != nil {
			if pathOk {
				t.Errorf("should not have failed in this case; err = %v", err)
			}
			// can't encode this one, so skip it
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
		decoded := DecodeSourcePath(encoded)
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
}

func RandWritePacket(r *rand.Rand) WritePacket {
	return WritePacket{
		DestinationPath:           RandPath(r),
		DestinationLogicalAddress: RandLogicalAddr(r),
		VerifyData:                r.Intn(2) == 0,
		Acknowledge:               r.Intn(2) == 0,
		Increment:                 r.Intn(2) == 0,
		DestinationKey:            uint8(r.Uint32()),
		SourcePath:                RandPath(r),
		SourceLogicalAddress:      RandLogicalAddr(r),
		TransactionIdentifier:     uint16(r.Uint32()),
		ExtendedWriteAddress:      uint8(r.Uint32()),
		WriteAddress:              r.Uint32(),
		DataBytes:                 testpoint.RandPacket(r),
	}
}

func WritePacketsEqual(wp1, wp2 WritePacket) bool {
	return reflect.DeepEqual(wp1, wp2)
}

func IsWritePacketOk(wp WritePacket) bool {
	return len(wp.SourcePath) <= 12 && IsSourcePathOk(wp.SourcePath)
}

func TestWritePacketEncoding(t *testing.T) {
	r := rand.New(rand.NewSource(11))
	const trace = false
	// run many tests for coverage
	for i := 0; i < 1000; i++ {
		// generate packet
		packet := RandWritePacket(r)
		// encode
		packetOk := IsWritePacketOk(packet)
		encoding, err := packet.Encode()
		if err != nil {
			if packetOk {
				// only error when it's SUPPOSED to encode correctly
				t.Error(err)
			}
			continue
		}
		if !packetOk {
			t.Error("packet should not have been encoded properly in this case")
		}
		// strip off address bytes
		if len(encoding) <= len(packet.DestinationPath) ||
			!bytes.Equal(encoding[:len(packet.DestinationPath)], packet.DestinationPath) ||
			encoding[len(packet.DestinationPath)] != packet.DestinationLogicalAddress {
			// error if not routable
			t.Error("destination path not correctly encoded")
			continue
		}
		received := encoding[len(packet.DestinationPath):]
		// decode packet
		decoded, err := DecodePacket(received)
		if err != nil {
			t.Error(err)
			continue
		}
		// compare packets
		wpDec, ok := decoded.(WritePacket)
		if !ok {
			t.Error("not correctly decoded as a write packet")
			continue
		}
		if len(wpDec.DestinationPath) > 0 {
			t.Error("destination path should not have been available to decode")
			continue
		}
		// insert destination path from network routing, just so that they compare correctly
		wpDec.DestinationPath = encoding[:len(packet.DestinationPath)]
		if !WritePacketsEqual(packet, wpDec) {
			t.Errorf("packets did not match\nreceived: %v\nexpected: %v", wpDec, packet)
		}
	}
}
