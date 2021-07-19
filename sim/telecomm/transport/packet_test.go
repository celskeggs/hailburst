package transport

import (
	"bytes"
	"github.com/celskeggs/hailburst/sim/testpoint"
	"math/rand"
	"testing"
)

func RandCommPacket(r *rand.Rand) *CommPacket {
	return &CommPacket{
		MagicNumber: r.Uint32(),
		CmdTlmId:    r.Uint32(),
		Timestamp:   r.Uint64(),
		DataBytes:   testpoint.RandPacket(r),
		CRC:         r.Uint32(),
	}
}

func CommPacketsEqual(c1, c2 *CommPacket) bool {
	return c1.MagicNumber == c2.MagicNumber && c1.CRC == c2.CRC && c1.CmdTlmId == c2.CmdTlmId && c1.Timestamp == c2.Timestamp && bytes.Equal(c1.DataBytes, c2.DataBytes)
}

func TestCodecSimple(t *testing.T) {
	r := rand.New(rand.NewSource(1111))
	for i := 0; i < 1000; i++ {
		cp1 := RandCommPacket(r)
		encoded := cp1.Encode()
		cp2, bskip, berr := DecodeCommPacket(encoded)
		if berr != 0 {
			t.Errorf("should not be any byte errors: %v", berr)
		}
		if bskip != len(encoded) {
			t.Errorf("should use every byte remaining (%d), not %d", len(encoded), bskip)
		}
		if !CommPacketsEqual(cp1, cp2) {
			t.Errorf("comm packets do not match: %v, %v", cp1, cp2)
		}
	}
}
