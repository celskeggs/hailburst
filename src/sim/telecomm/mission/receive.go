package mission

import (
	"sim/model"
	"sim/telecomm"
	"sim/telecomm/transport"
	"sim/verifier/collector"
)

func AttachTelemetryCollector(ctx model.SimContext, input *telecomm.Connection, ac collector.ActivityCollector) {
	transport.AttachReceiver(ctx, input, func(packet *transport.CommPacket) {
		telem, timestamp, ok := transport.DecodeTelemetry(packet)
		if ok {
			ac.OnTelemetryDownlink(telem, timestamp)
		} else {
			ac.OnTelemetryErrors(0, 1)
		}
	}, func(errorCount int) {
		ac.OnTelemetryErrors(errorCount, 0)
	})
}
