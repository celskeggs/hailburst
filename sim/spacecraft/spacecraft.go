package spacecraft

import (
	"github.com/celskeggs/hailburst/sim/fakewire/exchange"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/router"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/spacecraft/clock"
	"github.com/celskeggs/hailburst/sim/spacecraft/magnetometer"
	"github.com/celskeggs/hailburst/sim/telecomm"
	"github.com/celskeggs/hailburst/sim/telecomm/mission"
	"github.com/celskeggs/hailburst/sim/telecomm/radio"
	"github.com/celskeggs/hailburst/sim/timesync"
	"github.com/celskeggs/hailburst/sim/timesync/integrate"
	"github.com/celskeggs/hailburst/sim/verifier"
	"time"
)

const (
	PortFSW    = 1
	PortRadio  = 2
	PortMagnet = 3
	PortClock  = 4

	NumPorts = 4

	AddrFSW    = 40
	AddrRadio  = 41
	AddrMagnet = 42
	AddrClock  = 43

	KeyRadio  = 101
	KeyMagnet = 102
	KeyClock  = 103
)

func BuildSpacecraft(onFailure func(elapsed time.Duration, explanation string)) timesync.ProtocolImpl {
	return integrate.MakePacketApp(func(sim model.SimContext, source fwmodel.PacketSource, sink fwmodel.PacketSink) {
		// build the activity collector
		ac := verifier.MakeActivityVerifier(sim, func(explanation string) {
			onFailure(sim.Now().Since(model.TimeZero), explanation)
		})

		// now, build the onboard FakeWire topology
		ports := router.Router(sim, NumPorts, false, func(address int) (port int, pop bool, drop bool) {
			switch address {
			case AddrFSW:
				return PortFSW, false, false
			case AddrRadio:
				return PortRadio, false, false
			case AddrMagnet:
				return PortMagnet, false, false
			case AddrClock:
				return PortClock, false, false
			default:
				return -1, false, true
			}
		})
		// so that ports are properly indexed
		ports = append([]fwmodel.PacketWire{{nil, nil}}, ports...)
		// connect the FSW controller to the switch on Port 1
		exchange.PatchWires(sim, ports[PortFSW], fwmodel.PacketWire{
			Source: source,
			Sink:   sink,
		})
		// connect a radio to the switch on Port 2
		commGround, commFlight := telecomm.MakePathway(sim, time.Microsecond)
		mission.AttachMissionControl(sim, commGround, ac)
		radio.FWRadioConfig{
			MemorySize:     0x4000,
			LogicalAddress: AddrRadio,
			DestinationKey: KeyRadio,
		}.Construct(sim, ports[PortRadio], commFlight)
		// connect a magnetometer to the switch on Port 3
		magnetometer.Config{
			MeasurementDelay: time.Millisecond * 15,
			LogicalAddress:   AddrMagnet,
			DestinationKey:   KeyMagnet,
		}.Construct(sim, ports[PortMagnet], magnetometer.MakeRandomMagneticEnvironment(sim), ac)
		// connect a spacecraft clock to the switch on Port 4
		clock.Config{
			LogicalAddress: AddrClock,
			DestinationKey: KeyClock,
		}.Construct(sim, ports[PortClock])
	})
}
