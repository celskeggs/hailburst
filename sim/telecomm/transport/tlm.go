package transport

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"github.com/celskeggs/hailburst/sim/model"
)

const (
	MagicNumTlm           = 0x7313DA7A // "tele-data"
	CmdReceivedTID        = 0x01000001
	CmdCompletedTID       = 0x01000002
	CmdNotRecognizedTID   = 0x01000003
	TlmDroppedTID         = 0x01000004
	PongTID               = 0x01000005
	ClockCalibratedTID    = 0x01000006
	HeartbeatTID          = 0x01000007
	MagPwrStateChangedTID = 0x02000001
	MagReadingsArrayTID   = 0x02000002
)

type BaseTelemetry struct{}

func (bt *BaseTelemetry) Decode(t Telemetry, dataBytes []byte, tlmId uint32) error {
	r := bytes.NewReader(dataBytes)
	if err := binary.Read(r, binary.BigEndian, t); err != nil {
		return fmt.Errorf("while decoding %d bytes into ID %08x: %v", len(dataBytes), tlmId, err)
	}
	if r.Len() != 0 {
		return fmt.Errorf("extraneous bytes left over in telemetry packet: %d", r.Len())
	}
	return nil
}

type Telemetry interface {
	// Decode takes a reference to this interface itself, just to simplify the implementation of BaseTelemetry
	Decode(t Telemetry, dataBytes []byte, tlmId uint32) error
}

type CmdReceived struct {
	BaseTelemetry
	OriginalTimestamp uint64
	OriginalCommandId uint32 // the CID
}

func (c *CmdReceived) String() string {
	return fmt.Sprintf("CmdReceived(OT=%v, OC=0x%08x)",
		model.FromNanosecondsIgnore(c.OriginalTimestamp), c.OriginalCommandId)
}

type CmdCompleted struct {
	BaseTelemetry
	OriginalTimestamp uint64
	OriginalCommandId uint32
	Success           bool
}

func (c *CmdCompleted) String() string {
	var success string
	if c.Success {
		success = "SUCCESS"
	} else {
		success = "FAILURE"
	}
	return fmt.Sprintf("CmdCompleted(OT=%v, OC=0x%08x, %s)",
		model.FromNanosecondsIgnore(c.OriginalTimestamp), c.OriginalCommandId, success)
}

type CmdNotRecognized struct {
	BaseTelemetry
	OriginalTimestamp uint64
	OriginalCommandId uint32
	Length            uint32
}

func (c *CmdNotRecognized) String() string {
	return fmt.Sprintf("CmdNotRecognized(OT=%v, OC=0x%08x, Len=%d)",
		model.FromNanosecondsIgnore(c.OriginalTimestamp), c.OriginalCommandId, c.Length)
}

type TlmDropped struct {
	BaseTelemetry
	MessagesLost uint32
}

func (c *TlmDropped) String() string {
	return fmt.Sprintf("TlmDropped(Messages=%d)", c.MessagesLost)
}

type Pong struct {
	BaseTelemetry
	PingID uint32
}

func (c *Pong) String() string {
	return fmt.Sprintf("Pong(ID=0x%08x)", c.PingID)
}

type ClockCalibrated struct {
	BaseTelemetry
	Adjustment int64
}

func (c *ClockCalibrated) String() string {
	return fmt.Sprintf("ClockCalibrated(Adjustment=%+d)", c.Adjustment)
}

type Heartbeat struct {
	BaseTelemetry
}

func (c *Heartbeat) String() string {
	return "Heartbeat"
}

type MagPwrStateChanged struct {
	BaseTelemetry
	PowerState bool
}

func (c *MagPwrStateChanged) String() string {
	if c.PowerState {
		return "MagPwrStateChanged(OFF -> ON)"
	} else {
		return "MagPwrStateChanged(ON -> OFF)"
	}
}

type MagReading struct {
	ReadingTime      uint64
	MagX, MagY, MagZ int16
}

func (c *MagReading) String() string {
	return fmt.Sprintf("MagReading(T=%v, Mag=<%d,%d,%d>)",
		model.FromNanosecondsIgnore(c.ReadingTime), c.MagX, c.MagY, c.MagZ)
}

type MagReadingsArray struct {
	Readings []MagReading
}

func (c *MagReadingsArray) String() string {
	return fmt.Sprintf("MagReadingsArray[N=%d]", len(c.Readings))
}

func (m *MagReadingsArray) Decode(_ Telemetry, dataBytes []byte, tlmId uint32) error {
	r := bytes.NewReader(dataBytes)
	for r.Len() > 0 {
		var mr MagReading
		if err := binary.Read(r, binary.BigEndian, &mr); err != nil {
			return fmt.Errorf("while decoding %d bytes into array-based ID %08x: %v", len(dataBytes), tlmId, err)
		}
		m.Readings = append(m.Readings, mr)
	}
	if r.Len() != 0 {
		return fmt.Errorf("extraneous bytes left over in array-based telemetry packet: %d", r.Len())
	}
	if len(m.Readings) == 0 {
		return errors.New("cannot have zero entries in downlinked magnetometer readings array")
	}
	return nil
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
	case HeartbeatTID:
		t = &Heartbeat{}
	case MagReadingsArrayTID:
		t = &MagReadingsArray{}
	default:
		return nil, 0, fmt.Errorf("unrecognized telemetry ID: %08x", cp.CmdTlmId)
	}
	if err := t.Decode(t, cp.DataBytes, cp.CmdTlmId); err != nil {
		return nil, 0, err
	}
	timestampNs, nsOk := model.FromNanoseconds(cp.Timestamp)
	if !nsOk {
		return nil, 0, fmt.Errorf("nanoseconds value not unpacked correctly: %v", cp.Timestamp)
	}
	return t, timestampNs, nil
}
