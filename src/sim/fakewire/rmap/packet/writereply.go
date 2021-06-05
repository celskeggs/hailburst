package packet

import (
	"encoding/binary"
	"fmt"
	"sim/fakewire/fwmodel"
)

type WriteReply struct {
	SourcePath                Path
	SourceLogicalAddress      uint8 // should default to 254
	OptVerifyData             bool
	OptIncrement              bool
	Status                    uint8 // 0 for successful execution
	DestinationLogicalAddress uint8 // should default to 254
	TransactionIdentifier     uint16
}

// these next three functions included for testing purposes

func (wr WriteReply) IsValid() bool {
	return len(wr.SourcePath) <= 12 && isSourcePathValid(wr.SourcePath)
}

func (wr WriteReply) VerifyData() bool {
	return true  // no data to verify
}

func (wr WriteReply) PathBytes() (routing []uint8, npath int) {
	path := append([]byte{}, wr.SourcePath...)
	path = append(path, wr.SourceLogicalAddress)
	return path, len(wr.SourcePath)
}

func (wr WriteReply) MergePath(path Path) (Packet, error) {
	copied := wr
	if len(copied.SourcePath) > 0 {
		return nil, fmt.Errorf("source path unexpectedly already populated")
	}
	copied.SourcePath = path
	return copied, nil
}

// assumes top-level validation already performed in DecodePacket
func decodeWriteReply(ptcspalb uint8, packet []byte) (Packet, error) {
	// write reply packet
	wr := WriteReply{}
	wr.SourcePath = nil
	wr.SourceLogicalAddress = packet[0]
	wr.OptVerifyData = (ptcspalb & bitVerifyData) != 0
	if (ptcspalb & bitAcknowledge) == 0 {
		return nil, fmt.Errorf("not a valid RMAP write reply packet: ACK bit not set (len=%d, ptcspalb=%02x)", len(packet), ptcspalb)
	}
	// parse source path address
	if len(packet) != 8 {
		return nil, fmt.Errorf("not a valid RMAP write reply packet on second check (len=%d)", len(packet))
	}
	wr.OptIncrement = (ptcspalb & bitIncrement) != 0
	// ignore the source path address length field... not important!
	wr.Status = packet[3]
	wr.DestinationLogicalAddress = packet[4]
	wr.TransactionIdentifier = binary.BigEndian.Uint16(packet[5:7])
	headerCRC := packet[7]
	computedHeaderCRC := RmapCrc8(packet[0:7])
	if headerCRC != computedHeaderCRC {
		return nil, fmt.Errorf("invalid CRC on RMAP write reply header: computed %02x but header states %02x", computedHeaderCRC, headerCRC)
	}
	return wr, nil
}

func (wr WriteReply) Encode() ([]byte, error) {
	packet := append([]byte{}, wr.SourcePath...)
	var ptcspalb uint8 = bitIsWrite | bitAcknowledge
	if wr.OptVerifyData {
		ptcspalb |= bitVerifyData
	}
	if wr.OptIncrement {
		ptcspalb |= bitIncrement
	}
	encodedPath, err := encodeSourcePath(wr.SourcePath)
	if err != nil {
		return nil, err
	}
	if len(encodedPath)%4 != 0 {
		panic("invalid encoding")
	}
	sourcePathAddrLen := len(encodedPath) / 4
	if (sourcePathAddrLen & bitsSPAL) != sourcePathAddrLen {
		return nil, fmt.Errorf("source path is too long: %d bytes", len(wr.SourcePath))
	}
	ptcspalb |= uint8(sourcePathAddrLen)
	packet = append(packet, wr.SourceLogicalAddress, fwmodel.IdentifierRMAP, ptcspalb, wr.Status)
	packet = append(packet, wr.DestinationLogicalAddress)
	packet = append(packet, encodeUint16BE(wr.TransactionIdentifier)...)
	packet = append(packet, RmapCrc8(packet[len(wr.SourcePath):]))
	return packet, nil
}
