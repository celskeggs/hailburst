package transport

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math/rand"
)

const (
	MagicNumCmd       = 0x73133C2C // "tele-exec"
	PingCID           = 0x01000001
	MagSetPwrStateCID = 0x02000001
)

type Command interface {
	CmdId() uint32
}

type Ping struct {
	PingID uint32
}

func (c Ping) String() string {
	return fmt.Sprintf("Ping(ID=0x%08x)", c.PingID)
}

func (Ping) CmdId() uint32 {
	return PingCID
}

type MagSetPwrState struct {
	PowerState bool
}

func (c MagSetPwrState) String() string {
	if c.PowerState {
		return "MagSetPwrState(ON)"
	} else {
		return "MagSetPwrState(OFF)"
	}
}

func (MagSetPwrState) CmdId() uint32 {
	return MagSetPwrStateCID
}

func GenerateCmd(r *rand.Rand) Command {
	switch r.Intn(2) {
	case 0:
		return Ping{
			PingID: r.Uint32(),
		}
	case 1:
		return MagSetPwrState{
			PowerState: r.Intn(2) == 0,
		}
	default:
		panic("invalid result")
	}
}

func EncodeCommand(cmd Command, timestamp uint64) *CommPacket {
	pkt := &CommPacket{
		MagicNumber: MagicNumCmd,
		CmdTlmId:    cmd.CmdId(),
		Timestamp:   timestamp,
	}
	var buf bytes.Buffer
	if binary.Write(&buf, binary.BigEndian, cmd) != nil {
		panic("unexpected error")
	}
	pkt.DataBytes = buf.Bytes()
	pkt.CRC = pkt.ComputeCRC()
	return pkt
}
