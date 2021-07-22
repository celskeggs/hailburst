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
	ChEscapeSym   ControlChar = 0x86
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
	case ChEscapeSym:
		return "EscapeSym"
	default:
		panic(fmt.Sprintf("invalid control character: 0x%x", uint8(cc)))
	}
}

func (cc ControlChar) Validate() {
	if !IsCtrl(byte(cc)) {
		panic("invalid control character")
	}
}

func IsCtrl(raw byte) bool {
	return ControlChar(raw) >= ChHandshake1 && ControlChar(raw) <= ChEscapeSym
}

type Decoder struct {
	inEscape bool
	readData func([]byte)
	readCtrl func(ControlChar)
}

func MakeDecoder(readData func([]byte), readCtrl func(ControlChar)) *Decoder {
	return &Decoder{
		inEscape: false,
		readData: readData,
		readCtrl: readCtrl,
	}
}

func (d *Decoder) Decode(raw []byte) {
	var lrData []byte
	writeData := func(b byte) {
		lrData = append(lrData, b)
	}
	writeCtrl := func(b ControlChar) {
		if len(lrData) > 0 {
			d.readData(lrData)
			lrData = nil
		}
		d.readCtrl(b)
	}

	for _, b := range raw {
		if d.inEscape {
			d.inEscape = false
			decoded := b ^ 0x10
			if IsCtrl(decoded) {
				// if valid escape sequence, write decoded byte
				writeData(decoded)
				continue
			}
			// otherwise... push a marker that the escape sequence was invalid, and process the character normally
			writeCtrl(ChEscapeSym)
		}
		if ControlChar(b) == ChEscapeSym {
			// escape sequence
			d.inEscape = true
		} else if IsCtrl(b) {
			// control character
			writeCtrl(ControlChar(b))
		} else {
			// regular byte
			writeData(b)
		}
	}

	if len(lrData) > 0 {
		d.readData(lrData)
	}
}

func EncodeDataBytes(data []byte) []byte {
	result := make([]byte, len(data) * 2)
	outIndex := 0
	for _, b := range data {
		if IsCtrl(b) {
			// needs to be escaped; encode byte so that it remains in the data range
			result[outIndex] = byte(ChEscapeSym)
			outIndex += 1
			b ^= 0x10
		}
		result[outIndex] = b
		outIndex += 1
	}
	return result[:outIndex]
}

func EncodeCtrlChar(cc ControlChar) []byte {
	cc.Validate()
	return []byte{byte(cc)}
}
