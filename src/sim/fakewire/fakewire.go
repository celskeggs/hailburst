package fakewire

// DecodeFakeWire translates input bytes from a serial line to FakeWire characters.
// If the input closes, that counts as a parity error.
// This function only exits, and the output closes, when a parity error is encountered.
func DecodeFakeWire(output chan<- FWChar, input <-chan uint8) {
	bits := make(chan bool, 1024)
	go encodeToBits(bits, input)
	decodeFakeWire(output, bits)
}

// EncodeFakeWire translates FakeWire characters to output bytes for a serial line.
// The output closes, and the function returns, when the input closes.
func EncodeFakeWire(output chan<- uint8, input <-chan FWChar) {
	bits := make(chan bool, 1024)
	go decodeFromBits(output, bits)
	encodeFakeWire(bits, input)
}
