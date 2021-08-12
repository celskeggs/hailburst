package codec

import "encoding/binary"

// MakeDecoder is the same as MakeCharDecoder, but additionally decodes parametrized control characters.
func MakeDecoder(readData func([]byte), readCtrl func(ControlChar, uint32)) *Decoder {
	var recvParam []byte
	pendingCtrl := ChNone

	return MakeCharDecoder(func(bytes []byte) {
		if pendingCtrl == ChNone {
			readData(bytes)
		} else {
			recvParam = append(recvParam, bytes...)
			if len(recvParam) >= 4 {
				readCtrl(pendingCtrl, binary.BigEndian.Uint32(recvParam))

				if len(recvParam) > 4 {
					readData(recvParam[4:])
				}

				pendingCtrl = ChNone
				recvParam = nil
			}
		}
	}, func(char ControlChar) {
		if pendingCtrl != ChNone {
			readCtrl(ChCodecError, 0)
			pendingCtrl = ChNone
		}
		if char.IsParametrized() {
			pendingCtrl = char
			recvParam = nil
		} else {
			readCtrl(char, 0)
		}
	})
}
