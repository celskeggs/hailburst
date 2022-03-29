package router

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/packetlink"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/testpoint"
	"math/rand"
	"testing"
	"time"
)

func SubTestRouterN(t *testing.T, numPorts int, hasICP bool, protocol RoutingProtocol) {
	sim := component.MakeSimControllerSeeded(54321 + int64(numPorts), model.TimeZero)

	var wires []fwmodel.PacketWire
	var offset int
	if hasICP {
		if protocol == nil {
			protocol = AlwaysDropProtocol
		}
		wires = Router(sim, numPorts, true, protocol)
		offset = 0
	} else {
		if protocol == nil {
			wires = Switch(sim, numPorts)
			protocol = AlwaysDropProtocol
		} else {
			wires = Router(sim, numPorts, false, protocol)
		}
		offset = 1
	}
	inputs := make([]fwmodel.PacketSink, len(wires))
	outputs := make([]fwmodel.PacketSource, len(wires))
	for i, wire := range wires {
		input := packetlink.MakePacketNode(sim)
		inputs[i] = input.Sink()
		packetlink.PatchLinks(sim, input.Source(), wire.Sink)

		output := packetlink.MakePacketNode(sim)
		outputs[i] = output.Source()
		packetlink.PatchLinks(sim, wire.Source, output.Sink())
	}

	type testVector struct {
		SourcePortIndex int
		DestAddr        int
		ExpectedPort    int
		ExpectedPop     bool
		ExpectedDrop    bool
	}
	var testVectors []testVector
	for destAddr := 0; destAddr <= 255; destAddr++ {
		destAddrType := Classify(destAddr)
		for sourcePortIndex := 0; sourcePortIndex <= numPorts-offset; sourcePortIndex++ {
			tv := testVector{
				SourcePortIndex: sourcePortIndex,
				DestAddr:        destAddr,
			}
			if destAddrType == AddressTypePhysical && destAddr >= offset && destAddr <= numPorts {
				tv.ExpectedPort = destAddr
				tv.ExpectedPop = true
			} else if destAddrType == AddressTypeLogical {
				tv.ExpectedPort, tv.ExpectedPop, tv.ExpectedDrop = protocol(destAddr)
			} else {
				tv.ExpectedDrop = true
			}
			testVectors = append(testVectors, tv)
		}
	}
	sim.Rand().Shuffle(len(testVectors), func(i, j int) {
		testVectors[i], testVectors[j] = testVectors[j], testVectors[i]
	})

	for _, tv := range testVectors {
		// t.Logf("Test vector: %v", tv)

		// step without any packets
		sim.Advance(sim.Now().Add(time.Second))
		// confirm that no packets are available and that packets can be sent anywhere
		for i := 0; i < numPorts; i++ {
			for outputs[i].HasPacketAvailable() {
				data := outputs[i].ReceivePacket()
				t.Errorf("unexpectedly found packet of length %d available on port %d", len(data), offset+i)
			}
			if !inputs[i].CanAcceptPacket() {
				t.Errorf("unexpectedly found input unable to accept packet on port %d", offset+i)
			}
		}
		// inject a packet to this destination address
		basePacket := testpoint.RandPacket(sim.Rand())
		addressedPacket := append([]byte{byte(tv.DestAddr)}, basePacket...)
		inputs[tv.SourcePortIndex].SendPacket(addressedPacket)
		// let the packet transmit
		sim.Advance(sim.Now().Add(time.Microsecond))
		// make sure that the packet is available where expected
		if !tv.ExpectedDrop {
			if outputs[tv.ExpectedPort-offset].HasPacketAvailable() {
				data := outputs[tv.ExpectedPort-offset].ReceivePacket()
				if tv.ExpectedPop {
					testpoint.AssertPacketsMatch(t, data, basePacket)
				} else {
					testpoint.AssertPacketsMatch(t, data, addressedPacket)
				}
			} else {
				t.Errorf("expected to find packet available on output port %d, but not found", tv.ExpectedPort)
			}
		}
		// confirm that no more packets are available anywhere else
		for i := 0; i < numPorts; i++ {
			for outputs[i].HasPacketAvailable() {
				data := outputs[i].ReceivePacket()
				t.Errorf("unexpectedly found packet of length %d available on port %d", len(data), offset+i)
			}
		}
	}
}

func TestSwitch(t *testing.T) {
	for i := 1; i <= MaxPhysicalPorts; i++ {
		SubTestRouterN(t, i, false, nil)
	}
}

func TestSwitchWithICP(t *testing.T) {
	for i := 1; i <= MaxPhysicalPorts; i++ {
		SubTestRouterN(t, i, true, nil)
	}
}

func TestRouter(t *testing.T) {
	r := rand.New(rand.NewSource(87654321))

	var hitDrop, hitPop, hitNonPop bool

	for numPorts := 5; numPorts <= 10; numPorts++ {
		numPortsWithICP := numPorts + 1
		selections := map[int]int{}
		SubTestRouterN(t, numPorts, true, func(address int) (port int, pop bool, drop bool) {
			if Classify(address) != AddressTypeLogical {
				t.Fatal("not a logical address")
			}
			port, ok := selections[address]
			if !ok {
				port = r.Intn(numPortsWithICP*2 + 1)
				selections[address] = port
			}
			if port == numPortsWithICP*2 {
				// drop
				hitDrop = true
				return -1, false, true
			} else if port >= numPortsWithICP && port < numPortsWithICP*2 {
				// pop
				hitPop = true
				return port - numPortsWithICP, true, false
			} else if port >= 0 && port < numPortsWithICP {
				// no pop
				hitNonPop = true
				return port, false, false
			} else {
				panic("invalid")
			}
		})
	}

	if !hitDrop {
		t.Error("Did not test drop")
	}
	if !hitPop {
		t.Error("Did not test pop")
	}
	if !hitNonPop {
		t.Error("Did not test non-pop")
	}
}
