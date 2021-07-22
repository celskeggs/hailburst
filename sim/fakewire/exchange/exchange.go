package exchange

import (
	"encoding/binary"
	"fmt"
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/codec"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/util"
	"log"
	"time"
)

type RecvIdState uint8

const (
	RecvIdIdle RecvIdState = iota
	RecvIdPrimary
	RecvIdSecondary
)

type U32Receiver struct {
	Data []byte
}

func (u *U32Receiver) Reset() {
	u.Data = nil
}

func (u *U32Receiver) Put(d []byte) (ok bool) {
	if len(u.Data) > 4 {
		panic("invalid state")
	}
	if len(d)+len(u.Data) > 4 {
		return false
	}
	u.Data = append(u.Data, d...)
	return true
}

func (u *U32Receiver) Done() bool {
	return len(u.Data) == 4
}

func (u *U32Receiver) Decode() uint32 {
	if !u.Done() {
		panic("invalid state")
	}
	return binary.BigEndian.Uint32(u.Data)
}

type Exchange struct {
	Sim   model.SimContext
	Label string

	IsHandshaking bool // main exchange state; indicates Handshaking state as opposed to Operating state
	RecvId        RecvIdState
	PrimaryId     U32Receiver
	SecondaryId   U32Receiver

	SentPrimaryHandshake        bool
	SentPrimaryId               uint32
	NeedsSendSecondaryHandshake bool

	InboundBuffer   []byte
	InboundReadDone bool
	HasSentFCT      bool
	RemoteSentFCT   bool
	RecvInProgress  bool

	CondNotify *component.EventDispatcher

	TxBusy bool
}

func (ex *Exchange) Reset() {
	ex.IsHandshaking = true

	ex.SentPrimaryHandshake = false
	ex.NeedsSendSecondaryHandshake = false
	ex.PrimaryId.Reset()
	ex.SecondaryId.Reset()
	ex.SentPrimaryId = 0
	ex.RecvId = RecvIdIdle

	ex.InboundBuffer = nil
	ex.InboundReadDone = false
	ex.HasSentFCT = false
	ex.RemoteSentFCT = false
	ex.RecvInProgress = false

	ex.CondNotify.DispatchLater()
}

func (ex *Exchange) Debug(explanation string, args ...interface{}) {
	log.Printf("%v [%s] FakeWire: %s", ex.Sim.Now(), ex.Label, fmt.Sprintf(explanation, args...))
}

func (ex *Exchange) ReceiveBytes(bytes []byte) {
	if ex.RecvId != RecvIdIdle {
		// receive bytes for handshake ID
		var id *U32Receiver
		if ex.RecvId == RecvIdPrimary {
			id = &ex.PrimaryId
			if !ex.IsHandshaking {
				panic("cannot be receiving primary handshake ID in operating mode")
			}
		} else if ex.RecvId == RecvIdSecondary {
			id = &ex.SecondaryId
		} else {
			panic("invalid RecvId state")
		}
		if !id.Put(bytes) {
			ex.Debug("Received too many data characters during handshake; resetting.")
			ex.Reset()
		} else if id.Done() {
			if ex.RecvId == RecvIdPrimary {
				// primary handshake received
				if ex.SentPrimaryHandshake && ex.PrimaryId.Decode() == ex.SentPrimaryId {
					ex.Debug("Received the same handshake ID that we sent: 0x%08x; ignoring. (Is there a loop?)",
						ex.PrimaryId.Decode())
				} else {
					ex.Debug("Received primary handshake with ID=0x%08x.", ex.PrimaryId.Decode())
					ex.NeedsSendSecondaryHandshake = true
					ex.CondNotify.DispatchLater()
				}
			} else if !ex.SentPrimaryHandshake {
				ex.Debug("Received secondary handshake with ID=0x%08x when primary had not been sent; resetting.", ex.SecondaryId.Decode())
				ex.Reset()
			} else if ex.SentPrimaryId != ex.SecondaryId.Decode() {
				ex.Debug("Received mismatched secondary ID 0x%08x instead of 0x%08x; resetting.",
					ex.SecondaryId.Decode(), ex.SentPrimaryId)
				ex.Reset()
			} else if ex.IsHandshaking {
				ex.Debug("Received secondary handshake with ID=0x%08x; transitioning to operating mode.",
					ex.SecondaryId.Decode())
				ex.IsHandshaking = false
				ex.CondNotify.DispatchLater()
			} else {
				ex.Debug("Received unnecessary secondary handshake with ID=0x%08x; ignoring.",
					ex.SecondaryId.Decode())
			}
			ex.RecvId = RecvIdIdle
		}
	} else if ex.IsHandshaking {
		ex.Debug("Received unexpected data bytes during handshake; resetting.")
		ex.Reset()
	} else {
		// receive packet bytes during operating mode
		if !ex.RecvInProgress {
			ex.Debug("Hit unexpected data character 0x%x before start-of-packet; resetting.", bytes[0])
			ex.Reset()
		} else if ex.InboundReadDone {
			panic("inconsistent state")
		}
		ex.InboundBuffer = append(ex.InboundBuffer, bytes...)
	}
}

func (ex *Exchange) ReceiveCtrlChar(symbol codec.ControlChar) {
	if ex.RecvId != RecvIdIdle && symbol != codec.ChHandshake1 {
		ex.Debug("Hit unexpected control character %v while waiting for handshake ID; resetting.", symbol)
		ex.Reset()
	} else if ex.IsHandshaking {
		switch symbol {
		case codec.ChHandshake1:
			// need to receive handshake ID next
			ex.RecvId = RecvIdPrimary
			ex.PrimaryId.Reset()
		case codec.ChHandshake2:
			// need to receive handshake ID next
			ex.RecvId = RecvIdSecondary
			ex.SecondaryId.Reset()
		default:
			ex.Debug("Hit unexpected control character %v during handshake; resetting.", symbol)
			ex.Reset()
		}
	} else {
		// operating mode
		switch symbol {
		case codec.ChHandshake1:
			// abort connection and restart everything
			ex.Debug("Received handshake request during operating mode; resetting.")
			ex.Reset()
			ex.RecvId = RecvIdPrimary
			ex.PrimaryId.Reset()
		case codec.ChHandshake2:
			if ex.SentPrimaryHandshake {
				// late secondary handshake, likely because handshakes crossed in flight
				ex.RecvId = RecvIdSecondary
				ex.SecondaryId.Reset()
			} else {
				// abort connection and restart everything
				ex.Debug("Received unexpected secondary handshake during operating mode; resetting.")
				ex.Reset()
			}
		case codec.ChStartPacket:
			if !ex.HasSentFCT {
				ex.Debug("Received unauthorized start-of-packet; resetting.")
				ex.Reset()
			} else {
				if ex.InboundReadDone || ex.RecvInProgress {
					panic("inconsistent state with HasSentFCT")
				}
				ex.HasSentFCT = false
				ex.RecvInProgress = true
			}
		case codec.ChEndPacket:
			if !ex.RecvInProgress {
				ex.Debug("Hit unexpected end-of-packet before start-of-packet; resetting.")
				ex.Reset()
			} else {
				if ex.InboundReadDone || ex.HasSentFCT {
					log.Panicf("inconsistent state: {RecvInProgress=%v, InboundReadDone=%v, HasSentFCT=%v}",
						ex.RecvInProgress, ex.InboundReadDone, ex.HasSentFCT)
				}
				ex.InboundReadDone = true
				ex.RecvInProgress = false
				ex.CondNotify.DispatchLater()
			}
		case codec.ChErrorPacket:
			if !ex.RecvInProgress {
				ex.Debug("Hit unexpected error-end-of-packet before start-of-packet; resetting.")
				ex.Reset()
			} else {
				if !ex.InboundReadDone || ex.HasSentFCT {
					panic("inconsistent state with RecvInProgress")
				}
				// discard the data in the current packet
				ex.InboundBuffer = nil
			}
		case codec.ChFlowControl:
			if ex.RemoteSentFCT {
				ex.Debug("Received duplicate FCT; resetting.")
				ex.Reset()
			} else {
				ex.RemoteSentFCT = true
				ex.CondNotify.DispatchLater()
			}
		case codec.ChEscapeSym:
			// indicates that an invalid escape sequence was received by the codec
			ex.Debug("Received invalid escape sequence; resetting.")
			ex.Reset()
		default:
			panic(fmt.Sprintf("invalid control character: %v", symbol))
		}
	}
}

// Transmit requires that TxBusy is false.
func (ex *Exchange) Transmit(io *component.TwixtIO, sink model.DataSinkBytes, data []byte) {
	if ex.TxBusy {
		panic("Transmit contract requires TxBusy=false on entry")
	}
	ex.TxBusy = true
	for len(data) > 0 {
		count := sink.TryWrite(data)
		data = data[count:]
		if len(data) > 0 {
			io.Yield()
		}
	}
	if !ex.TxBusy {
		panic("inconsistent: TxBusy should still be set")
	}
	ex.TxBusy = false
	ex.CondNotify.DispatchLater()
}

func concat(allData ...[]byte) (total []byte) {
	for _, data := range allData {
		if len(total) == 0 {
			total = data
		} else if len(data) > 0 {
			total = append(total, data...)
		}
	}
	return total
}

func (ex *Exchange) HandshakePeriod() time.Duration {
	fiveMsInNs := (5 * time.Millisecond).Nanoseconds()
	return time.Duration(ex.Sim.Rand().Int63n(fiveMsInNs)+fiveMsInNs) * time.Nanosecond
}

func FakeWire(ctx model.SimContext, lsink model.DataSinkBytes, lsource model.DataSourceBytes, psink fwmodel.PacketSink, psource fwmodel.PacketSource, label string) {
	// data receiver thread

	ex := &Exchange{
		Sim:        ctx,
		Label:      label,
		CondNotify: component.MakeEventDispatcher(ctx, "sim.fakewire.exchange.FakeWire/CondNotify"),
	}
	ex.Reset()

	// connect a Decoder to receive all data from the link source
	decoder := codec.MakeDecoder(ex.ReceiveBytes, ex.ReceiveCtrlChar)
	component.DataPumpDirect(ctx, lsource, decoder.Decode)

	component.BuildTwixt(ctx, []model.EventSource{psink, ex.CondNotify}, func(io *component.TwixtIO) {
		for {
			for ex.IsHandshaking || ex.TxBusy {
				io.Yield()
			}
			if ex.RecvInProgress || ex.HasSentFCT || ex.InboundReadDone || ex.InboundBuffer != nil {
				panic("inconsistent state")
			}

			ex.HasSentFCT = true

			ex.Transmit(io, lsink, codec.EncodeCtrlChar(codec.ChFlowControl))

			for !ex.IsHandshaking {
				if ex.InboundReadDone && psink.CanAcceptPacket() {
					// if nil, this means that the packet is empty... but not everything will handle a nil packet
					// correctly, so make sure it's not sent as a nil
					if ex.InboundBuffer == nil {
						ex.InboundBuffer = make([]byte, 0)
					}
					psink.SendPacket(ex.InboundBuffer)
					ex.InboundReadDone = false
					ex.InboundBuffer = nil

					break
				}
				io.Yield()
			}
			if ex.HasSentFCT {
				panic("should have been reset by this point")
			}
		}
	})
	component.BuildTwixt(ctx, []model.EventSource{psource, lsink, ex.CondNotify}, func(io *component.TwixtIO) {
		for {
			// wait until handshake completes, transmit is possible, and a packet is ready to be sent
			for ex.IsHandshaking || !ex.RemoteSentFCT || ex.TxBusy || !psource.HasPacketAvailable() {
				//ex.Debug("Sleeping on no transmission: IsHandshaking=%v RemoteSentFCT=%v TxBusy=%v HasPacketAvailable=%v",
				//	ex.IsHandshaking, ex.RemoteSentFCT, ex.TxBusy, psource.HasPacketAvailable())
				io.Yield()
			}
			packet := psource.ReceivePacket()
			ex.RemoteSentFCT = false
			ex.Transmit(io, lsink, concat(
				codec.EncodeCtrlChar(codec.ChStartPacket),
				codec.EncodeDataBytes(packet),
				codec.EncodeCtrlChar(codec.ChEndPacket),
			))
		}
	})
	component.BuildTwixt(ctx, []model.EventSource{lsink, ex.CondNotify}, func(io *component.TwixtIO) {
		nextHandshake := ctx.Now().Add(ex.HandshakePeriod())
		// requires that TxBusy is false
		sendHandshake := func(handshake codec.ControlChar, handshakeId uint32) {
			ex.Transmit(io, lsink, concat(
				codec.EncodeCtrlChar(handshake),
				codec.EncodeDataBytes(util.EncodeUint32BE(handshakeId)),
			))
			nextHandshake = ctx.Now().Add(ex.HandshakePeriod())
		}
		for {
			for !ex.IsHandshaking || ex.TxBusy {
				io.Yield()
			}
			if ex.NeedsSendSecondaryHandshake {
				// if indicated, send secondary handshake
				ex.NeedsSendSecondaryHandshake = false
				handshakeId := ex.PrimaryId.Decode()

				sendHandshake(codec.ChHandshake2, handshakeId)

				ex.Debug("Sent secondary handshake with ID=0x%08x; transitioning to operating mode.", handshakeId)
				ex.IsHandshaking = false
				ex.CondNotify.DispatchLater()
			} else {
				// if we're in the handshaking state... then we need to send primary handshakes on a regular basis
				if ctx.Now().Before(nextHandshake) {
					io.YieldUntil(nextHandshake)
					continue
				}
				// pick something very likely to be distinct (Go picks msb unset, C picks msb set)
				handshakeId := 0x7FFFFFFF & ctx.Rand().Uint32()
				ex.SentPrimaryId = handshakeId
				ex.SentPrimaryHandshake = true

				sendHandshake(codec.ChHandshake1, handshakeId)

				ex.Debug("Sent primary handshake with ID=0x%08x.", handshakeId)
			}
		}
	})
}
