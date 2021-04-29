package fakewire

func encodeFakeWire(output chan<- bool, input <-chan FWChar) {
	defer close(output)

	lastRemainderOdd := false // either initialization should be fine

	for ch := range input {
		nextWord, isData := ch.Data()
		nbits := 8
		if !isData {
			nextWord = ch.ctrlIndex()
			nbits = 2
		}

		// first, send parity bit from the last message
		parityBit := lastRemainderOdd != isData
		output <- parityBit

		// now our control bit
		output <- !isData

		// reset for next parity computation
		lastRemainderOdd = false

		// and now our data/control bits
		for i := 0; i < nbits; i++ {
			bit := (nextWord & (1 << i)) != 0
			output <- bit

			// flip last remainder oddness if bit is one
			lastRemainderOdd = bit != lastRemainderOdd
		}
	}

	// cannot finish our current character, not without knowing the next one... just cut it off here, and the other
	// side will interpret this as a parity error.
}

// grabs N bits plus one parity bit
func grabBitsAndParity(input <-chan bool, bits int) (value uint8, parityOdd, ok bool) {
	parityOdd = false
	for i := 0; i <= bits; i++ {
		v, ok := <-input
		if !ok {
			return 0, false, false
		}
		// flip parityOdd if v is true
		parityOdd = v != parityOdd
		// only incorporate bit if we're not the last bit (the parity bit)
		if v && i < bits {
			value |= 1 << i
		}
	}
	return value, parityOdd, true
}

func decodeFakeWire(output chan<- FWChar, input <-chan bool) {
	defer close(output)

	// discard parity bit
	grabBitsAndParity(input, 0)
	// but keep the first control bit
	ctrlBit := <-input

	// (we don't bother checking for closed channels yet... we'll hit the next tripwire before that matters.)

	for {
		var nextBits int
		var char FWChar

		if ctrlBit {
			nextBits = 2
		} else {
			nextBits = 8
		}

		nextChar, parityOdd, ok := grabBitsAndParity(input, nextBits)
		if !ok {
			// connection terminated... parity error!
			return
		}

		if ctrlBit {
			char = ctrlCharFromIndex(nextChar)
		} else {
			char = DataChar(nextChar)
		}

		// control bit for next byte is included in our parity check
		ctrlBit, ok = <-input

		if ctrlBit == parityOdd || !ok {
			// either the control bit was 1 when we already had an odd number of ones bits
			// OR the control bit was 0 when we already had an even number of ones bits
			// OR the input just terminated
			// regardless -- parity error!
			return
		}

		// have to wait until here to send, so that we don't send data if it has the wrong parity
		output <- char
	}
}
