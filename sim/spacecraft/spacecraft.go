package spacecraft

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/packetlink"
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
	"github.com/celskeggs/hailburst/sim/verifier/collector"
	"time"
)

const (
	PortFCE    = 1 // flight compute element
	PortRadio  = 2
	PortMagnet = 3
	PortClock  = 4

	NumPorts = 4

	AddrFCEMin = 32
	AddrFCEMax = 39

	AddrRadio  = 45
	AddrMagnet = 46
	AddrClock  = 47

	KeyRadio  = 101
	KeyMagnet = 102
	KeyClock  = 103
)

func BuildSpacecraft(onFailure func(elapsed time.Duration, explanation string), reqLogPath, activityLogPath string, injectIOErrors bool, recordPath string) timesync.ProtocolImpl {
	return integrate.MakePacketApp(func(sim model.SimContext, source fwmodel.PacketSource, sink fwmodel.PacketSink, recorder *component.CSVByteRecorder) {
		// build the activity collector
		ac := verifier.MakeActivityVerifier(sim, func(explanation string) {
			onFailure(sim.Now().Since(model.TimeZero), explanation)
		}, reqLogPath)
		if activityLogPath != "" {
			ac = collector.MakeActivityRenderer(sim, activityLogPath, ac)
		}

		// now, build the onboard FakeWire topology
		ports := router.Router(sim, NumPorts, false, func(address int) (port int, pop bool, drop bool) {
			if address >= AddrFCEMin && address <= AddrFCEMax {
				return PortFCE, false, false
			}
			switch address {
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
		for port, name := range map[int]string{
			PortFCE:    "FCE",
			PortRadio:  "Radio",
			PortMagnet: "Magnet",
			PortClock:  "Clock",
		} {
			ports[port] = packetlink.RecordWire(recorder, "Packet:Router->"+name, "Packet:"+name+"->Router", ports[port])
		}

		// connect the FSW controller to the switch on Port 1
		packetlink.PatchWires(sim, ports[PortFCE], fwmodel.PacketWire{
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
	}, injectIOErrors, recordPath)
}
