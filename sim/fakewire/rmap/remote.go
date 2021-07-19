package rmap

import (
	"errors"
	"fmt"
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/rmap/packet"
	"github.com/celskeggs/hailburst/sim/fakewire/router"
	"github.com/celskeggs/hailburst/sim/model"
	"log"
	"reflect"
)

type RemoteAddressing struct {
	// how to get the packet there
	DestinationPath    packet.Path
	DestinationLogical uint8 // (default 254)
	// how to indicate we're talking to the right destination
	DestinationKey uint8
	// how to get a reply back
	SourcePath    packet.Path
	SourceLogical uint8 // (default 254)
}

type Completion struct {
	original  packet.Packet
	completed bool
	status    uint8
	data      []byte
}

func (c *Completion) Completed() bool {
	return c.completed
}

func (c *Completion) Status() uint8 {
	if !c.completed {
		panic("not yet completed")
	}
	return c.status
}

func (c *Completion) Data() []byte {
	if !c.completed {
		panic("not yet completed")
	}
	return c.data
}

type RemoteTerminal struct {
	event                   *component.EventDispatcher
	source                  fwmodel.PacketSource
	sink                    fwmodel.PacketSink
	pendingAccept           bool
	outstandingTransactions map[uint16]*Completion
	nextTxId                uint16
	errorCount              uint32
}

func MakeRemoteTerminal(ctx model.SimContext, wire fwmodel.PacketWire) *RemoteTerminal {
	rt := &RemoteTerminal{
		event:                   component.MakeEventDispatcher(ctx, "sim.fakewire.rmap.RemoteTerminal/event"),
		source:                  wire.Source,
		sink:                    wire.Sink,
		pendingAccept:           false,
		outstandingTransactions: map[uint16]*Completion{},
		nextTxId:                1,
	}
	rt.source.Subscribe(rt.processPackets)
	ctx.Later("sim.fakewire.rmap.RemoteTerminal/processPackets", rt.processPackets)
	rt.sink.Subscribe(func() {
		if rt.pendingAccept && rt.sink.CanAcceptPacket() {
			rt.event.DispatchLater()
			rt.pendingAccept = false
		}
	})
	return rt
}

func (rt *RemoteTerminal) CheckErrors() (count uint32) {
	count, rt.errorCount = rt.errorCount, 0
	return count
}

func (rt *RemoteTerminal) logError(fmt string, args ...interface{}) {
	rt.errorCount += 1
	log.Printf("RMAP Remote Terminal: "+fmt, args...)
}

func comparePackets(reply packet.Packet, original packet.Packet) error {
	if wp, ok := original.(packet.WritePacket); ok {
		if wr, ok := reply.(packet.WriteReply); ok {
			if wr.OptVerifyData != wp.OptVerifyData || wr.OptIncrement != wp.OptIncrement || wp.OptAcknowledge != true {
				return fmt.Errorf("mismatched flags: packet=%v, reply=%v", wp, wr)
			} else if wr.SourceLogicalAddress != wp.SourceLogicalAddress {
				return fmt.Errorf("mismatched source addresses: packet=%v, reply=%v", wp, wr)
			} else if wr.DestinationLogicalAddress != wp.DestinationLogicalAddress {
				return fmt.Errorf("mismatched destination addresses: packet=%v, reply=%v", wp, wr)
			} else if wr.TransactionIdentifier != wp.TransactionIdentifier {
				panic("txids should always match by internal consistency")
			} else {
				// everything matches! (except path, which shouldn't, because it was stripped during routing)
				return nil
			}
		} else {
			return fmt.Errorf("expected (write packet, write reply), but got: (%v, %v)", reflect.TypeOf(original), reflect.TypeOf(reply))
		}
	} else if rp, ok := original.(packet.ReadPacket); ok {
		if rr, ok := reply.(packet.ReadReply); ok {
			if rr.OptIncrement != rp.OptIncrement {
				return fmt.Errorf("mismatched flags: packet=%v, reply=%v", rp, rr)
			} else if rr.SourceLogicalAddress != rp.SourceLogicalAddress {
				return fmt.Errorf("mismatched source addresses: packet=%v, reply=%v", rp, rr)
			} else if rr.DestinationLogicalAddress != rp.DestinationLogicalAddress {
				return fmt.Errorf("mismatched destination addresses: packet=%v, reply=%v", rp, rr)
			} else if rr.TransactionIdentifier != rp.TransactionIdentifier {
				panic("txids should always match by internal consistency")
			} else {
				// everything matches! (except path, which shouldn't, because it was stripped during routing)
				return nil
			}
		} else {
			return fmt.Errorf("expected (read packet, read reply), but got: (%v, %v)", reflect.TypeOf(original), reflect.TypeOf(reply))
		}
	} else {
		return fmt.Errorf("expected (packet, reply), but got: (%v, %v)", reflect.TypeOf(original), reflect.TypeOf(reply))
	}
}

func (rt *RemoteTerminal) processPackets() {
	for rt.source.HasPacketAvailable() {
		decoded, err := packet.DecodePacket(rt.source.ReceivePacket())
		if err != nil {
			rt.logError("could not decode received packet: %v", err)
			continue
		}
		if wr, ok := decoded.(packet.WriteReply); ok {
			completion := rt.outstandingTransactions[wr.TransactionIdentifier]
			if completion == nil {
				rt.logError("was not expecting write reply with txid=%v", wr.TransactionIdentifier)
			} else if matcherr := comparePackets(wr, completion.original); matcherr != nil {
				rt.logError("found mismatched write reply: %v", matcherr)
			} else {
				completion.completed = true
				completion.status = wr.Status
				delete(rt.outstandingTransactions, wr.TransactionIdentifier)
			}
		} else if rr, ok := decoded.(packet.ReadReply); ok {
			completion := rt.outstandingTransactions[rr.TransactionIdentifier]
			if completion == nil {
				rt.logError("was not expecting read reply with txid=%v", rr.TransactionIdentifier)
			} else if matcherr := comparePackets(rr, completion.original); matcherr != nil {
				rt.logError("found mismatched write reply: %v", matcherr)
			} else {
				completion.completed = true
				completion.status = rr.Status
				completion.data = rr.DataBytes
				delete(rt.outstandingTransactions, rr.TransactionIdentifier)
			}
		} else {
			rt.logError("RMAP Remote Terminal received unexpected packet: %v", decoded)
		}
	}
}

func (rt *RemoteTerminal) Subscribe(callback func()) (cancel func()) {
	return rt.event.Subscribe(callback)
}

func (rt *RemoteTerminal) Attach(addressing RemoteAddressing) (*RemoteDevice, error) {
	if router.Classify(int(addressing.SourceLogical)) != router.AddressTypeLogical {
		return nil, fmt.Errorf("not a valid logical address: %v", addressing.SourceLogical)
	}
	if router.Classify(int(addressing.DestinationLogical)) != router.AddressTypeLogical {
		return nil, fmt.Errorf("not a valid logical address: %v", addressing.DestinationLogical)
	}
	return &RemoteDevice{
		rt:         rt,
		addressing: addressing,
	}, nil
}

func (rt *RemoteTerminal) generateTxId() (uint16, *Completion, error) {
	// TODO: better handling for running out of transaction IDs
	if len(rt.outstandingTransactions) >= 65535 {
		return 0, nil, errors.New("out of transaction IDs to allocate")
	}
	// don't allocate 0, which is the default ID for non-acknowledged transactions
	for rt.outstandingTransactions[rt.nextTxId] != nil || rt.nextTxId == 0 {
		rt.nextTxId += 1
	}
	txid := rt.nextTxId
	rt.nextTxId += 1
	c := &Completion{}
	rt.outstandingTransactions[txid] = c
	return txid, c, nil
}

type RemoteDevice struct {
	rt         *RemoteTerminal
	addressing RemoteAddressing
}

// Write returns nil (with no error) if a transaction cannot occur at this time.
// If result is non-nil, then the request was transmitted; once a reply is received, Completed() will return true.
// If no reply is expected, Completed() will return true immediately.
func (rd *RemoteDevice) Write(acknowledge, verify, increment bool, extAddr uint8, writeAddr uint32, data []byte) (*Completion, error) {
	if !rd.rt.sink.CanAcceptPacket() {
		rd.rt.pendingAccept = true
		return nil, nil
	}
	var txid uint16 = 0
	var completion *Completion
	if acknowledge {
		var err error
		txid, completion, err = rd.rt.generateTxId()
		if err != nil {
			return nil, err
		}
	} else {
		// unacknowledged; make it immediately return
		completion = &Completion{
			completed: true,
			status:    NoError,
		}
	}
	wp := packet.WritePacket{
		DestinationPath:           rd.addressing.DestinationPath,
		DestinationLogicalAddress: rd.addressing.DestinationLogical,
		OptVerifyData:             verify,
		OptAcknowledge:            acknowledge,
		OptIncrement:              increment,
		DestinationKey:            rd.addressing.DestinationKey,
		SourcePath:                rd.addressing.SourcePath,
		SourceLogicalAddress:      rd.addressing.SourceLogical,
		TransactionIdentifier:     txid,
		ExtendedWriteAddress:      extAddr,
		WriteAddress:              writeAddr,
		DataBytes:                 data,
		DataCRC:                   packet.RmapCrc8(data),
	}
	completion.original = wp
	txmit, err := wp.Encode()
	if err != nil {
		return nil, err
	}
	rd.rt.sink.SendPacket(txmit)
	return completion, nil
}

// Read returns nil (with no error) if a transaction cannot occur at this time.
// If result is non-nil, then the request was transmitted; once a reply is received, Completed() will return true.
func (rd *RemoteDevice) Read(increment bool, extAddr uint8, readAddr uint32, dataLength int) (*Completion, error) {
	if dataLength < 0 {
		panic("impossible data length provided")
	}
	if !rd.rt.sink.CanAcceptPacket() {
		rd.rt.pendingAccept = true
		return nil, nil
	}
	txid, completion, err := rd.rt.generateTxId()
	if err != nil {
		return nil, err
	}
	rp := packet.ReadPacket{
		DestinationPath:           rd.addressing.DestinationPath,
		DestinationLogicalAddress: rd.addressing.DestinationLogical,
		OptIncrement:              increment,
		DestinationKey:            rd.addressing.DestinationKey,
		SourcePath:                rd.addressing.SourcePath,
		SourceLogicalAddress:      rd.addressing.SourceLogical,
		TransactionIdentifier:     txid,
		ExtendedReadAddress:       extAddr,
		ReadAddress:               readAddr,
		DataLength:                uint32(dataLength),
	}
	completion.original = rp
	txmit, err := rp.Encode()
	if err != nil {
		return nil, err
	}
	rd.rt.sink.SendPacket(txmit)
	return completion, nil
}

// TODO: implement read-modify-write
