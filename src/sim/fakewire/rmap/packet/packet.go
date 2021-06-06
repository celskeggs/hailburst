package packet

import (
	"fmt"
	"sim/fakewire/fwmodel"
)

type Packet interface {
	Encode() ([]byte, error)

	// VerifyData makes sure that the data checksum matches, if relevant
	VerifyData() bool

	// these next three functions included for testing purposes

	IsValid() bool
	PathBytes() (routing []uint8, npath int)
	MergePath(path Path) (Packet, error)
}

const (
	bitReserved    = 0x80
	bitIsCommand   = 0x40
	bitIsWrite     = 0x20
	bitVerifyData  = 0x10
	bitAcknowledge = 0x08
	bitIncrement   = 0x04
	bitsSPAL       = 0x03 // source path address length
)

func DecodePacket(packet []byte) (Packet, error) {
	if len(packet) >= 2 && packet[1] != fwmodel.IdentifierRMAP {
		return nil, fmt.Errorf("not a valid RMAP packet (len=%d, proto=%d)", len(packet), packet[1])
	}
	if len(packet) < 8 {
		return nil, fmt.Errorf("not a valid RMAP packet (len=%d)", len(packet))
	}
	// packet type / command / source path address length byte (PTCSPALB)
	ptcspalb := packet[2]
	if (ptcspalb & bitReserved) != 0 {
		return nil, fmt.Errorf("reserved PTCSPALB in RMAP packet: %x", ptcspalb)
	}
	switch ptcspalb & (bitIsCommand | bitIsWrite) {
	case 0:
		// ready reply packet
		return decodeReadReply(ptcspalb, packet)
	case bitIsWrite:
		// write reply packet
		return decodeWriteReply(ptcspalb, packet)
	case bitIsCommand:
		// read packet
		return decodeReadPacket(ptcspalb, packet)
	case bitIsWrite | bitIsCommand:
		// write packet
		return decodeWritePacket(ptcspalb, packet)
	default:
		panic("impossible bitmasking result")
	}
}
