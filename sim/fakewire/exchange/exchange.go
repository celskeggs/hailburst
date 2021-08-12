package exchange

import (
	"fmt"
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/codec"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/model"
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

type Exchange struct {
	Sim   model.SimContext
	Label string

	State ExchangeState

	SendHandshakeId        uint32
	RecvHandshakeId        uint32
	SendSecondaryHandshake bool

	FctsSent uint32
	FctsRcvd uint32
	PktsSent uint32
	PktsRcvd uint32

	InboundBuffer   []byte
	InboundReadDone bool
	RecvInProgress  bool

	CondNotify *component.EventDispatcher

	TxBusy bool
}

func (ex *Exchange) CheckInvariants() {
	if ex.State < StateConnecting || ex.State > StateOperating {
		log.Panicf("invalid state: %v", ex.State)
	}
	if ex.FctsRcvd < ex.PktsSent || ex.FctsRcvd > ex.PktsSent+1 {
		log.Panicf("invalid pkts_sent=%v fcts_rcvd=%v", ex.PktsSent, ex.FctsRcvd)
	}
}

func (ex *Exchange) Reset() {
	ex.State = StateConnecting

	ex.SendHandshakeId = 0
	ex.RecvHandshakeId = 0
	ex.SendSecondaryHandshake = false

	ex.FctsSent = 0
	ex.FctsRcvd = 0
	ex.PktsSent = 0
	ex.PktsRcvd = 0

	ex.InboundBuffer = nil
	ex.InboundReadDone = false
	ex.RecvInProgress = false

	ex.CheckInvariants()

	ex.CondNotify.DispatchLater()
}

func (ex *Exchange) Debug(explanation string, args ...interface{}) {
	log.Printf("%v [%s] FakeWire: %s", ex.Sim.Now(), ex.Label, fmt.Sprintf(explanation, args...))
}

func (ex *Exchange) ReceiveBytes(bytes []byte) {
	ex.CheckInvariants()

	if ex.State == StateOperating {
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

func (ex *Exchange) ReceiveCtrlChar(symbol codec.ControlChar, param uint32) {
	ex.CheckInvariants()
	symbol.Validate(param)
	if ex.State == StateConnecting {
		switch symbol {
		case codec.ChHandshake1:
			// received primary handshake
			ex.Debug("Received a primary handshake with ID=0x%08x.", param)
			ex.RecvHandshakeId = param
			ex.SendSecondaryHandshake = true
			ex.CondNotify.DispatchLater()
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
			// received secondary handshake
			if param == ex.SendHandshakeId {
				ex.Debug("Received secondary handshake with ID=0x%08x; transitioning to operating mode.", param)
				ex.State = StateOperating
				ex.CondNotify.DispatchLater()
			} else {
				ex.Debug("Received mismatched secondary ID 0x%08x instead of 0x%08x; resetting.", param, ex.SendHandshakeId)
				ex.Reset()
			}
		default:
			ex.Debug("Hit unexpected control character %v while HANDSHAKING; resetting.", symbol)
			ex.Reset()
		}
	} else if ex.State == StateOperating {
		switch symbol {
		case codec.ChHandshake1:
			// abort connection and restart everything; handle primary handshake
			ex.Debug("Received a primary handshake with ID=0x%08x during operating mode; resetting.", param)
			ex.Reset()
			ex.RecvHandshakeId = param
			ex.SendSecondaryHandshake = true
			ex.CondNotify.DispatchLater()
		case codec.ChHandshake2:
			// abort connection and restart everything
			ex.Debug("Received secondary handshake request during operating mode; resetting.")
			ex.Reset()
		case codec.ChStartPacket:
			if ex.FctsSent != ex.PktsRcvd+1 {
				ex.Debug("Received unauthorized start-of-packet (fcts_sent=%v, pkts_rcvd=%v); resetting.",
					ex.FctsSent, ex.PktsRcvd)
				ex.Reset()
			} else {
				if ex.InboundReadDone || ex.RecvInProgress {
					panic("inconsistent state with HasSentFCT")
				}
				ex.RecvInProgress = true
				ex.PktsRcvd += 1
			}
		case codec.ChEndPacket:
			if !ex.RecvInProgress {
				ex.Debug("Hit unexpected end-of-packet before start-of-packet; resetting.")
				ex.Reset()
			} else {
				if ex.InboundReadDone || ex.PktsRcvd != ex.FctsSent {
					log.Panicf("inconsistent state:\n\tRecvInProgress=%v\n\tInboundReadDone=%v\n\tPktsRcvd=%v\n\tFctsSent=%v\n",
						ex.RecvInProgress, ex.InboundReadDone, ex.PktsRcvd, ex.FctsSent)
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
				if !ex.InboundReadDone || ex.PktsRcvd != ex.FctsSent {
					log.Panicf("inconsistent state:\n\tRecvInProgress=%v\n\tInboundReadDone=%v\n\tPktsRcvd=%v\n\tFctsSent=%v\n",
						ex.RecvInProgress, ex.InboundReadDone, ex.PktsRcvd, ex.FctsSent)
				}
				// discard the data in the current packet
				ex.InboundBuffer = nil
			}
		case codec.ChFlowControl:
			if param == ex.FctsRcvd+1 {
				// make sure this FCT matches our send state
				if ex.PktsSent != ex.FctsRcvd {
					ex.Debug("Received incremented FCT(%v) when no packet had been sent (%v, %v); resetting.",
						param, ex.PktsSent, ex.FctsRcvd)
					ex.Reset()
				} else {
					// received FCT; can send another packet
					ex.FctsRcvd = param
					ex.CondNotify.DispatchLater()
				}
			} else if param != ex.FctsRcvd {
				// FCT number should always either stay the same or increment by one
				ex.Debug("Received unexpected FCT(%v) when last count was %v; resetting.", param, ex.FctsRcvd)
				ex.Reset()
			}
		case codec.ChKeepAlive:
			if ex.PktsRcvd != param {
				ex.Debug("KAT mismatch: received %v packets, but supposed to have received %v; resetting.",
					ex.PktsRcvd, param)
				ex.Reset()
			}
		case codec.ChEscapeSym:
			// indicates that an invalid escape sequence was received by the codec
			ex.Debug("Received invalid escape sequence; resetting.")
			ex.Reset()
		default:
			log.Panicf("invalid control character: %v", symbol)
		}
	} else {
		log.Panicf("Invalid state: %v", ex.State)
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
			ex.CheckInvariants()
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

	// Receiver Thread
	component.BuildTwixt(ctx, []model.EventSource{psink, ex.CondNotify}, func(io *component.TwixtIO) {
		ex.CheckInvariants()
		for {
			for ex.State != StateOperating || ex.TxBusy || !psink.CanAcceptPacket() {
				io.Yield()
				ex.CheckInvariants()
			}
			if ex.RecvInProgress || ex.PktsRcvd != ex.FctsSent || ex.InboundReadDone || ex.InboundBuffer != nil {
				log.Panicf("Inconsistent state:\n"+
					"\tState=%v\n\tTxBusy=%v\n\tRecvInProgress=%v\n"+
					"\tPktsRcvd=%v\n\tFctsSent=%v\n\tInboundReadDone=%v\n\tInboundBuffer=%v",
					ex.State, ex.TxBusy, ex.RecvInProgress,
					ex.PktsRcvd, ex.FctsSent, ex.InboundReadDone, ex.InboundBuffer)
			}

			ex.FctsSent += 1
			ex.Transmit(io, lsink, codec.EncodeCtrlChar(codec.ChFlowControl, ex.FctsSent))

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
				ex.CheckInvariants()
			}
			if ex.PktsRcvd != ex.FctsSent {
				panic("should have been reset by this point")
			}
		}
	})
	// Transmitter thread
	component.BuildTwixt(ctx, []model.EventSource{psource, lsink, ex.CondNotify}, func(io *component.TwixtIO) {
		ex.CheckInvariants()
		for {
			// wait until handshake completes, transmit is possible, and a packet is ready to be sent
			for ex.State != StateOperating || ex.PktsSent == ex.FctsRcvd || ex.TxBusy || !psource.HasPacketAvailable() {
				io.Yield()
				ex.CheckInvariants()
			}
			packet := psource.ReceivePacket()
			if ex.PktsSent != ex.FctsRcvd - 1 {
				log.Panicf("invalid combination of pkts_sent and fcts_rcvd at this point: %v, %v",
					ex.PktsSent, ex.FctsRcvd)
			}
			ex.PktsSent += 1
			ex.Transmit(io, lsink, concat(
				codec.EncodeCtrlChar(codec.ChStartPacket, 0),
				codec.EncodeDataBytes(packet),
				codec.EncodeCtrlChar(codec.ChEndPacket, 0),
			))
		}
	})
	// Flow token thread
	component.BuildTwixt(ctx, []model.EventSource{lsink, ex.CondNotify}, func(io *component.TwixtIO) {
		ex.CheckInvariants()
		nextInterval := ctx.Now().Add(ex.HandshakePeriod())
		// requires that TxBusy is false
		sendCtrlChar := func(ctrlChar codec.ControlChar, param uint32) {
			ex.Transmit(io, lsink, concat(
				codec.EncodeCtrlChar(ctrlChar, param),
			))
			nextInterval = ctx.Now().Add(ex.HandshakePeriod())
		}
		for {
			for ex.TxBusy {
				io.Yield()
				ex.CheckInvariants()
			}
			if ex.SendSecondaryHandshake {
				if ex.State != StateConnecting {
					panic("invalid state")
				}
				// if indicated, send secondary handshake
				handshakeId := ex.RecvHandshakeId

				sendCtrlChar(codec.ChHandshake2, handshakeId)

				if !ex.SendSecondaryHandshake {
					ex.Debug("Sent secondary handshake with ID=0x%08x, but request revoked by reset; not transitioning.", handshakeId)
				} else if handshakeId != ex.RecvHandshakeId {
					ex.Debug("Sent secondary handshake with ID=0x%08x, but new primary ID=0x%08x had been received in the meantime; not transitioning.",
						handshakeId, ex.RecvHandshakeId)
				} else if ex.State != StateConnecting {
					ex.Debug("Sent secondary handshake with ID=0x%08x, but state is now %v instead of CONNECTING; not transitioning.",
						handshakeId, ex.State)
				} else {
					ex.Debug("Sent secondary handshake with ID=0x%08x; transitioning to operating mode.", handshakeId)
					ex.State = StateOperating
					ex.SendSecondaryHandshake = false
				}

				ex.CondNotify.DispatchLater()
			} else if ex.State == StateOperating {
				// if we're operating... then we need to send FCTs and KATs on a regular basis
				if ctx.Now().Before(nextInterval) {
					io.YieldUntil(nextInterval)
					ex.CheckInvariants()
					continue
				}
				sendCtrlChar(codec.ChFlowControl, ex.FctsSent)
				sendCtrlChar(codec.ChKeepAlive, ex.PktsSent)
			} else {
				// if we're in the handshaking state... then we need to send primary handshakes on a regular basis
				if ctx.Now().Before(nextInterval) {
					io.YieldUntil(nextInterval)
					ex.CheckInvariants()
					continue
				}
				// pick something very likely to be distinct (Go picks msb unset, C picks msb set)
				handshakeId := 0x7FFFFFFF & ctx.Rand().Uint32()
				ex.SendHandshakeId = handshakeId
				ex.State = StateHandshaking

				sendCtrlChar(codec.ChHandshake1, handshakeId)

				ex.Debug("Sent primary handshake with ID=0x%08x.", handshakeId)
			}
		}
	})
}
