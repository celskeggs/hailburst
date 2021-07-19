package telecomm

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/model"
	"time"
)

type Connection struct {
	ctx          model.SimContext
	dispOut      *component.EventDispatcher
	dispIn       *component.EventDispatcher
	byteDuration time.Duration
	outbound     *ByteSchedule
	inbound      *ByteSchedule
}

func MakePathway(ctx model.SimContext, byteDuration time.Duration) (*Connection, *Connection) {
	l2r, r2l := MakeByteSchedule(ctx), MakeByteSchedule(ctx)
	l2rd := component.MakeEventDispatcher(ctx, "sim.telecomm.Connection/L2R")
	r2ld := component.MakeEventDispatcher(ctx, "sim.telecomm.Connection/R2L")
	return &Connection{
			ctx:          ctx,
			byteDuration: byteDuration,
			outbound:     l2r,
			dispOut:      l2rd,
			inbound:      r2l,
			dispIn:       r2ld,
		}, &Connection{
			ctx:          ctx,
			byteDuration: byteDuration,
			outbound:     r2l,
			dispOut:      r2ld,
			inbound:      l2r,
			dispIn:       l2rd,
		}
}

// Subscribe to changes in the future transmission schedule
func (tc *Connection) Subscribe(callback func()) (cancel func()) {
	return tc.dispIn.Subscribe(callback)
}

func (tc *Connection) UpdateTransmission(transmissionStart model.VirtualTime, newBytes []byte) {
	// skip over anything we can't update
	if transmissionStart.Before(tc.ctx.Now()) {
		skip := int(1 + (tc.ctx.Now().Since(transmissionStart)-1)/tc.byteDuration)
		transmissionStart = transmissionStart.Add(tc.byteDuration * time.Duration(skip))
		if skip >= len(newBytes) {
			newBytes = nil
		} else {
			newBytes = newBytes[skip:]
		}
	}
	// confirm assumptions
	if transmissionStart.Before(tc.ctx.Now()) {
		panic("tx time should have been adjusted")
	}
	// clear anything remaining after this point
	tc.outbound.ClearBytes(transmissionStart)
	// and if we still have bytes to insert, insert them
	if len(newBytes) > 0 {
		let := tc.outbound.LastEndTime()
		if transmissionStart.Before(let) {
			transmissionStart = let
		}
		tc.outbound.FillBytes(transmissionStart, tc.byteDuration, newBytes)
	}
	tc.dispOut.DispatchLater()
}

func (tc *Connection) CountTxBytesRemaining() (count int, remainingStartAt model.VirtualTime) {
	lastEndTime := tc.outbound.LastEndTime()
	remainingDuration := lastEndTime.Since(tc.ctx.Now())
	bytesRemain := int(remainingDuration / tc.byteDuration)
	return bytesRemain, lastEndTime.Add(-tc.byteDuration * time.Duration(bytesRemain))
}

func (tc *Connection) LastEndTime() model.VirtualTime {
	return tc.outbound.LastEndTime()
}

func (tc *Connection) PullBytesAvailable() []byte {
	return tc.inbound.ReceiveBytes(tc.ctx.Now())
}

func (tc *Connection) ConsumeBytesUpTo(upto model.VirtualTime, count int) {
	if len(tc.inbound.ReceiveBytes(upto)) != count {
		panic("count mismatch when consuming bytes")
	}
}

func (tc *Connection) PeekAllBytes() []byte {
	return tc.inbound.PeekAllBytes()
}

func (tc *Connection) LookupEndTime(nth int) model.VirtualTime {
	return tc.inbound.LookupEndTime(nth)
}
