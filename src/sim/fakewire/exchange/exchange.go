package exchange

import (
	"fmt"
	"sim/component"
	"sim/fakewire/fwmodel"
	"sim/model"
	"time"
)

type exchangeState int

// simplified/one-shot version of SpaceWire exchange protocol
const (
	Inactive   exchangeState = 1
	Started    exchangeState = 2
	Connecting exchangeState = 3
	Run        exchangeState = 4
	Erroring   exchangeState = 5
	Errored    exchangeState = 6
)

type exchangeData struct {
	sim   model.SimContext
	state exchangeState

	inboundPacket     []byte
	inboundReady      bool // true if packet present
	notifiedFIFOReady bool

	outboundPacket  []byte
	outboundReady   bool // true if packet present
	remoteFIFOReady bool
	spacingPending  bool // true if a NULL should be sent if there's not another packet to transmit

	lSink   fwmodel.DataSinkFWChar
	lSource fwmodel.DataSourceFWChar
	pSink   packetSink
	pSource packetSource

	pendingSendFCT   bool
	sendNextInitChar model.VirtualTime
	cancelNextTimer  func()
	isRecvESC        bool
	sentAtLeastOneNull bool
}

const TokNull = fwmodel.FWChar(0x1F0)

func (exc *exchangeData) receiveChar() (ch fwmodel.FWChar, any bool) {
	var input [1]fwmodel.FWChar
	if exc.lSource.TryRead(input[:]) != 1 {
		return 0, false
	}
	if !exc.isRecvESC && input[0] == fwmodel.CtrlESC {
		exc.isRecvESC = true
		if exc.lSource.TryRead(input[:]) != 1 {
			return 0, false
		}
	}
	if exc.isRecvESC {
		exc.isRecvESC = false
		if input[0] == fwmodel.CtrlFCT {
			return TokNull, true
		} else {
			fmt.Printf("hit invalid character %v after ESC; treating as parity error\n", input[0])
			return fwmodel.ParityFail, true
		}
	} else {
		return input[0], true
	}
}

func (exc *exchangeData) pump() {
	if exc.pendingSendFCT {
		if exc.lSink.TryWrite([]fwmodel.FWChar{fwmodel.CtrlFCT}) == 1 {
			exc.pendingSendFCT = false
		}
	}
	if exc.state == Inactive {
		// don't send nulls until we receive something
		if _, any := exc.receiveChar(); any {
			exc.sentAtLeastOneNull = false
			exc.state = Started
		}
	}
	if exc.state == Started {
		// sending NULLs
		if exc.sim.Now().AtOrAfter(exc.sendNextInitChar) && !exc.pendingSendFCT {
			// NULL is ESC + FCT...
			count := exc.lSink.TryWrite([]fwmodel.FWChar{fwmodel.CtrlESC, fwmodel.CtrlFCT})
			// if we didn't send the FCT, make sure we send the rest later
			if count == 1 {
				exc.pendingSendFCT = true
			}
			// if we sent anything, delay the time we'll do this again, so that we don't spam the port
			if count > 0 {
				exc.sendNextInitChar = exc.sim.Now().Add(5 * time.Millisecond)
				if exc.cancelNextTimer != nil {
					exc.cancelNextTimer()
				}
				exc.cancelNextTimer = exc.sim.SetTimer(exc.sendNextInitChar, "sim.fakewire.exchange.FakeWireExchange/SendNextInitChar", exc.pump)
				exc.sentAtLeastOneNull = true
			}
		}

		if exc.sentAtLeastOneNull {
			// and waiting to receive at least one NULL
			if ch, any := exc.receiveChar(); any {
				if ch == TokNull {
					// start waiting to receive an FCT
					exc.state = Connecting

					// now... send one FCT followed by one NULL. I know that SpaceWire sends more than one FCT, but one is
					// sufficient for us, and it simplifies distinguishing FCTs for init from buffer space markers
					count := exc.lSink.TryWrite([]fwmodel.FWChar{fwmodel.CtrlFCT, fwmodel.CtrlESC, fwmodel.CtrlFCT})
					if count == 0 || count == 2 {
						exc.pendingSendFCT = true
					}
					if count <= 1 {
						// so that we push the FCT over the next byte boundary if encoded to bits
						exc.spacingPending = true
					}
				} else {
					fmt.Printf("hit unexpected character when looking for a NULL: %v\n", ch)
					exc.state = Erroring
				}
			}
		}
	}
	if exc.state == Connecting {
		// finish sending any follow-up NULLs if possible
		if exc.spacingPending && !exc.pendingSendFCT {
			count := exc.lSink.TryWrite([]fwmodel.FWChar{fwmodel.CtrlESC, fwmodel.CtrlFCT})
			if count >= 1 {
				exc.spacingPending = false
				if count == 1 {
					exc.pendingSendFCT = true
				}
			}
		}

		// waiting to receive one FCT (but accepting extra NULLs)
		if ch, any := exc.receiveChar(); any {
			if ch == TokNull {
				// that's fine; discard it
			} else if ch == fwmodel.CtrlFCT {
				exc.state = Run
				exc.remoteFIFOReady = true
				exc.notifiedFIFOReady = true
			} else {
				fmt.Printf("hit unexpected character when looking for a NULL or FCT: %v\n", ch)
				exc.state = Erroring
			}
		}
	}
	if exc.state == Run {
		// okay! nominal situation. now, let's see what we need to do.
		// fmt.Printf("%p: PUMP.RUN (S/O=%v)\n", exc, exc.spacingPending)

		// first -- do we need to send anything?
		if exc.outboundReady && exc.remoteFIFOReady && !exc.pendingSendFCT {
			early := false
			for i, b := range exc.outboundPacket {
				if exc.lSink.TryWrite([]fwmodel.FWChar{fwmodel.DataChar(b)}) != 1 {
					// can't send this one... let's stop here
					if i > 0 {
						exc.outboundPacket = exc.outboundPacket[i:]
					}
					early = true
					break
				}
			}
			if !early {
				exc.outboundPacket = nil
			}
			// if we sent everything but the EOP... send the EOP!
			if len(exc.outboundPacket) == 0 {
				count := exc.lSink.TryWrite([]fwmodel.FWChar{fwmodel.CtrlEOP, fwmodel.CtrlESC, fwmodel.CtrlFCT})
				if count >= 1 {
					// fmt.Printf("%p: SINK OUT FINISH: %d\n", exc, count)
					exc.remoteFIFOReady = false
					exc.outboundReady = false
					exc.pSink.DispatchLater()
					if count == 1 {
						exc.spacingPending = true
					} else if count == 2 {
						exc.pendingSendFCT = true
					}
				}
			}
		} else if exc.spacingPending && !exc.pendingSendFCT {
			count := exc.lSink.TryWrite([]fwmodel.FWChar{fwmodel.CtrlESC, fwmodel.CtrlFCT})
			// fmt.Printf("%p: SINK OUT FOLLOW-UP: %d\n", exc, count)
			if count >= 1 {
				exc.spacingPending = false
				if count == 1 {
					exc.pendingSendFCT = true
				}
			}
		}

		// second -- can we receive anything?

		// consistency check: we should never tell the other side that we're ready to receive a packet if we have one
		// in the inbound buffer
		if exc.inboundReady && exc.notifiedFIFOReady {
			panic("local behavior is incorrect: inboundReady==true when notifiedFIFOReady==true")
		}

		for exc.state == Run {
			ch, any := exc.receiveChar()
			if !any {
				break
			}
			if ch == TokNull {
				// discard; always okay to have a NULL
			} else if ch == fwmodel.ParityFail {
				fmt.Printf("Error condition: parity on incoming line failed\n")
				exc.state = Erroring
			} else if ch == fwmodel.CtrlFCT {
				if exc.remoteFIFOReady {
					fmt.Printf("Error condition: received FCT when remote FIFO already known to be ready\n")
					exc.state = Erroring
				} else {
					exc.remoteFIFOReady = true
				}
			} else if !exc.notifiedFIFOReady {
				fmt.Printf("Error condition: received char %v before we notified remote that we were ready\n", ch)
				exc.state = Erroring
			} else if exc.inboundReady {
				panic("inconsistent local state, as above")
			} else if ch == fwmodel.CtrlEEP {
				// discard packet
				exc.inboundPacket = nil
				exc.notifiedFIFOReady = false
			} else if ch == fwmodel.CtrlEOP {
				// make packet available to reader
				exc.inboundReady = true
				exc.notifiedFIFOReady = false
				exc.pSource.DispatchLater()
			} else if chdata, isData := ch.Data(); isData {
				exc.inboundPacket = append(exc.inboundPacket, chdata)
			} else {
				fmt.Printf("Error condition: unexpected control character %v\n", ch)
				exc.state = Erroring
			}
		}

		// unlike SpaceWire, we send one FCT per packet... not per 8 N-Chars
		if exc.state == Run && !exc.inboundReady && !exc.notifiedFIFOReady && !exc.pendingSendFCT {
			// add another NULL afterwards so that we push the output over the edge of a byte boundary
			count := exc.lSink.TryWrite([]fwmodel.FWChar{fwmodel.CtrlFCT, fwmodel.CtrlESC, fwmodel.CtrlFCT})
			if count == 0 || count == 2 {
				exc.pendingSendFCT = true
			}
			if count <= 1 {
				exc.spacingPending = true
			}
			exc.notifiedFIFOReady = true
		}
	}
	if exc.state == Erroring && !exc.pendingSendFCT {
		// abort link
		if exc.lSink.TryWrite([]fwmodel.FWChar{fwmodel.ParityFail}) == 1 {
			exc.state = Errored
		}
	}
	if exc.state == Errored {
		// nothing to do!
	}
}

type packetSink struct {
	*component.EventDispatcher
	exc *exchangeData
}

func (p packetSink) CanAcceptPacket() bool {
	return !p.exc.outboundReady
}

func (p packetSink) SendPacket(packetData []byte) {
	if p.exc.outboundReady {
		panic("packet already outbound")
	}
	p.exc.outboundPacket = packetData
	p.exc.outboundReady = true
	p.exc.pump()
}

type packetSource struct {
	*component.EventDispatcher
	exc *exchangeData
}

func (p packetSource) HasPacketAvailable() bool {
	return p.exc.inboundReady
}

func (p packetSource) ReceivePacket() []byte {
	if !p.exc.inboundReady {
		panic("packet not inbound")
	}
	out := p.exc.inboundPacket
	p.exc.inboundReady = false
	p.exc.inboundPacket = nil
	p.exc.pump()
	return out
}

func FakeWireExchange(ctx model.SimContext, sink fwmodel.DataSinkFWChar, source fwmodel.DataSourceFWChar, waitForNulls bool) (fwmodel.PacketSink, fwmodel.PacketSource) {
	exc := &exchangeData{
		sim:           ctx,
		state:         Started,
		inboundReady:  false,
		outboundReady: false,
		lSink:         sink,
		lSource:       source,
		pSink: packetSink{
			EventDispatcher: component.MakeEventDispatcher(ctx, "sim.fakewire.exchange.FakeWireExchange/Sink"),
		},
		pSource: packetSource{
			EventDispatcher: component.MakeEventDispatcher(ctx, "sim.fakewire.exchange.FakeWireExchange/Source"),
		},
		sendNextInitChar: model.TimeZero,
	}
	exc.pSink.exc = exc
	exc.pSource.exc = exc

	if waitForNulls {
		// don't send anything until we receive at least one character
		exc.state = Inactive
	}

	sink.Subscribe(exc.pump)
	source.Subscribe(exc.pump)
	ctx.Later("sim.fakewire.exchange.FakeWireExchange/Start", exc.pump)

	return exc.pSink, exc.pSource
}
