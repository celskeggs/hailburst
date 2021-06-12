package transport

import (
	"bytes"
	"encoding/binary"
	"sim/model"
)

const (
	MagicNumTlm           = 0x7313DA7A // "tele-data"
	CmdReceivedTID        = 0x01000001
	CmdCompletedTID       = 0x01000002
	CmdNotRecognizedTID   = 0x01000003
	TlmDroppedTID         = 0x01000004
	PongTID               = 0x01000005
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

type MagPwrStateChanged struct {
	PowerState bool
}

func (MagPwrStateChanged) Validate() bool {
	return true
}

func DecodeTelemetry(cp *CommPacket) (t Telemetry, timestamp model.VirtualTime, ok bool) {
	if cp.MagicNumber != MagicNumTlm {
		return nil, 0, false
	}
	if cp.CRC != cp.ComputeCRC() {
		return nil, 0, false
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
	default:
		return nil, 0, false
	}
	r := bytes.NewReader(cp.DataBytes)
	if binary.Read(r, binary.BigEndian, t) != nil {
		return nil, 0, false
	}
	if r.Len() != 0 {
		return nil, 0, false
	}
	if !t.Validate() {
		return nil, 0, false
	}
	timestampNs, nsOk := model.FromNanoseconds(cp.Timestamp)
	if !nsOk {
		return nil, 0, false
	}
	return t, timestampNs, true
}
