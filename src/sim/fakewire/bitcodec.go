package fakewire

func encodeToBits(output chan<- bool, input <-chan uint8) {
	defer close(output)

	bits := []uint8{ 1, 2, 4, 8, 16, 32, 64, 128 }

	for u8 := range input {
		for _, bit := range bits {
			output <- (u8 & bit) != 0
		}
	}
}

func decodeFromBits(output chan<- uint8, input <-chan bool) {
	defer close(output)

	var u8 uint8
	count := 0
	for bit := range input {
		if bit {
			u8 |= 1 << count
		}
		count += 1
		if count == 8 {
			output <- u8
			count = 0
			u8 = 0
		}
	}
}
