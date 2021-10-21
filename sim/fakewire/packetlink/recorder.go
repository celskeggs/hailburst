package packetlink

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
)

func RecordSink(r *component.CSVByteRecorder, channel string, sink fwmodel.PacketSink) fwmodel.PacketSink {
	if r.IsRecording() {
		return TapSink(sink, func(packet []byte) {
			r.Record(channel, packet)
		})
	} else {
		return sink
	}
}

func RecordSource(r *component.CSVByteRecorder, channel string, source fwmodel.PacketSource) fwmodel.PacketSource {
	if r.IsRecording() {
		return TapSource(source, func(packet []byte) {
			r.Record(channel, packet)
		})
	} else {
		return source
	}
}

func RecordWire(r *component.CSVByteRecorder, channelSource, channelSink string, wire fwmodel.PacketWire) fwmodel.PacketWire {
	return fwmodel.PacketWire{
		Source: RecordSource(r, channelSource, wire.Source),
		Sink:   RecordSink(r, channelSink, wire.Sink),
	}
}
