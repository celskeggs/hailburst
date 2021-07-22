package fwmodel

import "github.com/celskeggs/hailburst/sim/model"

// at the packet level of abstraction, only complete packets (terminated with an EOP) are passed around.
// packets terminated with an EEP are dropped.

type PacketSource interface {
	model.EventSource
	HasPacketAvailable() bool
	ReceivePacket() []byte
}

type PacketSink interface {
	model.EventSource
	CanAcceptPacket() bool
	SendPacket(packetData []byte)
}

type PacketWire struct {
	Source PacketSource
	Sink   PacketSink
}
