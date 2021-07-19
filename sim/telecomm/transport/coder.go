package transport

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/telecomm"
)

func AttachReceiver(ctx model.SimContext, source *telecomm.Connection, callback func(packet *CommPacket), onErrors func(int)) {
	var cancelTimer func()
	var recomputeReception func()
	recomputeReception = func() {
		if cancelTimer != nil {
			cancelTimer()
			cancelTimer = nil
		}
		for {
			packet, bytesUsed, errors := DecodeCommPacket(source.PeekAllBytes())
			if bytesUsed == 0 {
				// nothing to do... we won't have any steps to take until more bytes are transmitted.
				return
			}
			receiveAt := source.LookupEndTime(bytesUsed - 1)
			if packet != nil {
				// if we were able to parse a packet, we need to either take it or leave it
				if ctx.Now().AtOrAfter(receiveAt) {
					// if all the bytes we need are fixed into place, take it!
					source.ConsumeBytesUpTo(receiveAt, bytesUsed)
					callback(packet)
					if errors > 0 {
						onErrors(errors)
					}
					// and see if we can decode any more packets
					continue
				}
				// otherwise, the packet isn't complete yet, and we'll have to wait.
			} else {
				// if we weren't able to parse a packet, but we still knew that some number of bytes were erroneous, we
				// can get rid of however many of them have actually been received so far.
				if bytesUsed != errors {
					panic("coherency error... if we're throwing all the bytes away, they should all be errors!")
				}
				pulled := len(source.PullBytesAvailable())
				if pulled > bytesUsed {
					panic("too many bytes pulled!")
				}
				onErrors(pulled)
			}
			// set a timer, so that we'll remember to handle the future bytes
			cancelTimer = ctx.SetTimer(receiveAt, "sim.telecomm.ground.AttachReceiver/Finalize", recomputeReception)
			return
		}
	}
	source.Subscribe(recomputeReception)
	ctx.Later("sim.telecomm.ground.AttachReceiver/Recompute", recomputeReception)
}

type Sender struct {
	ctx         model.SimContext
	dest        *telecomm.Connection
	disp        *component.EventDispatcher
	canceltimer func()
}

func MakeSender(ctx model.SimContext, dest *telecomm.Connection) *Sender {
	return &Sender{
		ctx:         ctx,
		dest:        dest,
		disp:        component.MakeEventDispatcher(ctx, "sim.telecomm.ground.Sender/Dispatch"),
		canceltimer: nil,
	}
}

func (s *Sender) Subscribe(callback func()) (cancel func()) {
	return s.disp.Subscribe(callback)
}

func (s *Sender) CanSend() bool {
	if s.dest.LastEndTime().After(s.ctx.Now()) {
		// not done sending yet... don't accept more data to transmit.
		// but set a timer, so that when we DO have the ability to send, we can let anyone subscribed know
		if s.canceltimer != nil {
			s.canceltimer()
		}
		s.canceltimer = s.ctx.SetTimer(s.dest.LastEndTime(), "sim.telecomm.ground.Sender/CompleteSend", s.disp.Dispatch)
		return false
	} else {
		return true
	}
}

func (s *Sender) Send(packet *CommPacket) bool {
	if !s.CanSend() {
		return false
	}
	// in this case, we have no pending bytes to send, so we can transmit now!
	s.dest.UpdateTransmission(s.ctx.Now(), packet.Encode())
	return true
}
