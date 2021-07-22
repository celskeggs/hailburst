package packetlink

import (
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/model"
)

func PatchLinks(ctx model.SimContext, source fwmodel.PacketSource, sink fwmodel.PacketSink) {
	pump := func() {
		for source.HasPacketAvailable() && sink.CanAcceptPacket() {
			sink.SendPacket(source.ReceivePacket())
		}
	}
	source.Subscribe(pump)
	sink.Subscribe(pump)
	ctx.Later("sim.fakewire.exchange.PatchCable/Start", pump)
}

func PatchWires(ctx model.SimContext, left fwmodel.PacketWire, right fwmodel.PacketWire) {
	PatchLinks(ctx, left.Source, right.Sink)
	PatchLinks(ctx, right.Source, left.Sink)
}
