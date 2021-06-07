package ground

import (
	"bytes"
	"encoding/binary"
)

/*
Comm packet format:
 1. Escape sequences begin with 0xFF bytes.
 2. 0xFF 0x11 means "a single 0xFF"
 3. 0xFF 0x22 means "start packet", and is followed by a header.
 4. The header consists of a 32-bit directional magic number, a 32-bit command ID, and a 64-bit timestamp.
 5. After the header, the body of the packet begins, and continues until an "end packet normally" marker.
 5. 0xFF 0x33 means "end packet normally", and is immediately preceded by a 32-bit CRC of the header and the packet data.
*/

const (
	Escape    = 0xFF
	EscEscape = 0x11
	EscSOP    = 0x22
	EscEOP    = 0x33
)

type CommPacket struct {
	MagicNumber uint32
	CommandId   uint32
	Timestamp   uint64
	DataBytes   []byte
	CRC         uint32
}

func decodeEscapes(region []byte) (decoded []byte, ok bool) {
	decoded = make([]byte, 0, len(region))
	// cannot handle smashed escapes
	if len(region) >= 1 && region[len(region)-1] == 0xFF {
		return nil, false
	}
	// make sure there are no escapes except Escape+EscEscape
	for i := 0; i < len(region) - 1; i++ {
		if region[i] == Escape && region[i+1] != EscEscape {
			return nil, false
		}
	}
	// then fix all of them up
	return bytes.ReplaceAll(region, []byte{Escape, EscEscape}, []byte{Escape}), true
}

func encodeEscapes(region []byte) (encoded []byte) {
	return bytes.ReplaceAll(region, []byte{Escape}, []byte{Escape, EscEscape})
}

func DecodeCommPacket(stream []byte) (packet *CommPacket, bytesUsed int, errors int) {
	if len(stream) < 2 {
		return nil, 0, 0
	}
	// chew up any bytes until the next start-of-packet (SOP)
	sop := bytes.Index(stream, []byte{0xFF, 0x22})
	if sop == -1 {
		// chew up everything except the last byte, which could be a 0xFF
		return nil, len(stream) - 1, len(stream) - 1
	}
	// now see if we have an end-of-packet later
	eop := bytes.Index(stream, []byte{0xFF, 0x33})
	if eop == -1 {
		// just cut off the first non-SOP part, if relevant... leave the partial packet, for now!
		return nil, sop, sop
	}
	consumed := eop + 2
	// if we have both a start and end of packet, try decoding the middle
	decodedBytes, ok := decodeEscapes(stream[2:eop])
	if !ok || len(decodedBytes) < 20 {
		return nil, consumed, consumed
	}
	be := binary.BigEndian
	return &CommPacket{
		MagicNumber: be.Uint32(decodedBytes[0:]),
		CommandId:   be.Uint32(decodedBytes[4:]),
		Timestamp:   be.Uint64(decodedBytes[8:]),
		DataBytes:   decodedBytes[16 : len(decodedBytes)-4],
		CRC:         be.Uint32(decodedBytes[len(decodedBytes)-4:]),
	}, consumed, sop
}

func (cp *CommPacket) Encode() []byte {
	plain := make([]byte, 20+len(cp.DataBytes))
	be := binary.BigEndian
	be.PutUint32(plain[0:], cp.MagicNumber)
	be.PutUint32(plain[4:], cp.CommandId)
	be.PutUint64(plain[8:], cp.Timestamp)
	copy(plain[16:], cp.DataBytes)
	be.PutUint32(plain[len(plain)-4:], cp.CRC)

	var wrapped []byte
	wrapped = append(wrapped, Escape, EscSOP)
	wrapped = append(wrapped, encodeEscapes(plain)...)
	wrapped = append(wrapped, Escape, EscEOP)
	return wrapped
}
