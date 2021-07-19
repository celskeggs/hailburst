package magnetometer

import (
	"encoding/binary"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/rmap"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/verifier/collector"
	"time"
)

type MagneticEnvironment interface {
	MeasureNow() (x, y, z int16)
}

type Config struct {
	MeasurementDelay time.Duration
	LogicalAddress   uint8
	DestinationKey   uint8
}

type magneticDevice struct {
	Context          model.SimContext
	MeasurementDelay time.Duration
	Environment      MagneticEnvironment
	Collector        collector.ActivityCollector

	ErrorCount          uint16
	PowerEnabled        bool
	LatchSet            bool
	CancelLatch         func()
	SnapX, SnapY, SnapZ int16
}

const (
	StatusOk        = rmap.NoError
	ErrNotAligned   = 1
	ErrInvalidAddr  = 2
	ErrInvalidValue = 3
	ErrCorruptData  = 4
)

const (
	RegErrors    = 0
	RegPower     = 1
	RegLatch     = 2
	RegX         = 3
	RegY         = 4
	RegZ         = 5
	NumRegisters = 6
)

func (m *magneticDevice) CorruptCommand() {
	m.ErrorCount += 1
}

func (m *magneticDevice) AttemptWrite(extAddr uint8, writeAddr uint32, increment bool, data []byte) (error uint8) {
	if len(data) != 2 {
		return ErrNotAligned
	}
	if extAddr != 0 {
		return ErrInvalidAddr
	}
	newValue := binary.BigEndian.Uint16(data)
	switch writeAddr {
	case RegErrors:
		if newValue == 0 {
			m.ErrorCount = 0
			return StatusOk
		} else if m.ErrorCount >= newValue {
			m.ErrorCount -= newValue
			return StatusOk
		} else {
			return ErrInvalidValue
		}
	case RegPower:
		if newValue == 0 {
			if m.PowerEnabled {
				m.PowerEnabled = false
				m.LatchSet = false
				if m.CancelLatch != nil {
					m.CancelLatch()
					m.CancelLatch = nil
				}
				m.SnapX, m.SnapY, m.SnapZ = 0, 0, 0
				m.Collector.OnSetMagnetometerPower(false)
			}
			return StatusOk
		} else if newValue == 1 {
			if !m.PowerEnabled {
				m.PowerEnabled = true
				m.Collector.OnSetMagnetometerPower(true)
			}
			return StatusOk
		} else {
			return ErrInvalidValue
		}
	case RegLatch:
		if !m.PowerEnabled {
			// ignore the request
			return StatusOk
		}
		if newValue == 0 {
			if m.LatchSet {
				m.LatchSet = false
				if m.CancelLatch != nil {
					m.CancelLatch()
					m.CancelLatch = nil
				}
			}
			return StatusOk
		} else if newValue == 1 {
			if !m.LatchSet {
				m.LatchSet = true
				latchX, latchY, latchZ := m.Environment.MeasureNow()
				m.Collector.OnMeasureMagnetometer(latchX, latchY, latchZ)
				if m.CancelLatch != nil {
					panic("cancel latch should always be nil when LatchSet is false")
				}
				m.CancelLatch = m.Context.SetTimer(m.Context.Now().Add(m.MeasurementDelay), "sim.spacecraft.magnetometer.MagneticDevice/Latch", func() {
					if m.LatchSet {
						m.LatchSet = false
						m.CancelLatch = nil
						m.SnapX, m.SnapY, m.SnapZ = latchX, latchY, latchZ
					}
				})
			}
			return StatusOk
		} else {
			return ErrInvalidValue
		}
	default:
		return ErrInvalidAddr
	}
}

func (m *magneticDevice) WriteCorrupt(extAddr uint8, writeAddr uint32, increment bool) (error uint8) {
	return ErrCorruptData
}

func encodeBool(b bool) uint16 {
	if b {
		return 1
	} else {
		return 0
	}
}

func (m *magneticDevice) AttemptRead(extAddr uint8, readAddr uint32, increment bool, dataLength uint32) (data []byte, error uint8) {
	if extAddr != 0 {
		return nil, ErrInvalidAddr
	}
	readOffset := readAddr * 2
	var regBytes [NumRegisters * 2]byte
	be := binary.BigEndian
	be.PutUint16(regBytes[2*RegErrors:], m.ErrorCount)
	be.PutUint16(regBytes[2*RegPower:], encodeBool(m.PowerEnabled))
	be.PutUint16(regBytes[2*RegLatch:], encodeBool(m.LatchSet))
	be.PutUint16(regBytes[2*RegX:], uint16(m.SnapX))
	be.PutUint16(regBytes[2*RegY:], uint16(m.SnapY))
	be.PutUint16(regBytes[2*RegZ:], uint16(m.SnapZ))
	rbLen := uint32(len(regBytes))
	if readOffset > rbLen || dataLength > rbLen || readOffset+dataLength > rbLen {
		return nil, ErrInvalidAddr
	}
	return regBytes[readOffset:][:dataLength], StatusOk
}

func (c Config) Construct(ctx model.SimContext, wire fwmodel.PacketWire, me MagneticEnvironment, ac collector.ActivityCollector) {
	rmap.PublishLocalDevice(ctx, &magneticDevice{
		Context:          ctx,
		MeasurementDelay: c.MeasurementDelay,
		Environment:      me,
		Collector:        ac,

		ErrorCount:   0,
		PowerEnabled: false,
		LatchSet:     false,
		CancelLatch:  nil,
		SnapX:        0,
		SnapY:        0,
		SnapZ:        0,
	}, c.LogicalAddress, c.DestinationKey, wire)
}
