package fwmodel

import (
	"fmt"
	"strconv"
)

type FWChar uint16

// we use a different bit pattern here from fakewire.h to avoid the impression that it's part of the protocol
// (it's just part of how the characters are efficiently represented on each side)
const (
	// CtrlFCT represents a Flow Control Token
	CtrlFCT FWChar = 0x1A0
	// CtrlEOP represents a Normal End of Packet
	CtrlEOP FWChar = 0x1A1
	// CtrlEEP represents an Error End of Packet
	CtrlEEP FWChar = 0x1A2
	// CtrlESC represents an Escape
	CtrlESC FWChar = 0x1A3

	// ParityFail indicates that a parity error was encountered (or should be produced)
	ParityFail FWChar = 0x1FF
)

func DataChar(d uint8) FWChar {
	return FWChar(d)
}

func CtrlCharFromIndex(index uint8) FWChar {
	if index > 3 {
		panic("invalid index for control char")
	}
	return CtrlFCT + FWChar(index)
}

func (c FWChar) CtrlIndex() uint8 {
	if c >= CtrlFCT && c <= CtrlESC {
		return uint8(c - CtrlFCT)
	}
	panic("not a control character when generating index!")
}

func (c FWChar) Data() (uint8, bool) {
	if c <= 0xFF {
		return uint8(c), true
	}
	// not data... see if it's a control character, or else an error?
	switch c {
	case CtrlFCT:
		return 0, false
	case CtrlEOP:
		return 0, false
	case CtrlEEP:
		return 0, false
	case CtrlESC:
		return 0, false
	default:
		panic(fmt.Sprintf("Neither data character nor control character: %d", uint16(c)))
	}
}

func (c FWChar) DataUnwrap() uint8 {
	ch, ok := c.Data()
	if !ok {
		panic("expected data character")
	}
	return ch
}

func (c FWChar) IsCtrl() bool {
	_, isData := c.Data()
	return !isData
}

func (c FWChar) String() string {
	if c <= 0xFF {
		return strconv.QuoteRuneToASCII(rune(c))
	}
	switch c {
	case CtrlFCT:
		return "FCT"
	case CtrlEOP:
		return "EOP"
	case CtrlEEP:
		return "EEP"
	case CtrlESC:
		return "ESC"
	case ParityFail:
		return "ParityFail"
	default:
		panic("unrecognized character during String")
	}
}
