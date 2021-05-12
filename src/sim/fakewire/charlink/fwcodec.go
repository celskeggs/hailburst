package charlink

import (
	"sim/fakewire/fwmodel"
)

const minBitsPerFWChar = 4
const maxBitsPerFWChar = 10

type FWEncoder struct {
	lastRemainderOdd bool
	parityOk         bool
}

func MakeFWEncoder() *FWEncoder {
	return &FWEncoder{
		lastRemainderOdd: false, // either initialization should be fine
		parityOk:         true,
	}
}

func (fwe *FWEncoder) EncodeToBits(input []fwmodel.FWChar) (bits []bool, parityOk bool) {
	if !fwe.parityOk {
		return nil, false
	}

	output := make([]bool, 0, len(input)*maxBitsPerFWChar)

	lastRemainderOdd := fwe.lastRemainderOdd

	for _, ch := range input {
		if ch == fwmodel.ParityFail {
			// we can let the last byte be written safely...
			output = append(output, lastRemainderOdd, true)
			// and then produce one that necessarily fails parity
			for i := 0; i < 12; i++ {
				output = append(output, false)
			}
			fwe.parityOk = false
			return output, false
		}

		nextWord, isData := ch.Data()
		nbits := 8
		if !isData {
			nextWord = ch.CtrlIndex()
			nbits = 2
		}

		// first, send parity bit from the last message
		parityBit := lastRemainderOdd != isData

		// store the bits
		output = append(output, parityBit, !isData)

		// reset for next parity computation
		lastRemainderOdd = false

		// and now our data/control bits
		for i := 0; i < nbits; i++ {
			bit := (nextWord & (1 << i)) != 0

			output = append(output, bit)

			// flip last remainder oddness if bit is one
			lastRemainderOdd = bit != lastRemainderOdd
		}
	}

	fwe.lastRemainderOdd = lastRemainderOdd

	return output, true
}

type FWDecoder struct {
	hasCtrlBit bool
	ctrlBit    bool
	parityOk   bool
}

func MakeFWDecoder() *FWDecoder {
	return &FWDecoder{
		hasCtrlBit: false,
		parityOk:   true,
	}
}

// grabs N bits plus one parity bit
func grabBitsAndParity(input []bool, bits int) (value uint8, parityOdd bool) {
	if len(input) != bits+1 {
		panic("invalid input to grabBitsAndParity")
	}
	parityOdd = false
	for i := 0; i <= bits; i++ {
		// flip parityOdd if v is true
		parityOdd = input[i] != parityOdd
		// only incorporate bit if we're not the last bit (the parity bit)
		if input[i] && i < bits {
			value |= 1 << i
		}
	}
	return value, parityOdd
}

func (fwe *FWDecoder) DecodeFromBits(bits []bool) (data []fwmodel.FWChar, consumed int, parityOk bool) {
	if len(bits) < minBitsPerFWChar {
		panic("should only call DecodeFromBits when there are enough bits for a char to be at all possible")
	}
	if !fwe.parityOk {
		return nil, 0, false
	}
	consumed = 0

	ctrlBit := fwe.ctrlBit

	if !fwe.hasCtrlBit {
		// discard bits[0], which is the initial parity bit and not important
		fwe.hasCtrlBit = true
		ctrlBit = bits[1]
		consumed = 2
	}

	// fmt.Printf("input bits (%d): %v\n", len(bits), bits)

	decoded := make([]fwmodel.FWChar, 0, len(bits)/minBitsPerFWChar)

	for {
		var nextBits int
		var char fwmodel.FWChar

		if ctrlBit {
			nextBits = 2
		} else {
			nextBits = 8
		}

		if len(bits)-consumed < nextBits+2 {
			break
		}

		nextChar, parityOdd := grabBitsAndParity(bits[consumed:consumed+nextBits+1], nextBits)
		// fmt.Printf("Decode: C=%v, NC=%d, NB=%d, BITS=%v\n", util.StringBits0([]bool{ctrlBit}), nextChar, nextBits, util.StringBits0(bits[consumed:consumed+nextBits+2]))
		consumed += nextBits + 1

		if ctrlBit {
			char = fwmodel.CtrlCharFromIndex(nextChar)
		} else {
			char = fwmodel.DataChar(nextChar)
		}

		// control bit for next byte is included in our parity check
		ctrlBit = bits[consumed]
		consumed += 1

		if ctrlBit == parityOdd {
			// either the control bit was 1 when we already had an odd number of ones bits
			// OR the control bit was 0 when we already had an even number of ones bits
			// regardless -- parity error!
			fwe.parityOk = false
			// indicate the parity error to the receiver
			decoded = append(decoded, fwmodel.ParityFail)
			break
		}

		// have to wait until here to add, so that we don't produce data if has the wrong parity
		decoded = append(decoded, char)
	}

	fwe.ctrlBit = ctrlBit

	return decoded, consumed, fwe.parityOk
}
