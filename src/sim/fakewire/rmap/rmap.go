package rmap

import (
	"encoding/binary"
	"errors"
	"fmt"
	"sim/fakewire/fwmodel"
)

type Device interface {

}

type Packet interface {
	Encode() ([]byte, error)
}

type WritePacket struct {
	DestinationPath           []byte
	DestinationLogicalAddress uint8
	VerifyData                bool
	Acknowledge               bool
	Increment                 bool
	DestinationKey            uint8
	SourcePath                []byte
	SourceLogicalAddress      uint8
	TransactionIdentifier     uint16
	ExtendedWriteAddress      uint8
	WriteAddress              uint32
	DataBytes                 []byte
}

func DecodeSourcePath(path []byte) []byte {
	for len(path) > 1 && path[0] == 0 {
		path = path[1:]
	}
	return path
}

func EncodeSourcePath(path []byte) ([]byte, error) {
	if len(path) > 1 && path[0] == 0 {
		return nil, errors.New("cannot encode any source path starting with 0x00 except 0x00 itself")
	}
	prefixZeros := make([]byte, 3 - ((len(path) + 3) % 4))
	return append(prefixZeros, path...), nil
}

func Uint24BE(b []byte) uint32 {
	return uint32(b[2]) | uint32(b[1])<<8 | uint32(b[0])<<16
}

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

func DecodePacket(packet []byte) (Packet, error) {
	if len(packet) >= 2 && packet[1] != fwmodel.IdentifierRMAP {
		return nil, fmt.Errorf("not a valid RMAP packet (len=%d, proto=%d)", len(packet), packet[0])
	}
	if len(packet) < 17 {
		return nil, fmt.Errorf("not a valid RMAP packet (len=%d)", len(packet))
	}
	// packet type / command / source path address length byte (PTCSPALB)
	ptcspalb := packet[2]
	if (ptcspalb & 0xC0) != 0x40 {
		return nil, fmt.Errorf("invalid PTCSPALB in RMAP packet: %x", ptcspalb)
	}
	if (ptcspalb & 0x20) != 0 {
		// write packet
		wp := WritePacket{}
		wp.DestinationPath = nil
		wp.DestinationLogicalAddress = packet[0]
		wp.VerifyData = (ptcspalb & 0x10) != 0
		wp.Acknowledge = (ptcspalb & 0x08) != 0
		wp.Increment = (ptcspalb & 0x04) != 0
		sourcePathBytes := int(4 * (ptcspalb & 0x03))
		wp.DestinationKey = packet[3]
		// parse source path address
		if len(packet) < 17 + sourcePathBytes {
			return nil, fmt.Errorf("not a valid RMAP packet on second check (len=%d, spb=%d)", len(packet), sourcePathBytes)
		}
		wp.SourcePath = DecodeSourcePath(packet[4:4+sourcePathBytes])
		remHeader := packet[4+sourcePathBytes:]
		wp.SourceLogicalAddress = remHeader[0]
		wp.TransactionIdentifier = binary.BigEndian.Uint16(remHeader[1:3])
		wp.ExtendedWriteAddress = remHeader[3]
		wp.WriteAddress = binary.BigEndian.Uint32(remHeader[4:8])
		dataLength := Uint24BE(remHeader[8:11])
		headerCRC := remHeader[11]
		computedHeaderCRC := RmapCrc8(packet[0:4+sourcePathBytes+11])
		if headerCRC != computedHeaderCRC {
			return nil, fmt.Errorf("invalid CRC on RMAP header: computed %02x but header states %02x", computedHeaderCRC, headerCRC)
		}
		wp.DataBytes = remHeader[12:len(remHeader)-1]
		if uint32(len(wp.DataBytes)) != dataLength {
			return nil, fmt.Errorf("invalid number of data bytes in packet: header specified %d but packet size implied %d", dataLength, len(wp.DataBytes))
		}
		dataCRC := remHeader[len(remHeader)-1]
		computedDataCRC := RmapCrc8(wp.DataBytes)
		if headerCRC != computedHeaderCRC {
			return nil, fmt.Errorf("invalid CRC on RMAP data: computed %02x but trailer states %02x", computedDataCRC, dataCRC)
		}
		return wp, nil
	} else {
		// TODO
		panic("TODO")
	}
}

func MakeWritePacket() WritePacket {
	// sets relevant defaults
	return WritePacket{
		DestinationPath:           nil,
		DestinationLogicalAddress: 254,
		VerifyData:                false,
		Acknowledge:               true,
		Increment:                 true,
		DestinationKey:            0,
		SourcePath:                nil,
		SourceLogicalAddress:      254,
		TransactionIdentifier:     0,
		ExtendedWriteAddress:      0,
		WriteAddress:              0,
		DataBytes:                 nil,
	}
}

func (wp WritePacket) Encode() ([]byte, error) {
	packet := append([]byte{}, wp.DestinationPath...)
	var ptcspalb uint8 = 0x60
	if wp.VerifyData {
		ptcspalb |= 0x10
	}
	if wp.Acknowledge {
		ptcspalb |= 0x08
	}
	if wp.Increment {
		ptcspalb |= 0x04
	}
	encodedPath, err := EncodeSourcePath(wp.SourcePath)
	if err != nil {
		return nil, err
	}
	if len(encodedPath) % 4 != 0 {
		panic("invalid encoding")
	}
	sourcePathAddrLen := len(encodedPath) / 4
	if sourcePathAddrLen < 0 || sourcePathAddrLen > 3 {
		return nil, fmt.Errorf("source path is too long: %d bytes", len(wp.SourcePath))
	}
	ptcspalb |= uint8(sourcePathAddrLen)
	packet = append(packet, wp.DestinationLogicalAddress, fwmodel.IdentifierRMAP, ptcspalb, wp.DestinationKey)
	packet = append(packet, encodedPath...)
	packet = append(packet, wp.SourceLogicalAddress)
	packet = append(packet, EncodeUint16BE(wp.TransactionIdentifier)...)
	packet = append(packet, wp.ExtendedWriteAddress)
	packet = append(packet, EncodeUint32BE(wp.WriteAddress)...)
	b, err := EncodeUint24BE(uint32(len(wp.DataBytes)))
	if err != nil {
		return nil, err
	}
	packet = append(packet, b...)
	packet = append(packet, RmapCrc8(packet[len(wp.DestinationPath):]))
	packet = append(packet, wp.DataBytes...)
	packet = append(packet, RmapCrc8(wp.DataBytes))
	return packet, nil
}
