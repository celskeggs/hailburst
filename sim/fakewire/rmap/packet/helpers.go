package packet

import (
	"errors"
)

type Path []uint8

func decodeSourcePath(path []byte) Path {
	for len(path) > 1 && path[0] == 0 {
		path = path[1:]
	}
	return path
}

func encodeSourcePath(path Path) ([]byte, error) {
	if len(path) > 1 && path[0] == 0 {
		return nil, errors.New("cannot encode any source path starting with 0x00 except 0x00 itself")
	}
	prefixZeros := make([]byte, 3-((len(path)+3)%4))
	return append(prefixZeros, path...), nil
}

func uint24BE(b []byte) uint32 {
	return uint32(b[2]) | uint32(b[1])<<8 | uint32(b[0])<<16
}

const MaxUint24 = 0x00FFFFFF

func isSourcePathValid(path Path) bool {
	return len(path) <= 1 || path[0] != 0
}
