package packet

import (
	"encoding/binary"
	"fmt"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/util"
)

type ReadReply struct {
	SourcePath                Path
	SourceLogicalAddress      uint8 // should default to 254
	OptIncrement              bool
	Status                    uint8 // 0 for successful execution
	DestinationLogicalAddress uint8 // should default to 254
	TransactionIdentifier     uint16
	DataBytes                 []byte
}

// these next three functions included for testing purposes

func (rr ReadReply) IsValid() bool {
	return len(rr.SourcePath) <= 12 && isSourcePathValid(rr.SourcePath) && len(rr.DataBytes) <= MaxUint24
}

func (rr ReadReply) VerifyData() bool {
	return true // data automatically verified on receive; no additional check needed
}

func (rr ReadReply) PathBytes() (routing []uint8, npath int) {
	path := append([]byte{}, rr.SourcePath...)
	path = append(path, rr.SourceLogicalAddress)
	return path, len(rr.SourcePath)
}

func (rr ReadReply) MergePath(path Path) (Packet, error) {
	copied := rr
	if len(copied.SourcePath) > 0 {
		return nil, fmt.Errorf("source path unexpectedly already populated")
	}
	copied.SourcePath = path
	return copied, nil
}

// assumes top-level validation already performed in DecodePacket
func decodeReadReply(ptcspalb uint8, packet []byte) (Packet, error) {
	// read reply packet
	rr := ReadReply{}
	rr.SourcePath = nil
	rr.SourceLogicalAddress = packet[0]
	if (ptcspalb & (bitVerifyData | bitAcknowledge)) != bitAcknowledge {
		return nil, fmt.Errorf("not a valid RMAP read reply packet (len=%d, ptcspalb=%02x)", len(packet), ptcspalb)
	}
	rr.OptIncrement = (ptcspalb & bitIncrement) != 0
	// ignore the source path address length field... not important!
	rr.Status = packet[3]

	if len(packet) < 13 {
		return nil, fmt.Errorf("not a valid RMAP read reply packet on second check (len=%d)", len(packet))
	}
	rr.DestinationLogicalAddress = packet[4]
	rr.TransactionIdentifier = binary.BigEndian.Uint16(packet[5:7])
	if packet[7] != 0 {
		return nil, fmt.Errorf("nonzero value found in reserved field in RMAP read reply header (v=0x%02x)", packet[3])
	}
	dataLength := uint24BE(packet[8:11])
	headerCRC := packet[11]
	computedHeaderCRC := RmapCrc8(packet[0:11])
	if headerCRC != computedHeaderCRC {
		return nil, fmt.Errorf("invalid CRC on RMAP read reply header: computed %02x but header states %02x", computedHeaderCRC, headerCRC)
	}
	rr.DataBytes = packet[12 : len(packet)-1]
	if uint32(len(rr.DataBytes)) != dataLength {
		return nil, fmt.Errorf("invalid number of data bytes in RMAP read reply: header specified %d but packet size implied %d", dataLength, len(rr.DataBytes))
	}
	dataCRC := packet[len(packet)-1]
	computedDataCRC := RmapCrc8(rr.DataBytes)
	if dataCRC != computedDataCRC {
		return nil, fmt.Errorf("invalid CRC on RMAP read reply data: computed %02x but trailer states %02x", computedDataCRC, dataCRC)
	}
	return rr, nil
}

func (rr ReadReply) Encode() ([]byte, error) {
	packet := append([]byte{}, rr.SourcePath...)
	var ptcspalb uint8 = bitAcknowledge
	if rr.OptIncrement {
		ptcspalb |= bitIncrement
	}
	encodedPath, err := encodeSourcePath(rr.SourcePath)
	if err != nil {
		return nil, err
	}
	if len(encodedPath)%4 != 0 {
		panic("invalid encoding")
	}
	sourcePathAddrLen := len(encodedPath) / 4
	if (sourcePathAddrLen & bitsSPAL) != sourcePathAddrLen {
		return nil, fmt.Errorf("source path is too long: %d bytes", len(rr.SourcePath))
	}
	ptcspalb |= uint8(sourcePathAddrLen)
	packet = append(packet, rr.SourceLogicalAddress, fwmodel.IdentifierRMAP, ptcspalb, rr.Status)
	packet = append(packet, rr.DestinationLogicalAddress)
	packet = append(packet, util.EncodeUint16BE(rr.TransactionIdentifier)...)
	packet = append(packet, 0 /* reserved */)
	b, err := util.EncodeUint24BE(uint32(len(rr.DataBytes)))
	if err != nil {
		return nil, err
	}
	packet = append(packet, b...)
	packet = append(packet, RmapCrc8(packet[len(rr.SourcePath):]))
	packet = append(packet, rr.DataBytes...)
	packet = append(packet, RmapCrc8(rr.DataBytes))
	return packet, nil
}
