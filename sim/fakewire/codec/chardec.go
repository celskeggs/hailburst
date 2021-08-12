package codec

type Decoder struct {
	inEscape bool
	readData func([]byte)
	readCtrl func(ControlChar)
}

func MakeCharDecoder(readData func([]byte), readCtrl func(ControlChar)) *Decoder {
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
