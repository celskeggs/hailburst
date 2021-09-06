package packetlink

import "github.com/celskeggs/hailburst/sim/fakewire/fwmodel"

type tappedSink struct {
	fwmodel.PacketSink
	cb func(packet []byte)
}

func (t *tappedSink) SendPacket(packetData []byte) {
	t.cb(packetData)
	t.PacketSink.SendPacket(packetData)
}

func TapSink(sink fwmodel.PacketSink, cb func(packet []byte)) fwmodel.PacketSink {
	return &tappedSink{
		PacketSink: sink,
		cb:         cb,
	}
}

type tappedSource struct {
	fwmodel.PacketSource
	cb func(packet []byte)
}

func (t *tappedSource) ReceivePacket() []byte {
	packet := t.PacketSource.ReceivePacket()
	t.cb(packet)
	return packet
}

func TapSource(sink fwmodel.PacketSource, cb func(packet []byte)) fwmodel.PacketSource {
	return &tappedSource{
		PacketSource: sink,
		cb:           cb,
	}
}
