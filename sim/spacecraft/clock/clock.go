package clock

import (
	"encoding/binary"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/rmap"
	"github.com/celskeggs/hailburst/sim/model"
)

type Config struct {
	LogicalAddress uint8
	DestinationKey uint8
}

type clockDevice struct {
	Context    model.SimContext
	ErrorCount uint32
}

const (
	StatusOk         = rmap.NoError
	ErrNotAligned    = 1
	ErrInvalidAddr   = 2
	ErrInvalidValue  = 3
	ErrInvalidLength = 4
	ErrCorruptData   = 5
)

const (
	MagicNumber = 0x71CC70CC /* tick-tock */
	RegMagic    = 0x00
	RegClock    = 0x04
	RegErrors   = 0x0C
)

func (m *clockDevice) CorruptCommand() {
	m.ErrorCount += 1
}

func (m *clockDevice) AttemptWrite(extAddr uint8, writeAddr uint32, increment bool, data []byte) (error uint8) {
	if len(data) != 4 {
		return ErrNotAligned
	}
	if extAddr != 0 || writeAddr != RegErrors {
		return ErrInvalidAddr
	}
	newValue := binary.BigEndian.Uint32(data)
	if newValue == 0 {
		m.ErrorCount = 0
		return StatusOk
	} else if m.ErrorCount >= newValue {
		m.ErrorCount -= newValue
		return StatusOk
	} else {
		return ErrInvalidValue
	}
}

func (m *clockDevice) WriteCorrupt(extAddr uint8, writeAddr uint32, increment bool) (error uint8) {
	return ErrCorruptData
}

func (m *clockDevice) AttemptRead(extAddr uint8, readAddr uint32, increment bool, dataLength uint32) (data []byte, error uint8) {
	if extAddr != 0 || readAddr%4 != 0 {
		return nil, ErrInvalidAddr
	}
	var regLen uint32
	var out [8]byte
	switch readAddr {
	case RegMagic:
		regLen = 4
		binary.BigEndian.PutUint32(out[:], MagicNumber)
	case RegClock:
		regLen = 8
		binary.BigEndian.PutUint64(out[:], m.Context.Now().Nanoseconds())
	case RegClock + 4:
		return nil, ErrNotAligned
	case RegErrors:
		regLen = 4
		binary.BigEndian.PutUint32(out[:], m.ErrorCount)
	default:
		return nil, ErrInvalidAddr
	}
	if regLen != dataLength {
		return nil, ErrInvalidLength
	}
	return out[:regLen], StatusOk
}

func (c Config) Construct(ctx model.SimContext, wire fwmodel.PacketWire) {
	rmap.PublishLocalDevice(ctx, &clockDevice{
		Context:    ctx,
		ErrorCount: 0,
	}, c.LogicalAddress, c.DestinationKey, wire)
}
