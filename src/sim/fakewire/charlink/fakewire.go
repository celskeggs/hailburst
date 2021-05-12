package charlink

import (
	"sim/fakewire/fwmodel"
	"sim/model"
	"sim/util"
)

const transcodeBufferSize = 4 * 1024 * 8 // 4 KB in bits

// DecodeFakeWire translates input bytes from a serial line to FakeWire characters.
// If the input closes, that counts as a parity error.
func DecodeFakeWire(ctx model.SimContext, output fwmodel.DataSinkFWChar, input model.DataSourceBytes) {
	bitbuffer := util.MakeBitBuffer(transcodeBufferSize)
	var currentWriteData []fwmodel.FWChar
	fwd := MakeFWDecoder()
	pumpOut := func() bool {
		// if we have data we're trying to write, write it
		if len(currentWriteData) == 0 {
			return false
		}
		count := output.TryWrite(currentWriteData)
		currentWriteData = currentWriteData[count:]
		// doesn't truly count as progress made (nothing new will be able to be done) until we're done writing
		return len(currentWriteData) == 0
	}
	firstTranscoding := true
	pumpTranscode := func() bool {
		// don't decode if we're already waiting for a write to finish
		if len(currentWriteData) > 0 {
			return false
		}
		// try to decode some of the bits in the buffer... if we have enough to do that
		if bitbuffer.BitCount() < minBitsPerFWChar {
			return false
		}
		nextBits := make([]bool, bitbuffer.BitCount())
		if bitbuffer.Peek(nextBits) != len(nextBits) {
			panic("invalid bitcount in buffer")
		}
		chars, consumed, parityOk := fwd.DecodeFromBits(nextBits)
		// fmt.Printf("out of %d bits, consumed %d to produce %d chars (parityok=%v)\n", len(nextBits), consumed, len(chars), parityOk)
		if consumed == 0 {
			return false
		}
		if consumed < 0 || consumed > len(nextBits) {
			panic("invalid consumption count")
		}
		if bitbuffer.Skip(consumed) != consumed {
			panic("invalid bitcount in buffer")
		}
		if parityOk && !firstTranscoding && len(chars) == 0 {
			panic("expected at least one char to be ready")
		}
		firstTranscoding = false
		currentWriteData = chars
		return true
	}
	pumpIn := func() bool {
		// try to pull in data from the source, but only if we have enough room to store it
		if bitbuffer.SpaceCount() < util.BitsPerByte {
			// fmt.Printf("not enough data to pump in\n")
			return false
		}
		buffer := make([]byte, bitbuffer.SpaceCount()/util.BitsPerByte)
		count := input.TryRead(buffer)
		// fmt.Printf("data pump in received %d/%d bytes\n", count, len(buffer))
		if count == 0 {
			return false
		}
		buffer = buffer[:count]
		// fmt.Printf("stored bits: %v\n", util.BytesToBits(buffer))
		if bitbuffer.Put(util.BytesToBits(buffer)) != len(buffer)*util.BitsPerByte {
			panic("invalid bitcount in buffer")
		}
		return true
	}
	pump := func() {
		// fmt.Printf("pumping fakewire decoder...\n")
		for pumpIn() || pumpTranscode() || pumpOut() {
			// continue running until we run out of either input data or output space
		}
	}
	input.Subscribe(pump)
	output.Subscribe(pump)
	// get it started
	ctx.Later("sim.fakewire.charlink.DecodeFakeWire/Start", pump)
}

// EncodeFakeWire translates FakeWire characters to output bytes for a serial line.
func EncodeFakeWire(ctx model.SimContext, output model.DataSinkBytes, input fwmodel.DataSourceFWChar) {
	bitbuffer := util.MakeBitBuffer(transcodeBufferSize)
	fwe := MakeFWEncoder()
	pumpOut := func() bool {
		// try to push data to the output, but only if we have enough data to push
		if bitbuffer.BitCount() < util.BitsPerByte {
			return false
		}
		bytesOut := bitbuffer.BitCount() / util.BitsPerByte
		bitdata := make([]bool, bytesOut*util.BitsPerByte)
		if bitbuffer.Peek(bitdata) != len(bitdata) {
			panic("invalid bitcount in buffer")
		}
		bytedata := util.BitsToBytes(bitdata)
		// fmt.Printf("bits to write: %d\n", bitbuffer.BitCount())
		actual := output.TryWrite(bytedata)
		if actual == 0 {
			return false
		}
		if actual < 0 || actual > len(bytedata) {
			panic("invalid output from TryWrite")
		}
		if bitbuffer.Skip(actual*util.BitsPerByte) != actual*util.BitsPerByte {
			panic("invalid bitcount in buffer")
		}
		// fmt.Printf("remaining bits to write after writing %d bytes: %d\n", actual, bitbuffer.BitCount())
		return true
	}
	pumpIn := func() bool {
		// try to pull in data from the source, but only if we have enough room to store it
		if bitbuffer.SpaceCount() < maxBitsPerFWChar {
			return false
		}
		fwcharbuf := make([]fwmodel.FWChar, bitbuffer.SpaceCount()/maxBitsPerFWChar)
		count := input.TryRead(fwcharbuf)
		if count == 0 {
			return false
		}
		fwcharbuf = fwcharbuf[:count]
		bits, parityOk := fwe.EncodeToBits(fwcharbuf)
		if parityOk && (len(bits) < minBitsPerFWChar*count || len(bits) > maxBitsPerFWChar*count) {
			panic("invalid output bitcount given input data")
		}
		actual := bitbuffer.Put(bits)
		if actual != len(bits) {
			panic("should have been enough buffer space for this output")
		}
		return true
	}
	pump := func() {
		for pumpIn() || pumpOut() {
			// continue running until we run out of either input data or output space
		}
	}
	input.Subscribe(pump)
	output.Subscribe(pump)
	// get it started
	ctx.Later("sim.fakewire.charlink.EncodeFakeWire/Start", pump)
}
