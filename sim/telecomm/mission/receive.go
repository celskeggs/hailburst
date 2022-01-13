package mission

import (
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/telecomm"
	"github.com/celskeggs/hailburst/sim/telecomm/transport"
	"github.com/celskeggs/hailburst/sim/verifier/collector"
)

func AttachTelemetryCollector(ctx model.SimContext, input *telecomm.Connection, ac collector.ActivityCollector) {
	transport.AttachReceiver(ctx, input, func(packet *transport.CommPacket) {
		telem, timestamp, err := transport.DecodeTelemetry(packet)
		if err == nil {
			ac.OnTelemetryDownlink(telem, timestamp)
		} else {
			ac.OnTelemetryErrors(0, 1)
		}
	}, func(errorCount int) {
		ac.OnTelemetryErrors(errorCount, 0)
	})
}
