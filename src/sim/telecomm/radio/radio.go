package radio

import (
	"encoding/binary"
	"sim/fakewire/fwmodel"
	"sim/fakewire/rmap"
	"sim/model"
	"sim/telecomm"
	"time"
)

const (
	RegMagic     int = 0
	RegTxPtr         = 1
	RegTxLen         = 2
	RegTxState       = 3
	RegRxPtr         = 4
	RegRxLen         = 5
	RegRxPtrAlt      = 6
	RegRxLenAlt      = 7
	RegRxState       = 8
	RegErrCount      = 9
	RegMemBase       = 10
	RegMemSize       = 11
	NumRegisters     = 12
	RegBase          = 0x0000
	MemBase          = 0x1000
)

const (
	StatusOk            = rmap.NoError
	ErrPacketCorrupted  = 0x01
	ErrRegisterReadOnly = 0x02
	ErrInvalidAddress   = 0x03
	ErrValueOutOfRange  = 0x04
)

const (
	MagicNum = 0x7E1ECA11

	TxStateIdle   uint32 = 0x00
	TxStateActive uint32 = 0x01

	RxStateIdle      uint32 = 0x00
	RxStateListening uint32 = 0x01
	RxStateOverflow  uint32 = 0x02
)

type FWRadioConfig struct {
	ByteDuration   time.Duration
	MemorySize     int
	LogicalAddress uint8
	DestinationKey uint8
}

type fwRadio struct {
	ctx          model.SimContext
	ByteDuration time.Duration
	RadioMemory  []byte
	Registers    [NumRegisters]uint32
	Connection   telecomm.Connection

	CurrentTxStart model.VirtualTime
}

func min32(a, b uint32) uint32 {
	if a < b {
		return a
	} else {
		return b
	}
}

func (f *fwRadio) updateTelecommSimulation() {
	// simulate reception
	incoming := f.Connection.PullBytesAvailable()
	if len(incoming) > 0 && f.Registers[RegRxState] == RxStateListening {
		for len(incoming) > 0 {
			rxPtr := f.Registers[RegRxPtr]
			rxLen := f.Registers[RegRxLen]
			if rxLen == 0 || rxPtr >= uint32(len(f.RadioMemory)) {
				// if we're out of space in the main output buffer, see if we have a secondary output buffer
				if f.Registers[RegRxLenAlt] > 0 && f.Registers[RegRxPtrAlt] < uint32(len(f.RadioMemory)) {
					f.Registers[RegRxPtr], f.Registers[RegRxLen] = f.Registers[RegRxPtrAlt], f.Registers[RegRxLenAlt]
					f.Registers[RegRxPtrAlt], f.Registers[RegRxLenAlt] = 0, 0
					continue
				} else {
					break
				}
			}
			countAttempt := min32(uint32(len(incoming)), rxLen)
			countActual := copy(f.RadioMemory[rxPtr:], incoming[:countAttempt])
			f.Registers[RegRxLen] -= uint32(countActual)
			f.Registers[RegRxPtr] += uint32(countActual)
			incoming = incoming[countActual:]
		}
		// see if we ran out of space at the end of it all, and if we did, set the state to reflect that
		if len(incoming) > 0 {
			f.Registers[RegRxState] = RxStateOverflow
		}
	}
	// simulate transmission completion
	if f.Registers[RegTxState] == TxStateActive {
		remainingI, txStartAt := f.Connection.CountTxBytesRemaining()
		remaining := uint32(remainingI)
		if remaining < f.Registers[RegTxLen] {
			difference := f.Registers[RegTxLen] - remaining
			f.Registers[RegTxLen] -= difference
			f.Registers[RegTxPtr] += difference
		}
		f.CurrentTxStart = txStartAt
		if remaining == 0 {
			f.Registers[RegTxState] = TxStateIdle
			f.CurrentTxStart = model.NeverTimeout
		}
	}
}

func (f *fwRadio) reviseTransmission() {
	byteCount := min32(f.Registers[RegTxLen], uint32(len(f.RadioMemory))-f.Registers[RegTxPtr])
	dataBytes := f.RadioMemory[f.Registers[RegTxPtr]:][:byteCount]
	f.Connection.UpdateTransmission(f.CurrentTxStart, dataBytes)
}

func (f *fwRadio) updateRegister(regNum int, newValue uint32) (status uint8) {
	switch regNum {
	case RegMagic:
		return ErrRegisterReadOnly
	case RegTxPtr:
		if newValue > uint32(len(f.RadioMemory)) {
			return ErrValueOutOfRange
		}
		f.Registers[RegTxPtr] = newValue
		if f.Registers[RegTxState] == TxStateActive {
			f.reviseTransmission()
		}
		return StatusOk
	case RegTxLen:
		if newValue > uint32(len(f.RadioMemory)) {
			return ErrValueOutOfRange
		}
		f.Registers[RegTxLen] = newValue
		if f.Registers[RegTxState] == TxStateActive {
			f.reviseTransmission()
		}
		return StatusOk
	case RegTxState:
		switch newValue {
		case TxStateIdle:
			if f.Registers[RegTxState] == TxStateActive {
				f.Registers[RegTxState] = TxStateIdle
				// cancel the rest of the transmission
				f.Connection.UpdateTransmission(f.CurrentTxStart, nil)
				f.CurrentTxStart = model.NeverTimeout
			}
			return StatusOk
		case TxStateActive:
			if f.Registers[RegTxState] == TxStateIdle {
				f.Registers[RegTxState] = TxStateActive
				// start a new transmission
				f.CurrentTxStart = f.ctx.Now()
				f.reviseTransmission()
			}
			return StatusOk
		default:
			return ErrValueOutOfRange
		}
	case RegRxPtr:
		if newValue > uint32(len(f.RadioMemory)) {
			return ErrValueOutOfRange
		}
		f.Registers[RegRxPtr] = newValue
		return StatusOk
	case RegRxLen:
		if newValue > uint32(len(f.RadioMemory)) {
			return ErrValueOutOfRange
		}
		f.Registers[RegRxLen] = newValue
		return StatusOk
	case RegRxPtrAlt:
		if newValue > uint32(len(f.RadioMemory)) {
			return ErrValueOutOfRange
		}
		f.Registers[RegRxPtrAlt] = newValue
		return StatusOk
	case RegRxLenAlt:
		if newValue > uint32(len(f.RadioMemory)) {
			return ErrValueOutOfRange
		}
		f.Registers[RegRxLenAlt] = newValue
		return StatusOk
	case RegRxState:
		if newValue == RxStateIdle || newValue == RxStateListening || newValue == RxStateOverflow {
			f.Registers[RegRxState] = newValue
			return StatusOk
		} else {
			return ErrValueOutOfRange
		}
	case RegErrCount:
		if newValue == 0 {
			f.Registers[RegErrCount] = 0
			return StatusOk
		} else if newValue <= f.Registers[RegErrCount] {
			f.Registers[RegErrCount] -= newValue
			return StatusOk
		} else {
			return ErrValueOutOfRange
		}
	case RegMemBase:
		return ErrRegisterReadOnly
	case RegMemSize:
		return ErrRegisterReadOnly
	default:
		panic("no such register")
	}
}

func rangesOverlap(s1, e1, s2, e2 uint32) bool {
	/*
		Definition rangesOverlap (s1 : nat) e1 s2 e2 : bool :=
		  (s1 <? e1) && (s2 <? e2) &&
		    (if s2 <? s1 then s1 <? e2 else s2 <? e1).
	*/
	if s1 >= e1 || s2 >= e2 {
		return false
	}
	if s2 < s1 {
		return s1 < e2
	} else {
		return s2 < e1
	}
}

func (f *fwRadio) afterUpdateMemoryRange(startAddress, endAddress uint32) {
	if f.Registers[RegTxState] == TxStateActive && rangesOverlap(startAddress, endAddress, f.Registers[RegTxPtr], f.Registers[RegTxPtr]+f.Registers[RegTxLen]) {
		// revise existing transmission
		f.reviseTransmission()
	}
}

func (f *fwRadio) CorruptCommand() {
	f.Registers[RegErrCount] += 1
}

func (f *fwRadio) AttemptWrite(extAddr uint8, writeAddr uint32, increment bool, data []byte) (error uint8) {
	f.updateTelecommSimulation()
	if extAddr != 0 || !increment {
		return ErrInvalidAddress
	} else if int32(writeAddr) >= RegBase && writeAddr+uint32(len(data)) <= RegBase+NumRegisters*4 {
		var regData [NumRegisters * 4]byte
		for i := 0; i < NumRegisters; i++ {
			binary.BigEndian.PutUint32(regData[i*4:], f.Registers[i])
		}
		copy(regData[writeAddr-RegBase:], data)
		for i := 0; i < NumRegisters; i++ {
			if rangesOverlap(uint32(RegBase+i*4), uint32(RegBase+(i+1)*4), writeAddr, writeAddr+uint32(len(data))) {
				status := f.updateRegister(i, binary.BigEndian.Uint32(regData[i*4:]))
				if status != StatusOk {
					return status
				}
			}
		}
		return StatusOk
	} else if writeAddr >= MemBase && writeAddr+uint32(len(data)) <= MemBase+uint32(len(f.RadioMemory)) {
		copy(f.RadioMemory[writeAddr-MemBase:], data)
		f.afterUpdateMemoryRange(writeAddr-MemBase, writeAddr+uint32(len(data))-MemBase)
		return StatusOk
	} else {
		return ErrInvalidAddress
	}
}

func (f *fwRadio) WriteCorrupt(_ uint8, _ uint32, _ bool) (error uint8) {
	f.Registers[RegErrCount] += 1
	return ErrPacketCorrupted
}

func (f *fwRadio) AttemptRead(extAddr uint8, readAddr uint32, increment bool, dataLength uint32) (data []byte, error uint8) {
	f.updateTelecommSimulation()
	if extAddr != 0 || !increment {
		return nil, ErrInvalidAddress
	} else if int32(readAddr) >= RegBase && readAddr+dataLength <= RegBase+NumRegisters*4 {
		var regData [NumRegisters * 4]byte
		for i := 0; i < NumRegisters; i++ {
			binary.BigEndian.PutUint32(regData[i*4:], f.Registers[i])
		}
		return regData[readAddr-RegBase:][:dataLength], StatusOk
	} else if readAddr >= MemBase && readAddr+dataLength <= MemBase+uint32(len(f.RadioMemory)) {
		output := make([]byte, dataLength)
		copy(output, f.RadioMemory[readAddr-MemBase:])
		return output, StatusOk
	} else {
		return nil, ErrInvalidAddress
	}
}

func (frc FWRadioConfig) Construct(ctx model.SimContext, wire fwmodel.PacketWire, tele telecomm.Connection) {
	fr := &fwRadio{
		ctx:          ctx,
		ByteDuration: frc.ByteDuration,
		RadioMemory:  make([]byte, frc.MemorySize),
		Registers:    [NumRegisters]uint32{},
		Connection:   tele,
	}
	fr.Registers[RegMagic] = MagicNum
	fr.Registers[RegTxPtr] = 0
	fr.Registers[RegTxLen] = 0
	fr.Registers[RegTxState] = TxStateIdle
	fr.Registers[RegRxPtr] = 0
	fr.Registers[RegRxLen] = 0
	fr.Registers[RegRxPtrAlt] = 0
	fr.Registers[RegRxLenAlt] = 0
	fr.Registers[RegRxState] = RxStateIdle
	fr.Registers[RegErrCount] = 0
	fr.Registers[RegMemBase] = MemBase
	fr.Registers[RegMemSize] = uint32(frc.MemorySize)

	rmap.PublishLocalDevice(ctx, fr, frc.LogicalAddress, frc.DestinationKey, wire)
	ctx.Later("sim.telecomm.radio.FWRadio/Start", fr.updateTelecommSimulation)
}
