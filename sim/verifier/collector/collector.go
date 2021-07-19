package collector

import (
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/telecomm/transport"
)

type ActivityCollector interface {
	OnCommandUplink(command transport.Command, sendTimestamp model.VirtualTime)
	OnTelemetryErrors(byteErrors int, packetErrors int)
	OnTelemetryDownlink(telemetry transport.Telemetry, remoteTimestamp model.VirtualTime)
	OnSetMagnetometerPower(powered bool)
	OnMeasureMagnetometer(x, y, z int16)
}
