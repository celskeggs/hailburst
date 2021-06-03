package rmap

import (
	"fmt"
	"sim/fakewire/fwmodel"
)

type Device interface {
}

type Packet interface {
	Encode() ([]byte, error)

	// these next three functions included for testing purposes

	IsValid() bool
	PathBytes() (routing []byte, npath int)
	MergePath(path []byte) (Packet, error)
}

const (
	BitReserved    = 0x80
	BitIsCommand   = 0x40
	BitIsWrite     = 0x20
	BitVerifyData  = 0x10
	BitAcknowledge = 0x08
	BitIncrement   = 0x04
	BitsSPAL       = 0x03 // source path address length
)

func DecodePacket(packet []byte) (Packet, error) {
	if len(packet) >= 2 && packet[1] != fwmodel.IdentifierRMAP {
		return nil, fmt.Errorf("not a valid RMAP packet (len=%d, proto=%d)", len(packet), packet[0])
	}
	if len(packet) < 8 {
		return nil, fmt.Errorf("not a valid RMAP packet (len=%d)", len(packet))
	}
	// packet type / command / source path address length byte (PTCSPALB)
	ptcspalb := packet[2]
	if (ptcspalb & BitReserved) != 0 {
		return nil, fmt.Errorf("reserved PTCSPALB in RMAP packet: %x", ptcspalb)
	}
	switch ptcspalb & (BitIsCommand | BitIsWrite) {
	case 0:
		// ready reply packet
		return decodeReadReply(ptcspalb, packet)
	case BitIsWrite:
		// write reply packet
		return decodeWriteReply(ptcspalb, packet)
	case BitIsCommand:
		// read packet
		return decodeReadPacket(ptcspalb, packet)
	case BitIsWrite | BitIsCommand:
		// write packet
		return decodeWritePacket(ptcspalb, packet)
	default:
		panic("impossible bitmasking result")
	}
}
