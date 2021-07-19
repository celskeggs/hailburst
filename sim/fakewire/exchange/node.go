package exchange

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/model"
)

type PacketNode struct {
	ctx        model.SimContext
	usedSink   bool
	usedSource bool

	sink          nodeSink
	source        nodeSource
	currentPacket []byte
}

func MakePacketNode(ctx model.SimContext) *PacketNode {
	pn := &PacketNode{ctx: ctx}
	pn.sink = nodeSink{
		EventDispatcher: component.MakeEventDispatcher(ctx, "sim.fakewire.exchange.PacketNode/Sink"),
		pn:              pn,
	}
	pn.source = nodeSource{
		EventDispatcher: component.MakeEventDispatcher(ctx, "sim.fakewire.exchange.PacketNode/Source"),
		pn:              pn,
	}
	ctx.Later("sim.fakewire.exchange.PacketNode/validate", func() {
		if !pn.usedSink || !pn.usedSource {
			panic("sink or source not actually used on node")
		}
	})
	return pn
}

type nodeSink struct {
	*component.EventDispatcher
	pn *PacketNode
}

func (n *nodeSink) CanAcceptPacket() bool {
	return n.pn.currentPacket == nil
}

func (n *nodeSink) SendPacket(packetData []byte) {
	if n.pn.currentPacket != nil {
		panic("invalid condition for SendPacket")
	}
	n.pn.currentPacket = packetData
	n.pn.source.DispatchLater()
}

type nodeSource struct {
	*component.EventDispatcher
	pn *PacketNode
}

func (n *nodeSource) HasPacketAvailable() bool {
	return n.pn.currentPacket != nil
}

func (n *nodeSource) ReceivePacket() []byte {
	if n.pn.currentPacket == nil {
		panic("invalid condition for ReceivePacket")
	}
	p := n.pn.currentPacket
	n.pn.currentPacket = nil
	n.pn.sink.DispatchLater()
	return p
}

func (pn *PacketNode) Sink() fwmodel.PacketSink {
	if pn.usedSink {
		panic("sink already retrieved from node")
	}
	pn.usedSink = true
	return &pn.sink
}

func (pn *PacketNode) Source() fwmodel.PacketSource {
	if pn.usedSource {
		panic("source already retrieved from node")
	}
	pn.usedSource = true
	return &pn.source
}
