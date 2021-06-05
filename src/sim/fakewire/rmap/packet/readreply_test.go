package packet

import (
	"math/rand"
	"sim/testpoint"
	"testing"
)

func RandReadReply(r *rand.Rand) Packet {
	var status uint8 = 0
	if r.Intn(3) == 2 {
		status = uint8(r.Uint32())
	}
	return ReadReply{
		SourcePath:                RandPath(r),
		SourceLogicalAddress:      RandLogicalAddr(r),
		OptIncrement:              r.Intn(2) == 0,
		Status:                    status,
		DestinationLogicalAddress: RandLogicalAddr(r),
		TransactionIdentifier:     uint16(r.Uint32()),
		DataBytes:                 testpoint.RandPacket(r),
	}
}

func TestReadReplyEncoding(t *testing.T) {
	testPacketIterations(t, RandReadReply, rand.New(rand.NewSource(17)))
}

func TestReadReplyCorruption(t *testing.T) {
	testPacketCorruptions(t, RandReadReply, rand.New(rand.NewSource(19)))
}
