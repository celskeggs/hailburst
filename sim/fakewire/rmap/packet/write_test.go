package packet

import (
	"github.com/celskeggs/hailburst/sim/testpoint"
	"math/rand"
	"testing"
)

func RandWritePacket(r *rand.Rand) Packet {
	packetData := testpoint.RandPacket(r)
	return WritePacket{
		DestinationPath:           RandPath(r),
		DestinationLogicalAddress: RandLogicalAddr(r),
		OptVerifyData:             r.Intn(2) == 0,
		OptAcknowledge:            r.Intn(2) == 0,
		OptIncrement:              r.Intn(2) == 0,
		DestinationKey:            uint8(r.Uint32()),
		SourcePath:                RandPath(r),
		SourceLogicalAddress:      RandLogicalAddr(r),
		TransactionIdentifier:     uint16(r.Uint32()),
		ExtendedWriteAddress:      uint8(r.Uint32()),
		WriteAddress:              r.Uint32(),
		DataBytes:                 packetData,
		DataCRC:                   RmapCrc8(packetData),
	}
}

func TestWritePacketEncoding(t *testing.T) {
	testPacketIterations(t, RandWritePacket, rand.New(rand.NewSource(11)))
}

func TestWritePacketCorruption(t *testing.T) {
	testPacketCorruptions(t, RandWritePacket, rand.New(rand.NewSource(13)))
}
