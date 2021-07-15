package util

import "strings"

const BitsPerByte = 8

func BitsToByte(bits []bool) byte {
	if len(bits) != BitsPerByte {
		panic("invalid # of bits")
	}
	var output byte
	for i, bit := range bits {
		if bit {
			output |= 1 << i
		}
	}
	return output
}

func BitsToBytes(bits []bool) []byte {
	if len(bits)%BitsPerByte != 0 {
		panic("invalid # of bits")
	}
	output := make([]byte, len(bits)/BitsPerByte)
	for i := 0; i < len(output); i++ {
		output[i] = BitsToByte(bits[i*BitsPerByte : (i+1)*BitsPerByte])
	}
	return output
}

func BytesToBits(bytes []byte) []bool {
	output := make([]bool, len(bytes)*BitsPerByte)
	for i, b := range bytes {
		for j := 0; j < BitsPerByte; j++ {
			output[i*BitsPerByte+j] = (b & (1 << j)) != 0
		}
	}
	return output
}

func StringBits0(data []bool) string {
	var midbits []string
	for _, bit := range data {
		if bit {
			midbits = append(midbits, "1")
		} else {
			midbits = append(midbits, "0")
		}
	}
	return strings.Join(midbits, "")
}

func StringBits(data []byte) string {
	return StringBits0(BytesToBits(data))
}
