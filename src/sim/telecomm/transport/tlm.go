package transport

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"sim/model"
)

const (
	MagicNumTlm           = 0x7313DA7A // "tele-data"
	CmdReceivedTID        = 0x01000001
	CmdCompletedTID       = 0x01000002
	CmdNotRecognizedTID   = 0x01000003
	TlmDroppedTID         = 0x01000004
	PongTID               = 0x01000005
	ClockCalibratedTID    = 0x01000006
	MagPwrStateChangedTID = 0x02000001
)

type Telemetry interface {
	Validate() bool
}

type CmdReceived struct {
	OriginalTimestamp uint64
	OriginalCommandId uint32 // the CID
}

func (CmdReceived) Validate() bool {
	return true
}

type CmdCompleted struct {
	OriginalTimestamp uint64
	OriginalCommandId uint32
	Success           bool
}

func (CmdCompleted) Validate() bool {
	return true
}

type CmdNotRecognized struct {
	OriginalTimestamp uint64
	OriginalCommandId uint32
	Length            uint32
}

func (CmdNotRecognized) Validate() bool {
	return true
}

type TlmDropped struct {
	MessagesLost uint32
}

func (TlmDropped) Validate() bool {
	return true
}

type Pong struct {
	PingID uint32
}

func (Pong) Validate() bool {
	return true
}

type ClockCalibrated struct {
	Adjustment int64
}

func (ClockCalibrated) Validate() bool {
	return true
}

type MagPwrStateChanged struct {
	PowerState bool
}

func (MagPwrStateChanged) Validate() bool {
	return true
}

func DecodeTelemetry(cp *CommPacket) (t Telemetry, timestamp model.VirtualTime, err error) {
	if cp.MagicNumber != MagicNumTlm {
		return nil, 0, fmt.Errorf("wrong magic number: %08x instead of %08x", cp.MagicNumber, MagicNumTlm)
	}
	if cp.CRC != cp.ComputeCRC() {
		return nil, 0, fmt.Errorf("wrong CRC32: %08x computed instead of %08x specified", cp.ComputeCRC(), cp.CRC)
	}
	switch cp.CmdTlmId {
	case CmdReceivedTID:
		t = &CmdReceived{}
	case CmdCompletedTID:
		t = &CmdCompleted{}
	case CmdNotRecognizedTID:
		t = &CmdNotRecognized{}
	case TlmDroppedTID:
		t = &TlmDropped{}
	case PongTID:
		t = &Pong{}
	case MagPwrStateChangedTID:
		t = &MagPwrStateChanged{}
	case ClockCalibratedTID:
		t = &ClockCalibrated{}
	default:
		return nil, 0, fmt.Errorf("unrecognized telemetry ID: %08x", cp.CmdTlmId)
	}
	r := bytes.NewReader(cp.DataBytes)
	if err := binary.Read(r, binary.BigEndian, t); err != nil {
		return nil, 0, fmt.Errorf("while decoding %d bytes into ID %08x: %v", len(cp.DataBytes), cp.CmdTlmId, err)
	}
	if r.Len() != 0 {
		return nil, 0, fmt.Errorf("extraneous bytes left over in telemetry packet: %d", r.Len())
	}
	if !t.Validate() {
		return nil, 0, fmt.Errorf("unpacked telemetry failed validation: %v", t)
	}
	timestampNs, nsOk := model.FromNanoseconds(cp.Timestamp)
	if !nsOk {
		return nil, 0, fmt.Errorf("nanoseconds value not unpacked correctly: %v", cp.Timestamp)
	}
	return t, timestampNs, nil
}
