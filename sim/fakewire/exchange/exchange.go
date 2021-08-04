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

type ExchangeState uint8

const (
	StateInvalid = iota
	StateConnecting
	StateHandshaking
	StateOperating
)

func (s ExchangeState) String() string {
	switch s {
	case StateConnecting:
		return "CONNECTING"
	case StateHandshaking:
		return "HANDSHAKING"
	case StateOperating:
		return "OPERATING"
	default:
		return fmt.Sprintf("[UNKNOWN=%d]", uint8(s))
	}
}

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

	State ExchangeState

	SendHandshakeId        uint32
	IsReceivingHandshake   bool
	RecvHandshakeId        U32Receiver
	SendSecondaryHandshake bool

	InboundBuffer   []byte
	InboundReadDone bool
	HasSentFCT      bool
	RemoteSentFCT   bool
	RecvInProgress  bool

	CondNotify *component.EventDispatcher

	TxBusy bool
}

func (ex *Exchange) Reset() {
	ex.State = StateConnecting

	ex.SendHandshakeId = 0
	ex.IsReceivingHandshake = false
	ex.RecvHandshakeId.Reset()
	ex.SendSecondaryHandshake = false

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
	if ex.IsReceivingHandshake {
		// receive bytes for handshake ID
		if !ex.RecvHandshakeId.Put(bytes) {
			ex.Debug("Received too many data characters during handshake; resetting.")
			ex.Reset()
		} else if ex.RecvHandshakeId.Done() {
			ex.IsReceivingHandshake = false
			if ex.State == StateConnecting {
				ex.Debug("Received a primary handshake with ID=0x%08x.", ex.RecvHandshakeId.Decode())
				ex.SendSecondaryHandshake = true
				ex.CondNotify.DispatchLater()
			} else if ex.State == StateHandshaking {
				if ex.RecvHandshakeId.Decode() == ex.SendHandshakeId {
					ex.Debug("Received secondary handshake with ID=0x%08x; transitioning to operating mode.",
						ex.RecvHandshakeId.Decode())
					ex.State = StateOperating
					ex.CondNotify.DispatchLater()
				} else {
					ex.Debug("Received mismatched secondary ID 0x%08x instead of 0x%08x; resetting.",
						ex.RecvHandshakeId.Decode(), ex.SendHandshakeId)
					ex.Reset()
				}
			} else {
				panic(fmt.Sprintf("invalid state: %v", ex.State))
			}
		}
	} else if ex.State == StateOperating {
		// receive packet bytes during operating mode
		if !ex.RecvInProgress {
			ex.Debug("Hit unexpected data character 0x%x before start-of-packet; resetting.", bytes[0])
			ex.Reset()
		} else if ex.InboundReadDone {
			panic("inconsistent state")
		}
		ex.InboundBuffer = append(ex.InboundBuffer, bytes...)
	} else {
		ex.Debug("Received unexpected data character 0x%x during handshake mode %v; resetting.", bytes[0], ex.State)
		ex.Reset()
	}
}

func (ex *Exchange) ReceiveCtrlChar(symbol codec.ControlChar) {
	if ex.IsReceivingHandshake {
		ex.Debug("Hit unexpected control character %v while waiting for handshake ID; resetting.", symbol)
		ex.Reset()
	} else if ex.State == StateConnecting {
		switch symbol {
		case codec.ChHandshake1:
			// need to receive handshake ID next
			ex.IsReceivingHandshake = true
			ex.RecvHandshakeId.Reset()
			// abort sending a secondary handshake, in case we're already there
			ex.SendSecondaryHandshake = false
		case codec.ChHandshake2:
			ex.Debug("Received unexpected secondary handshake when no primary handshake had been sent; resetting.")
			ex.Reset()
		default:
			ex.Debug("Hit unexpected control character %v while CONNECTING; resetting.", symbol)
			ex.Reset()
		}
	} else if ex.State == StateHandshaking {
		switch symbol {
		case codec.ChHandshake1:
			ex.Debug("Received primary handshake collision while handshaking; resetting.")
			ex.Reset()
		case codec.ChHandshake2:
			// need to receive handshake ID next
			ex.IsReceivingHandshake = true
			ex.RecvHandshakeId.Reset()
		default:
			ex.Debug("Hit unexpected control character %v while HANDSHAKING; resetting.", symbol)
			ex.Reset()
		}
	} else if ex.State == StateOperating {
		switch symbol {
		case codec.ChHandshake1:
			// abort connection and restart everything
			ex.Debug("Received primary handshake request during operating mode; resetting.")
			ex.Reset()
			ex.IsReceivingHandshake = true
			ex.RecvHandshakeId.Reset()
		case codec.ChHandshake2:
			// abort connection and restart everything
			ex.Debug("Received secondary handshake request during operating mode; resetting.")
			ex.Reset()
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
	} else {
		panic(fmt.Sprintf("Invalid state: %v", ex.State))
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

// HandshakePeriod generates a random interval in the range [3ms, 10ms)
func (ex *Exchange) HandshakePeriod() time.Duration {
	msInNs := time.Millisecond.Nanoseconds()
	return time.Duration(ex.Sim.Rand().Int63n(7*msInNs)+3*msInNs) * time.Nanosecond
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
			for ex.State != StateOperating || ex.TxBusy || !psink.CanAcceptPacket() {
				io.Yield()
			}
			if ex.RecvInProgress || ex.HasSentFCT || ex.InboundReadDone || ex.InboundBuffer != nil {
				panic("inconsistent state")
			}

			ex.HasSentFCT = true

			ex.Transmit(io, lsink, codec.EncodeCtrlChar(codec.ChFlowControl))

			for ex.State == StateOperating {
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
			for ex.State != StateOperating || !ex.RemoteSentFCT || ex.TxBusy || !psource.HasPacketAvailable() {
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
			for ex.State == StateOperating || ex.TxBusy {
				io.Yield()
			}
			if ex.SendSecondaryHandshake {
				if ex.State != StateConnecting {
					panic("invalid state")
				}
				// if indicated, send secondary handshake
				handshakeId := ex.RecvHandshakeId.Decode()

				sendHandshake(codec.ChHandshake2, handshakeId)

				if !ex.SendSecondaryHandshake {
					ex.Debug("Sent secondary handshake with ID=0x%08x, but request revoked by reset; not transitioning.", handshakeId)
				} else if handshakeId != ex.RecvHandshakeId.Decode() {
					ex.Debug("Sent secondary handshake with ID=0x%08x, but new primary ID=0x%08x had been received in the meantime; not transitioning.",
						handshakeId, ex.RecvHandshakeId.Decode())
				} else if ex.State != StateConnecting {
					ex.Debug("Sent secondary handshake with ID=0x%08x, but state is now %v instead of CONNECTING; not transitioning.",
						handshakeId, ex.State)
				} else {
					ex.Debug("Sent secondary handshake with ID=0x%08x; transitioning to operating mode.", handshakeId)
					ex.State = StateOperating
					ex.SendSecondaryHandshake = false
				}

				ex.CondNotify.DispatchLater()
			} else {
				// if we're in the handshaking state... then we need to send primary handshakes on a regular basis
				if ctx.Now().Before(nextHandshake) {
					io.YieldUntil(nextHandshake)
					continue
				}
				// pick something very likely to be distinct (Go picks msb unset, C picks msb set)
				handshakeId := 0x7FFFFFFF & ctx.Rand().Uint32()
				ex.SendHandshakeId = handshakeId
				ex.State = StateHandshaking

				sendHandshake(codec.ChHandshake1, handshakeId)

				ex.Debug("Sent primary handshake with ID=0x%08x.", handshakeId)
			}
		}
	})
}
