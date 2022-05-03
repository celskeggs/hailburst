package mission

import (
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/telecomm"
	"github.com/celskeggs/hailburst/sim/telecomm/transport"
	"github.com/celskeggs/hailburst/sim/verifier/collector"
	"math/rand"
	"time"
)

func randInterval(r *rand.Rand, minInterval, maxInterval time.Duration) time.Duration {
	minNs, maxNs := minInterval.Nanoseconds(), maxInterval.Nanoseconds()
	ns := r.Int63n(maxNs - minNs + 1) + minNs
	return time.Nanosecond * time.Duration(ns)
}

func AttachCommandGenerator(ctx model.SimContext, dest *telecomm.Connection, minInterval, maxInterval time.Duration, ac collector.ActivityCollector) {
	var pendingCmd transport.Command
	var lastPacketTimestamp model.VirtualTime
	var lastPacket transport.Command
	var update func()
	genCmd := func() {
		if pendingCmd != nil {
			panic("should not ever call genCmd with pendingCmd != nil")
		}
		pendingCmd = transport.GenerateCmd(ctx.Rand())
		update()
	}
	sender := transport.MakeSender(ctx, dest)
	update = func() {
		if pendingCmd != nil {
			timestamp := ctx.Now()
			packet := transport.EncodeCommand(pendingCmd, timestamp.Nanoseconds())
			if sender.Send(packet) {
				if lastPacket != nil {
					ac.OnCommandUplink(lastPacket, lastPacketTimestamp)
				}
				lastPacket, lastPacketTimestamp = pendingCmd, timestamp
				pendingCmd = nil
				delay := randInterval(ctx.Rand(), minInterval, maxInterval)
				_ = ctx.SetTimer(ctx.Now().Add(delay), "sim.telecomm.cmd.CommandGenerator/GenCmd", genCmd)
				// we only check this so that the sender knows we want to hear when we can send again
				if sender.CanSend() {
					panic("should never be able to instantly send!")
				}
			} else {
				// if we fail, then the sender will tell us when we can try again
			}
		} else if lastPacket != nil && sender.CanSend() {
			// make sure we notify about packet send completion even if we aren't ready to send another packet
			ac.OnCommandUplink(lastPacket, lastPacketTimestamp)
			lastPacket = nil
		}
	}
	sender.Subscribe(update)
	// don't start the generator for the first second, to let the software initialize on boot
	ctx.SetTimer(ctx.Now().Add(time.Second), "sim.telecomm.cmd.CommandGenerator/First", genCmd)
}
