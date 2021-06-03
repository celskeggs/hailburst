package rmap

import (
	"encoding/binary"
	"errors"
	"fmt"
)

func decodeSourcePath(path []byte) []byte {
	for len(path) > 1 && path[0] == 0 {
		path = path[1:]
	}
	return path
}

func encodeSourcePath(path []byte) ([]byte, error) {
	if len(path) > 1 && path[0] == 0 {
		return nil, errors.New("cannot encode any source path starting with 0x00 except 0x00 itself")
	}
	prefixZeros := make([]byte, 3-((len(path)+3)%4))
	return append(prefixZeros, path...), nil
}

func uint24BE(b []byte) uint32 {
	return uint32(b[2]) | uint32(b[1])<<8 | uint32(b[0])<<16
}

func encodeUint16BE(u uint16) []byte {
	var out [2]byte
	binary.BigEndian.PutUint16(out[:], u)
	return out[:]
}

const maxUint24 = 0x00FFFFFF

func encodeUint24BE(u uint32) ([]byte, error) {
	out := encodeUint32BE(u)
	if out[0] != 0 {
		return nil, fmt.Errorf("value is too large for 24-bit encoding: %v", u)
	}
	return out[1:4], nil
}

func encodeUint32BE(u uint32) []byte {
	var out [4]byte
	binary.BigEndian.PutUint32(out[:], u)
	return out[:]
}

func isSourcePathValid(path []byte) bool {
	return len(path) <= 1 || path[0] != 0
}
