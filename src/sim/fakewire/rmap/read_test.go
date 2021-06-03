package rmap

import (
	"math/rand"
	"testing"
)

func RandReadPacket(r *rand.Rand) Packet {
	var length uint32
	if r.Intn(30) == 0 {
		length = r.Uint32()
	} else {
		// in most cases, provide "known-good" lengths, rather than ones that exceed the uint24 field
		length = r.Uint32() & 0xFFFFFF
	}
	return ReadPacket{
		DestinationPath:           RandPath(r),
		DestinationLogicalAddress: RandLogicalAddr(r),
		Increment:                 r.Intn(2) == 0,
		DestinationKey:            uint8(r.Uint32()),
		SourcePath:                RandPath(r),
		SourceLogicalAddress:      RandLogicalAddr(r),
		TransactionIdentifier:     uint16(r.Uint32()),
		ExtendedReadAddress:       uint8(r.Uint32()),
		ReadAddress:               r.Uint32(),
		DataLength:                length,
	}
}

func TestReadPacketEncoding(t *testing.T) {
	testPacketIterations(t, RandReadPacket, rand.New(rand.NewSource(23)))
}

func TestReadPacketCorruption(t *testing.T) {
	testPacketCorruptions(t, RandReadPacket, rand.New(rand.NewSource(29)))
}
