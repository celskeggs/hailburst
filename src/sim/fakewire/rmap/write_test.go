package rmap

import (
	"math/rand"
	"sim/testpoint"
	"testing"
)

func RandWritePacket(r *rand.Rand) Packet {
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

func TestWritePacketEncoding(t *testing.T) {
	testPacketIterations(t, RandWritePacket, rand.New(rand.NewSource(11)))
}

func TestWritePacketCorruption(t *testing.T) {
	testPacketCorruptions(t, RandWritePacket, rand.New(rand.NewSource(13)))
}
