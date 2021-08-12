package codec

import "fmt"

type ControlChar uint8

const (
	ChNone ControlChar = 0x00

	ChHandshake1  ControlChar = 0x80
	ChHandshake2  ControlChar = 0x81
	ChStartPacket ControlChar = 0x82
	ChEndPacket   ControlChar = 0x83
	ChErrorPacket ControlChar = 0x84
	ChFlowControl ControlChar = 0x85
	ChKeepAlive   ControlChar = 0x86
	ChEscapeSym   ControlChar = 0x87

	// ChCodecError is an alias, because EscapeSym never needs to be passed to an upper layer
	ChCodecError  = ChEscapeSym
)

func (cc ControlChar) String() string {
	switch cc {
	case ChNone:
		return "None"
	case ChHandshake1:
		return "Handshake1"
	case ChHandshake2:
		return "Handshake2"
	case ChStartPacket:
		return "StartPacket"
	case ChEndPacket:
		return "EndPacket"
	case ChErrorPacket:
		return "ErrorPacket"
	case ChFlowControl:
		return "FlowControl"
	case ChKeepAlive:
		return "KeepAlive"
	case ChCodecError:
		return "CodecError"
	default:
		panic(fmt.Sprintf("invalid control character: 0x%x", uint8(cc)))
	}
}

func (cc ControlChar) IsParametrized() bool {
	return cc == ChHandshake1 || cc == ChHandshake2 || cc == ChFlowControl || cc == ChKeepAlive
}

func (cc ControlChar) Validate(param uint32) {
	if !IsCtrl(byte(cc)) {
		panic("invalid control character")
	}
	if param != 0 && !cc.IsParametrized() {
		panic("unexpected parameter on symbol")
	}
}

func IsCtrl(raw byte) bool {
	return ControlChar(raw) >= ChHandshake1 && ControlChar(raw) <= ChEscapeSym
}
