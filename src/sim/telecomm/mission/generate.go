package mission

import (
	"log"
	"math"
	"math/rand"
	"reflect"
	"sim/model"
	"sim/telecomm"
	"sim/telecomm/transport"
	"sim/verifier/collector"
	"time"
)

func randInterval(r *rand.Rand, base time.Duration) time.Duration {
	baseNs := base.Nanoseconds()
	// multiply by something in the range [0.25, 4.0)
	ns := float64(baseNs) * math.Pow(2.0, r.Float64()*4-2)
	return time.Nanosecond * time.Duration(ns)
}

func AttachCommandGenerator(ctx model.SimContext, dest *telecomm.Connection, interval time.Duration, ac collector.ActivityCollector) {
	var pendingCmd transport.Command
	var lastPacketTimestamp model.VirtualTime
	var lastPacket transport.Command
	var update func()
	genCmd := func() {
		if pendingCmd != nil {
			panic("should not ever call genCmd with pendingCmd != nil")
		}
		pendingCmd = transport.GenerateCmd(ctx.Rand())
		log.Printf("Sending command: %v %v", reflect.TypeOf(pendingCmd), pendingCmd)
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
				delay := randInterval(ctx.Rand(), interval)
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
	ctx.SetTimer(model.TimeZero.Add(time.Second), "sim.telecomm.cmd.CommandGenerator/First", genCmd)
}
