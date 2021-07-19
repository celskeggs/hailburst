package mission

import (
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/telecomm"
	"github.com/celskeggs/hailburst/sim/verifier/collector"
	"time"
)

func AttachMissionControl(ctx model.SimContext, comm *telecomm.Connection, ac collector.ActivityCollector) {
	// generally send around two commands per second
	AttachCommandGenerator(ctx, comm, time.Second/2, ac)
	AttachTelemetryCollector(ctx, comm, ac)
}
