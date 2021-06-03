package rmap

import (
	"math/rand"
	"testing"
)

func RandWriteReply(r *rand.Rand) Packet {
	var status uint8 = 0
	if r.Intn(3) == 2 {
		status = uint8(r.Uint32())
	}
	return WriteReply{
		SourcePath:                RandPath(r),
		SourceLogicalAddress:      RandLogicalAddr(r),
		VerifyData:                r.Intn(2) == 0,
		Increment:                 r.Intn(2) == 0,
		Status:                    status,
		DestinationLogicalAddress: RandLogicalAddr(r),
		TransactionIdentifier:     uint16(r.Uint32()),
	}
}

func TestWriteReplyEncoding(t *testing.T) {
	testPacketIterations(t, RandWriteReply, rand.New(rand.NewSource(17)))
}

func TestWriteReplyCorruption(t *testing.T) {
	testPacketCorruptions(t, RandWriteReply, rand.New(rand.NewSource(19)))
}
