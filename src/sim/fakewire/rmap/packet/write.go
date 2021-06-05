package packet

import (
	"encoding/binary"
	"fmt"
	"sim/fakewire/fwmodel"
)

type WritePacket struct {
	DestinationPath           Path
	DestinationLogicalAddress uint8 // should default to 254
	OptVerifyData             bool
	OptAcknowledge            bool
	OptIncrement              bool
	DestinationKey            uint8
	SourcePath                Path
	SourceLogicalAddress      uint8 // should default to 254
	TransactionIdentifier     uint16
	ExtendedWriteAddress      uint8
	WriteAddress              uint32
	DataBytes                 []byte
	DataCRC                   uint8
}

// these next three functions included for testing purposes

func (wp WritePacket) IsValid() bool {
	return len(wp.SourcePath) <= 12 && isSourcePathValid(wp.SourcePath) && len(wp.DataBytes) <= MaxUint24
}

func (wp WritePacket) VerifyData() bool {
	return RmapCrc8(wp.DataBytes) == wp.DataCRC
}

func (wp WritePacket) PathBytes() (routing []uint8, npath int) {
	path := append([]byte{}, wp.DestinationPath...)
	path = append(path, wp.DestinationLogicalAddress)
	return path, len(wp.DestinationPath)
}

func (wp WritePacket) MergePath(path Path) (Packet, error) {
	copied := wp
	if len(copied.DestinationPath) > 0 {
		return nil, fmt.Errorf("destination path unexpectedly already populated")
	}
	copied.DestinationPath = path
	return copied, nil
}

// assumes top-level validation already performed in DecodePacket
func decodeWritePacket(ptcspalb uint8, packet []byte) (Packet, error) {
	// write packet
	wp := WritePacket{}
	wp.DestinationPath = nil
	wp.DestinationLogicalAddress = packet[0]
	wp.OptVerifyData = (ptcspalb & bitVerifyData) != 0
	wp.OptAcknowledge = (ptcspalb & bitAcknowledge) != 0
	wp.OptIncrement = (ptcspalb & bitIncrement) != 0
	sourcePathBytes := int(4 * (ptcspalb & bitsSPAL))
	wp.DestinationKey = packet[3]
	// parse source path address
	if len(packet) < 17+sourcePathBytes {
		return nil, fmt.Errorf("not a valid RMAP write packet on second check (len=%d, spb=%d)", len(packet), sourcePathBytes)
	}
	wp.SourcePath = decodeSourcePath(packet[4 : 4+sourcePathBytes])
	remHeader := packet[4+sourcePathBytes:]
	wp.SourceLogicalAddress = remHeader[0]
	wp.TransactionIdentifier = binary.BigEndian.Uint16(remHeader[1:3])
	wp.ExtendedWriteAddress = remHeader[3]
	wp.WriteAddress = binary.BigEndian.Uint32(remHeader[4:8])
	dataLength := uint24BE(remHeader[8:11])
	headerCRC := remHeader[11]
	computedHeaderCRC := RmapCrc8(packet[0 : 4+sourcePathBytes+11])
	if headerCRC != computedHeaderCRC {
		return nil, fmt.Errorf("invalid CRC on RMAP write packet header: computed %02x but header states %02x", computedHeaderCRC, headerCRC)
	}
	wp.DataBytes = remHeader[12 : len(remHeader)-1]
	// TODO: handle length mismatches in a way consistent with a streaming implementation, like we did with CRC mismatches
	if uint32(len(wp.DataBytes)) != dataLength {
		return nil, fmt.Errorf("invalid number of data bytes in RMAP write packet: header specified %d but packet size implied %d", dataLength, len(wp.DataBytes))
	}
	wp.DataCRC = remHeader[len(remHeader)-1]
	return wp, nil
}

func (wp WritePacket) Encode() ([]byte, error) {
	packet := append([]byte{}, wp.DestinationPath...)
	var ptcspalb uint8 = bitIsCommand | bitIsWrite
	if wp.OptVerifyData {
		ptcspalb |= bitVerifyData
	}
	if wp.OptAcknowledge {
		ptcspalb |= bitAcknowledge
	}
	if wp.OptIncrement {
		ptcspalb |= bitIncrement
	}
	encodedPath, err := encodeSourcePath(wp.SourcePath)
	if err != nil {
		return nil, err
	}
	if len(encodedPath)%4 != 0 {
		panic("invalid encoding")
	}
	sourcePathAddrLen := len(encodedPath) / 4
	if (sourcePathAddrLen & bitsSPAL) != sourcePathAddrLen {
		return nil, fmt.Errorf("source path is too long: %d bytes", len(wp.SourcePath))
	}
	ptcspalb |= uint8(sourcePathAddrLen)
	packet = append(packet, wp.DestinationLogicalAddress, fwmodel.IdentifierRMAP, ptcspalb, wp.DestinationKey)
	packet = append(packet, encodedPath...)
	packet = append(packet, wp.SourceLogicalAddress)
	packet = append(packet, encodeUint16BE(wp.TransactionIdentifier)...)
	packet = append(packet, wp.ExtendedWriteAddress)
	packet = append(packet, encodeUint32BE(wp.WriteAddress)...)
	b, err := encodeUint24BE(uint32(len(wp.DataBytes)))
	if err != nil {
		return nil, err
	}
	packet = append(packet, b...)
	packet = append(packet, RmapCrc8(packet[len(wp.DestinationPath):]))
	packet = append(packet, wp.DataBytes...)
	if wp.DataCRC != RmapCrc8(wp.DataBytes) {
		return nil, fmt.Errorf("cannot encoded with mismatched CRC")
	}
	packet = append(packet, wp.DataCRC)
	return packet, nil
}
