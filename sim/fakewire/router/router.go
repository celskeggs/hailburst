package router

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/model"
)

type AddressType int

const MaxPhysicalPorts = 31

const (
	AddressTypeInvalid  AddressType = 0
	AddressTypePhysical AddressType = 1
	AddressTypeLogical  AddressType = 2
)

func Classify(address int) AddressType {
	if address >= 0 && address <= MaxPhysicalPorts {
		return AddressTypePhysical
	} else if address > MaxPhysicalPorts && address <= 254 {
		return AddressTypeLogical
	} else {
		return AddressTypeInvalid
	}
}

// A RoutingProtocol takes in a logical address (32-254) and returns a 3-tuple:
//  1. The port number (0-31) to route the request
//  2. Whether the logical address byte should be popped off afterwards
//  3. Whether the packet should be dropped (in which case the other return values do not matter)
type RoutingProtocol func(address int) (port int, pop bool, drop bool)

type routerInfo struct {
	sim      model.SimContext
	protocol RoutingProtocol
	fifos    []*receiveFIFO
	muxes    []*transmitMux
}

func (ri *routerInfo) MakeWire(portID int) fwmodel.PacketWire {
	fifo := &receiveFIFO{
		ri:              ri,
		EventDispatcher: component.MakeEventDispatcher(ri.sim, "sim.fakewire.router.Router/FIFO"),
		hasPacket:       false,
		outputPortID:    -1,
	}
	ri.fifos = append(ri.fifos, fifo)
	mux := &transmitMux{
		ri:              ri,
		EventDispatcher: component.MakeEventDispatcher(ri.sim, "sim.fakewire.router.Router/Mux"),
		portID:          portID,
		sendingFIFO:     nil,
	}
	ri.muxes = append(ri.muxes, mux)

	return fwmodel.PacketWire{
		Source: mux,
		Sink:   fifo,
	}
}

func (ri *routerInfo) getMux(portID interface{}) *transmitMux {
	for _, mux := range ri.muxes {
		if mux.portID == portID {
			return mux
		}
	}
	return nil
}

func (ri *routerInfo) parseAddress(data []byte) (outputPortID int, strippedPacket []byte, drop bool) {
	// drop packets too short to have an address
	if len(data) == 0 {
		return -1, nil, true
	}
	address := int(data[0])
	switch Classify(address) {
	case AddressTypePhysical:
		return address, data[1:], false
	case AddressTypeLogical:
		port, pop, drop := ri.protocol(address)
		if drop {
			return -1, nil, true
		}
		if pop {
			data = data[1:]
		}
		if Classify(port) != AddressTypePhysical {
			panic("invalid port: not physical")
		}
		return port, data, false
	default:
		if address != 255 {
			panic("only expected to encounter invalid addresses equal to the reserved address")
		}
		// drop anything to the reserved address
		return -1, nil, true
	}
}

type receiveFIFO struct {
	ri *routerInfo

	*component.EventDispatcher

	hasPacket    bool
	outputPortID int
	packetData   []byte
}

func (r *receiveFIFO) CanAcceptPacket() bool {
	return !r.hasPacket
}

func (r *receiveFIFO) SendPacket(packetData []byte) {
	if r.hasPacket {
		panic("cannot send packet right now")
	}
	outputPortID, strippedPacket, drop := r.ri.parseAddress(packetData)
	if drop {
		// address doesn't go anywhere... accept packet and immediately drop it
		return
	}
	if Classify(outputPortID) != AddressTypePhysical {
		panic("not a physical address")
	}
	mux := r.ri.getMux(outputPortID)
	if mux == nil {
		// port not connected... accept packet and immediately drop it
		return
	}
	r.hasPacket = true
	r.outputPortID = outputPortID
	r.packetData = strippedPacket
	if mux.sendingFIFO == nil {
		mux.sendingFIFO = r
		mux.DispatchLater()
	}
}

type transmitMux struct {
	ri *routerInfo

	*component.EventDispatcher
	portID int

	sendingFIFO *receiveFIFO
}

func (t *transmitMux) HasPacketAvailable() bool {
	return t.sendingFIFO != nil
}

func (t *transmitMux) ReceivePacket() []byte {
	if t.sendingFIFO == nil || !t.sendingFIFO.hasPacket || t.sendingFIFO.outputPortID != t.portID {
		panic("incoherent fifo/mux matching")
	}
	// send packet
	p := t.sendingFIFO.packetData
	// remove from sending FIFO
	t.sendingFIFO.hasPacket = false
	t.sendingFIFO.outputPortID = -1
	t.sendingFIFO.DispatchLater()
	t.sendingFIFO = nil
	// and see if anyone else has a packet for us
	for _, fifo := range t.ri.fifos {
		if fifo.hasPacket && fifo.outputPortID == t.portID {
			t.sendingFIFO = fifo
			break
		}
	}
	return p
}

func Router(sim model.SimContext, ports int, hasICP bool, protocol RoutingProtocol) []fwmodel.PacketWire {
	info := &routerInfo{
		sim:      sim,
		protocol: protocol,
	}
	var wires []fwmodel.PacketWire
	if hasICP {
		wires = append(wires, info.MakeWire(0))
	}
	if ports < 1 || ports > MaxPhysicalPorts {
		panic("invalid number of ports to Router constructor")
	}
	for i := 1; i <= ports; i++ {
		wires = append(wires, info.MakeWire(i))
	}
	return wires
}

func AlwaysDropProtocol(address int) (port int, pop bool, drop bool) {
	return -1, false, true
}

// A Switch is a Router that drops anything sent to any logical addresses and has no Internal Configuration Port.
func Switch(sim model.SimContext, ports int) []fwmodel.PacketWire {
	return Router(sim, ports, false, AlwaysDropProtocol)
}
