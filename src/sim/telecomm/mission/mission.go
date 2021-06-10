package mission

import (
	"sim/model"
	"sim/telecomm"
	"sim/verifier/collector"
	"time"
)

func AttachMissionControl(ctx model.SimContext, comm *telecomm.Connection, ac collector.ActivityCollector) {
	// generally send around two commands per second
	AttachCommandGenerator(ctx, comm, time.Second/2, ac)
	AttachTelemetryCollector(ctx, comm, ac)
}
