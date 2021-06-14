package mission

import (
	"log"
	"reflect"
	"sim/model"
	"sim/telecomm"
	"sim/telecomm/transport"
	"sim/verifier/collector"
)

func AttachTelemetryCollector(ctx model.SimContext, input *telecomm.Connection, ac collector.ActivityCollector) {
	transport.AttachReceiver(ctx, input, func(packet *transport.CommPacket) {
		telem, timestamp, err := transport.DecodeTelemetry(packet)
		if err == nil {
			log.Printf("Received telemetry: %v %v", reflect.TypeOf(telem), telem)
			ac.OnTelemetryDownlink(telem, timestamp)
		} else {
			log.Printf("Telemetry packet error: %v", err)
			ac.OnTelemetryErrors(0, 1)
		}
	}, func(errorCount int) {
		ac.OnTelemetryErrors(errorCount, 0)
	})
}
