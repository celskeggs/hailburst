package util

import (
	"encoding/binary"
	"fmt"
)

func EncodeUint16BE(u uint16) []byte {
	var out [2]byte
	binary.BigEndian.PutUint16(out[:], u)
	return out[:]
}

func EncodeUint24BE(u uint32) ([]byte, error) {
	out := EncodeUint32BE(u)
	if out[0] != 0 {
		return nil, fmt.Errorf("value is too large for 24-bit encoding: %v", u)
	}
	return out[1:4], nil
}

func EncodeUint32BE(u uint32) []byte {
	var out [4]byte
	binary.BigEndian.PutUint32(out[:], u)
	return out[:]
}
