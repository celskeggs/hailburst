package codec

import "github.com/celskeggs/hailburst/sim/util"

func EncodeDataBytes(data []byte) []byte {
	result := make([]byte, len(data)*2)
	outIndex := 0
	for _, b := range data {
		if IsCtrl(b) {
			// needs to be escaped; encode byte so that it remains in the data range
			result[outIndex] = byte(ChEscapeSym)
			outIndex += 1
			b ^= 0x10
		}
		result[outIndex] = b
		outIndex += 1
	}
	return result[:outIndex]
}

func EncodeCtrlChar(cc ControlChar, p uint32) (out []byte) {
	cc.Validate(p)
	out = []byte{byte(cc)}
	if cc.IsParametrized() {
		out = append(out, EncodeDataBytes(util.EncodeUint32BE(p))...)
	}
	return out
}
