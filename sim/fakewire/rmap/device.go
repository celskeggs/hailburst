package rmap

import (
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/rmap/packet"
	"github.com/celskeggs/hailburst/sim/model"
	"log"
)

const NoError uint8 = 0

type HeaderError int

const (
	HeaderErrNotACommand HeaderError = 128
)

type LocalDevice interface {
	// CorruptCommand notifies the device that a command was ignored because it was incomplete or unverifiable.
	CorruptCommand()

	// AttemptWrite asks the device to handle a write to a particular location.
	// 0 may be returned if the write was accepted, otherwise a nonzero error code of the device's choice should be
	// returned.
	AttemptWrite(extAddr uint8, writeAddr uint32, increment bool, data []byte) (error uint8)
	// WriteCorrupt asks the device to indicate which error code it wants to use, given that an incomplete write was
	// received. 0 may not be returned, because that indicates a success.
	WriteCorrupt(extAddr uint8, writeAddr uint32, increment bool) (error uint8)

	// AttemptRead asks the device to handle a read from a particular location.
	// 0 may be returned if the read was allowed, otherwise a nonzero error code of the device's choice should be
	// returned.
	AttemptRead(extAddr uint8, readAddr uint32, increment bool, dataLength uint32) (data []byte, error uint8)
}

// PublishLocalDevice handles RMAP packets received on the wire and replies as necessary. If logicalAddress is not 0,
// it is used as an expectation for the logical address field; packets with other values are discarded.
func PublishLocalDevice(ctx model.SimContext, ld LocalDevice, logicalAddress uint8, destinationKey uint8, wire fwmodel.PacketWire) {
	processPackets := func() {
		// we only process packets if we could also send a reply immediately... no buffering here!
		for wire.Source.HasPacketAvailable() && wire.Sink.CanAcceptPacket() {
			request, err := packet.DecodePacket(wire.Source.ReceivePacket())
			if err != nil {
				log.Printf("RMAP Local Device: packet rejected by parse error: %v", err)
				ld.CorruptCommand()
			} else if wreq, ok := request.(packet.WritePacket); ok {
				if logicalAddress != 0 && logicalAddress != wreq.DestinationLogicalAddress {
					log.Printf("RMAP Local Device: packet rejected because of logical address mismatch: %v instead of %v", wreq.DestinationLogicalAddress, logicalAddress)
					ld.CorruptCommand()
				} else if wreq.DestinationKey != destinationKey {
					log.Printf("RMAP Local Device: packet rejected because of destination key mismatch: %v instead of %v", wreq.DestinationKey, destinationKey)
					ld.CorruptCommand()
				} else {
					var status uint8
					// only check CRC if requested
					if wreq.OptVerifyData && !wreq.VerifyData() {
						status = ld.WriteCorrupt(wreq.ExtendedWriteAddress, wreq.WriteAddress, wreq.OptIncrement)
						if status == NoError {
							panic("invalid WriteCorrupt implementation: should never return NoError")
						}
					} else {
						status = ld.AttemptWrite(wreq.ExtendedWriteAddress, wreq.WriteAddress, wreq.OptIncrement, wreq.DataBytes)
					}
					if wreq.OptAcknowledge {
						reply, err := packet.WriteReply{
							SourcePath:                wreq.SourcePath,
							SourceLogicalAddress:      wreq.SourceLogicalAddress,
							OptVerifyData:             wreq.OptVerifyData,
							OptIncrement:              wreq.OptIncrement,
							Status:                    status,
							DestinationLogicalAddress: wreq.DestinationLogicalAddress,
							TransactionIdentifier:     wreq.TransactionIdentifier,
						}.Encode()
						if err != nil {
							// the only error is an unencodable source path... which is not possible, because we decoded it!
							panic("should never be unable to encode write reply, yet error: " + err.Error())
						}
						wire.Sink.SendPacket(reply)
					}
				}
			} else if rreq, ok := request.(packet.ReadPacket); ok {
				if logicalAddress != 0 && logicalAddress != rreq.DestinationLogicalAddress {
					log.Printf("RMAP Local Device: packet rejected because of logical address mismatch: %v instead of %v", rreq.DestinationLogicalAddress, logicalAddress)
					ld.CorruptCommand()
				} else if rreq.DestinationKey != destinationKey {
					log.Printf("RMAP Local Device: packet rejected because of destination key mismatch: %v instead of %v", rreq.DestinationKey, destinationKey)
					ld.CorruptCommand()
				} else {
					if rreq.DataLength > packet.MaxUint24 {
						panic("invalid packet decoding: data length exceeds MaxUint24")
					}
					data, status := ld.AttemptRead(rreq.ExtendedReadAddress, rreq.ReadAddress, rreq.OptIncrement, rreq.DataLength)
					if len(data) > packet.MaxUint24 {
						panic("invalid AttemptRead implementation: returned read exceeds MaxUint24")
					}
					reply, err := packet.ReadReply{
						SourcePath:                rreq.SourcePath,
						SourceLogicalAddress:      rreq.SourceLogicalAddress,
						OptIncrement:              rreq.OptIncrement,
						Status:                    status,
						DestinationLogicalAddress: rreq.DestinationLogicalAddress,
						TransactionIdentifier:     rreq.TransactionIdentifier,
						DataBytes:                 data,
					}.Encode()
					if err != nil {
						// the first error is an unencodable source path... which is not possible, because we decoded it!
						// the second error is data exceeding the maximum, which is not possible, because we checked above!
						panic("should never be unable to encode read reply, yet error: " + err.Error())
					}
					wire.Sink.SendPacket(reply)
				}
			} else {
				log.Printf("RMAP Local Device: packet rejected because it was not a command: %v", request)
				ld.CorruptCommand()
			}
		}
	}
	wire.Sink.Subscribe(processPackets)
	wire.Source.Subscribe(processPackets)
	ctx.Later("sim.fakewire.rmap.PublishLocalDevice/processPackets", processPackets)
}
