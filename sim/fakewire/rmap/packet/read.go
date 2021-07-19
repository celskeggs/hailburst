package packet

import (
	"encoding/binary"
	"fmt"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
)

type ReadPacket struct {
	DestinationPath           Path
	DestinationLogicalAddress uint8 // should default to 254
	OptIncrement              bool
	DestinationKey            uint8
	SourcePath                Path
	SourceLogicalAddress      uint8 // should default to 254
	TransactionIdentifier     uint16
	ExtendedReadAddress       uint8
	ReadAddress               uint32
	DataLength                uint32
}

// these next three functions included for testing purposes

func (rp ReadPacket) IsValid() bool {
	return len(rp.SourcePath) <= 12 && isSourcePathValid(rp.SourcePath) && rp.DataLength <= MaxUint24
}

func (rp ReadPacket) VerifyData() bool {
	return true // no data to verify
}

func (rp ReadPacket) PathBytes() (routing []uint8, npath int) {
	path := append([]byte{}, rp.DestinationPath...)
	path = append(path, rp.DestinationLogicalAddress)
	return path, len(rp.DestinationPath)
}

func (rp ReadPacket) MergePath(path Path) (Packet, error) {
	copied := rp
	if len(copied.DestinationPath) > 0 {
		return nil, fmt.Errorf("destination path unexpectedly already populated")
	}
	copied.DestinationPath = path
	return copied, nil
}

// assumes top-level validation already performed in DecodePacket
func decodeReadPacket(ptcspalb uint8, packet []byte) (Packet, error) {
	// write packet
	rp := ReadPacket{}
	rp.DestinationPath = nil
	rp.DestinationLogicalAddress = packet[0]
	if ptcspalb&(bitVerifyData|bitAcknowledge) != bitAcknowledge {
		return nil, fmt.Errorf("not a valid RMAP read packet (len=%d, ptcspalb=%02x)", len(packet), ptcspalb)
	}
	rp.OptIncrement = (ptcspalb & bitIncrement) != 0
	sourcePathBytes := int(4 * (ptcspalb & bitsSPAL))
	rp.DestinationKey = packet[3]
	// parse source path address
	if len(packet) != 16+sourcePathBytes {
		return nil, fmt.Errorf("not a valid RMAP read packet on second check (len=%d, spb=%d)", len(packet), sourcePathBytes)
	}
	rp.SourcePath = decodeSourcePath(packet[4 : 4+sourcePathBytes])
	remHeader := packet[4+sourcePathBytes:]
	rp.SourceLogicalAddress = remHeader[0]
	rp.TransactionIdentifier = binary.BigEndian.Uint16(remHeader[1:3])
	rp.ExtendedReadAddress = remHeader[3]
	rp.ReadAddress = binary.BigEndian.Uint32(remHeader[4:8])
	rp.DataLength = uint24BE(remHeader[8:11])
	headerCRC := remHeader[11]
	computedHeaderCRC := RmapCrc8(packet[0 : 4+sourcePathBytes+11])
	if headerCRC != computedHeaderCRC {
		return nil, fmt.Errorf("invalid CRC on RMAP read packet header: computed %02x but header states %02x", computedHeaderCRC, headerCRC)
	}
	return rp, nil
}

func (rp ReadPacket) Encode() ([]byte, error) {
	packet := append([]byte{}, rp.DestinationPath...)
	var ptcspalb uint8 = bitIsCommand | bitAcknowledge
	if rp.OptIncrement {
		ptcspalb |= bitIncrement
	}
	encodedPath, err := encodeSourcePath(rp.SourcePath)
	if err != nil {
		return nil, err
	}
	if len(encodedPath)%4 != 0 {
		panic("invalid encoding")
	}
	sourcePathAddrLen := len(encodedPath) / 4
	if (sourcePathAddrLen & bitsSPAL) != sourcePathAddrLen {
		return nil, fmt.Errorf("source path is too long: %d bytes", len(rp.SourcePath))
	}
	ptcspalb |= uint8(sourcePathAddrLen)
	packet = append(packet, rp.DestinationLogicalAddress, fwmodel.IdentifierRMAP, ptcspalb, rp.DestinationKey)
	packet = append(packet, encodedPath...)
	packet = append(packet, rp.SourceLogicalAddress)
	packet = append(packet, encodeUint16BE(rp.TransactionIdentifier)...)
	packet = append(packet, rp.ExtendedReadAddress)
	packet = append(packet, encodeUint32BE(rp.ReadAddress)...)
	b, err := encodeUint24BE(rp.DataLength)
	if err != nil {
		return nil, err
	}
	packet = append(packet, b...)
	packet = append(packet, RmapCrc8(packet[len(rp.DestinationPath):]))
	return packet, nil
}
