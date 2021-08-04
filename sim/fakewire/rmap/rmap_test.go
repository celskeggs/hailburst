package rmap

import (
	"bytes"
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/fwmodel"
	"github.com/celskeggs/hailburst/sim/fakewire/packetlink"
	"github.com/celskeggs/hailburst/sim/fakewire/rmap/packet"
	"github.com/celskeggs/hailburst/sim/fakewire/router"
	"github.com/celskeggs/hailburst/sim/testpoint"
	"testing"
	"time"
)

type TestDevice struct {
	T                *testing.T
	CbCorruptCommand func()
	CbAttemptWrite   func(extAddr uint8, writeAddr uint32, increment bool, data []byte) (error uint8)
	CbWriteCorrupt   func(extAddr uint8, writeAddr uint32, increment bool) (error uint8)
	CbAttemptRead    func(extAddr uint8, readAddr uint32, increment bool, dataLength uint32) (data []byte, error uint8)
}

func (td *TestDevice) CorruptCommand() {
	if td.CbCorruptCommand == nil {
		td.T.Fatal("unexpected call to CorruptCommand()")
	}
	td.CbCorruptCommand()
}

func (td *TestDevice) AttemptWrite(extAddr uint8, writeAddr uint32, increment bool, data []byte) (error uint8) {
	if td.CbAttemptWrite == nil {
		td.T.Fatal("unexpected call to AttemptWrite()")
	}
	return td.CbAttemptWrite(extAddr, writeAddr, increment, data)
}

func (td *TestDevice) WriteCorrupt(extAddr uint8, writeAddr uint32, increment bool) (error uint8) {
	if td.CbWriteCorrupt == nil {
		td.T.Fatal("unexpected call to WriteCorrupt()")
	}
	return td.CbWriteCorrupt(extAddr, writeAddr, increment)
}

func (td *TestDevice) AttemptRead(extAddr uint8, readAddr uint32, increment bool, dataLength uint32) (data []byte, error uint8) {
	if td.CbAttemptRead == nil {
		td.T.Fatal("unexpected call to AttemptRead()")
	}
	return td.CbAttemptRead(extAddr, readAddr, increment, dataLength)
}

func (td *TestDevice) AssumeCorruptCommand() {
	if td.CbCorruptCommand != nil {
		td.T.Error("callback already registered for CorruptCommand")
	}
	td.CbCorruptCommand = func() {
		td.CbCorruptCommand = nil
	}
}

func (td *TestDevice) AssumeAttemptWrite(extAddr uint8, writeAddr uint32, increment bool, data []byte, error uint8) {
	if td.CbAttemptWrite != nil {
		td.T.Error("callback already registered for AttemptWrite")
	}
	td.CbAttemptWrite = func(extAddr2 uint8, writeAddr2 uint32, increment2 bool, data2 []byte) (error2 uint8) {
		if extAddr != extAddr2 || writeAddr != writeAddr2 || increment != increment2 || !bytes.Equal(data, data2) {
			td.T.Error("invalid arguments passed to AttemptWrite mock")
		}
		td.CbAttemptWrite = nil
		return error
	}
}

func (td *TestDevice) AssumeWriteCorrupt(extAddr uint8, writeAddr uint32, increment bool, error uint8) {
	if td.CbWriteCorrupt != nil {
		td.T.Error("callback already registered for WriteCorrupt")
	}
	td.CbWriteCorrupt = func(extAddr2 uint8, writeAddr2 uint32, increment2 bool) (error2 uint8) {
		if extAddr != extAddr2 || writeAddr != writeAddr2 || increment != increment2 {
			td.T.Error("invalid arguments passed to WriteCorrupt mock")
		}
		td.CbWriteCorrupt = nil
		return error
	}
}

func (td *TestDevice) AssumeAttemptRead(extAddr uint8, readAddr uint32, increment bool, dataLength uint32, data []byte, error uint8) {
	if td.CbAttemptRead != nil {
		td.T.Error("callback already registered for AttemptRead")
	}
	td.CbAttemptRead = func(extAddr2 uint8, readAddr2 uint32, increment2 bool, dataLength2 uint32) (data2 []byte, error2 uint8) {
		if extAddr != extAddr2 || readAddr != readAddr2 || increment != increment2 || dataLength != dataLength2 {
			td.T.Error("invalid arguments passed to AttemptRead mock")
		}
		td.CbAttemptRead = nil
		return append([]byte{}, data...), error
	}
}

func (td *TestDevice) CheckMocksCalled() {
	if td.CbCorruptCommand != nil {
		td.T.Error("CorruptCommand was not called")
	}
	if td.CbAttemptWrite != nil {
		td.T.Error("AttemptWrite was not called")
	}
	if td.CbWriteCorrupt != nil {
		td.T.Error("WriteCorrupt was not called")
	}
	if td.CbAttemptRead != nil {
		td.T.Error("AttemptRead was not called")
	}
}

// special case: potentially corrupted
func (td *TestDevice) AssumeAttemptWritePotentiallyCorrupted(extAddr uint8, writeAddr uint32, increment bool, data []byte, error uint8) {
	if td.CbAttemptWrite != nil {
		td.T.Error("callback already registered for AttemptWrite")
	}
	td.CbAttemptWrite = func(extAddr2 uint8, writeAddr2 uint32, increment2 bool, data2 []byte) (error2 uint8) {
		if extAddr != extAddr2 || writeAddr != writeAddr2 || increment != increment2 {
			td.T.Error("invalid arguments passed to known-corrupted AttemptWrite mock")
		}
		if len(data2) > len(data) {
			td.T.Error("corrupted data was unexpectedly longer")
		}
		// can't check anything about the data written... it could be different, or it could happen to be the same!
		// that's the problem with turning off 'verify'
		td.CbAttemptWrite = nil
		return error
	}
}

func runReadWriteTrials(t *testing.T, seed int64, numReads, numWrites int, destLogical, sourceLogical, destKey uint8, useLogical bool) {
	sim := component.MakeSimControllerSeeded(seed)
	// mocked local device
	testDevice := &TestDevice{T: t}
	var ra RemoteAddressing
	var swtch []fwmodel.PacketWire
	if useLogical {
		// build two-port router
		swtch = router.Router(sim, 2, false, func(address int) (port int, pop bool, drop bool) {
			if address == int(destLogical) {
				return 1, false, false
			} else if address == int(sourceLogical) {
				return 2, false, false
			} else {
				return -1, false, true
			}
		})
		// use purely logical addressing by specifying empty paths
		ra = RemoteAddressing{
			DestinationPath:    packet.Path{},
			DestinationLogical: destLogical,
			DestinationKey:     destKey,
			SourcePath:         packet.Path{},
			SourceLogical:      sourceLogical,
		}
	} else {
		// build two-port switch
		swtch = router.Switch(sim, 2)
		// use path addressing
		ra = RemoteAddressing{
			DestinationPath:    packet.Path{1},
			DestinationLogical: destLogical,
			DestinationKey:     destKey,
			SourcePath:         packet.Path{2},
			SourceLogical:      sourceLogical,
		}
	}
	// attach RMAP device to port 1
	PublishLocalDevice(sim, testDevice, destLogical, destKey, swtch[0])
	// attach remote terminal to port 2
	rd, err := MakeRemoteTerminal(sim, swtch[1]).Attach(ra)
	if err != nil {
		t.Fatal(err)
	}

	r := sim.Rand()

	writeTrials := make([]bool, numReads+numWrites)
	for i := 0; i < numWrites; i++ {
		writeTrials[i] = true
	}
	r.Shuffle(len(writeTrials), func(i, j int) {
		writeTrials[i], writeTrials[j] = writeTrials[j], writeTrials[i]
	})

	for _, isWriteTrial := range writeTrials {
		extAddr := uint8(r.Uint32())
		mainAddr := r.Uint32()
		acknowledge := !isWriteTrial || r.Intn(2) == 0
		verify := r.Intn(2) == 0 // only used for write trials
		increment := r.Intn(2) == 0

		// no actual protocol-level requirement that requested read length and returned read length match, so we can
		// do a better test by using different lengths
		dataLength := r.Uint32() & 0x00FFFFFF // only used for read trials
		dataBytes := testpoint.RandPacket(r)

		status := uint8(r.Uint32())
		// transmit request
		var completion *Completion
		for checks := 0; checks < 10 && completion == nil; checks++ {
			// fast-forward simulation
			sim.Advance(sim.Now().Add(time.Millisecond * 10))
			// attempt to transmit request
			if isWriteTrial {
				completion, err = rd.Write(acknowledge, verify, increment, extAddr, mainAddr, append([]byte{}, dataBytes...))
			} else {
				completion, err = rd.Read(increment, extAddr, mainAddr, int(dataLength))
			}
			if err != nil {
				t.Error(err)
				break
			}
		}
		if completion == nil {
			t.Error("could not transmit transaction")
			continue
		}
		if acknowledge {
			if completion.Completed() {
				t.Error("transaction should not have instantly completed")
			}
		} else {
			if !completion.Completed() {
				t.Error("transaction SHOULD have instantly completed")
			}
		}
		// set up expectation
		if isWriteTrial {
			testDevice.AssumeAttemptWrite(extAddr, mainAddr, increment, dataBytes, status)
		} else {
			testDevice.AssumeAttemptRead(extAddr, mainAddr, increment, dataLength, dataBytes, status)
		}
		// let the message be received
		sim.Advance(sim.Now().Add(time.Millisecond * 10))
		// make sure AttemptWrite/AttemptRead was called
		testDevice.CheckMocksCalled()
		// make sure that the reply returned
		if !completion.Completed() {
			t.Error("10 milliseconds should have been plenty of time for completion")
		} else {
			// and that it held the correct data
			if acknowledge {
				if completion.Status() != status {
					t.Errorf("received status %d != expected status %d", completion.Status(), status)
				}
			} else {
				if completion.Status() != 0 {
					t.Errorf("received status %d != expected non-acknowledged status 0", completion.Status())
				}
			}
			if isWriteTrial {
				if completion.Data() != nil {
					t.Error("should not be any data in a write trial")
				}
			} else {
				testpoint.AssertPacketsMatch(t, completion.Data(), dataBytes)
			}
			// and let the simulation fast-forward a bit to catch any other immediate errors
			sim.Advance(sim.Now().Add(time.Millisecond * 100))
		}

		errcount := rd.rt.CheckErrors()
		if errcount > 0 {
			t.Errorf("remote terminal encountered %d error(s)", errcount)
		}
	}
}

func TestRmapPathRead(t *testing.T) {
	// number of trials needs to be enough that, if transaction IDs don't get correctly recycled, we'll run out.
	runReadWriteTrials(t, 789, 70000, 0, 254, 201, 123, false)
}

func TestRmapPathWrite(t *testing.T) {
	// number of trials needs to be enough that, if transaction IDs don't get correctly recycled, we'll run out.
	runReadWriteTrials(t, 890, 0, 70000, 182, 61, 2, false)
}

func TestRmapPathRW(t *testing.T) {
	runReadWriteTrials(t, 901, 1000, 1000, 88, 32, 255, false)
}

func TestRmapLogicalRead(t *testing.T) {
	runReadWriteTrials(t, 012, 1000, 0, 253, 202, 32, true)
}

func TestRmapLogicalWrite(t *testing.T) {
	runReadWriteTrials(t, 123, 0, 1000, 181, 60, 52, true)
}

func TestRmapLogicalRW(t *testing.T) {
	runReadWriteTrials(t, 234, 1000, 1000, 89, 33, 222, true)
}

func TestRmapReadCorruption(t *testing.T) {
	var seed int64 = 345
	var destLogical, sourceLogical, destKey uint8 = 100, 101, 102

	sim := component.MakeSimControllerSeeded(seed)
	// mocked local device
	testDevice := &TestDevice{T: t}
	// packet nodes
	deviceInN, deviceOutN := packetlink.MakePacketNode(sim), packetlink.MakePacketNode(sim)
	terminalInN, terminalOutN := packetlink.MakePacketNode(sim), packetlink.MakePacketNode(sim)
	deviceIn, deviceOut := deviceInN.Sink(), deviceOutN.Source()
	terminalIn, terminalOut := terminalInN.Sink(), terminalOutN.Source()
	// attach RMAP device
	PublishLocalDevice(sim, testDevice, destLogical, destKey, fwmodel.PacketWire{
		Source: deviceInN.Source(),
		Sink:   deviceOutN.Sink(),
	})
	// attach remote terminal
	rd, err := MakeRemoteTerminal(sim, fwmodel.PacketWire{
		Source: terminalInN.Source(),
		Sink:   terminalOutN.Sink(),
	}).Attach(RemoteAddressing{
		DestinationPath:    packet.Path{}, // use logical addressing, so that we don't have to strip paths
		DestinationLogical: destLogical,
		DestinationKey:     destKey,
		SourcePath:         packet.Path{},
		SourceLogical:      sourceLogical,
	})
	if err != nil {
		t.Fatal(err)
	}

	r := sim.Rand()

	for trial := 0; trial < 2000; trial++ {
		// check for lingering errors
		errcount := rd.rt.CheckErrors()
		if errcount > 0 {
			t.Errorf("remote terminal encountered %d error(s)", errcount)
		}

		extAddr := uint8(r.Uint32())
		mainAddr := r.Uint32()
		increment := r.Intn(2) == 0

		var corruptRequest, corruptReply bool
		switch r.Intn(6) % 4 {
		case 0: // more likely
			corruptRequest, corruptReply = false, true
		case 1: // more likely
			corruptRequest, corruptReply = true, false
		case 2: // less likely
			corruptRequest, corruptReply = false, false
		case 3: // less likely
			corruptRequest, corruptReply = true, true
		}

		t.Logf("TRIAL %d: corruptRequest=%v, corruptReply=%v\n", trial, corruptRequest, corruptReply)

		dataBytes := testpoint.RandPacket(r)
		status := uint8(r.Uint32())

		// make sure nodes are clean
		for deviceOut.HasPacketAvailable() {
			deviceOut.ReceivePacket()
			t.Error("unexpected packet in device output node")
		}
		for terminalOut.HasPacketAvailable() {
			terminalOut.ReceivePacket()
			t.Error("unexpected packet in terminal output node")
		}

		// transmit request
		completion, err := rd.Read(increment, extAddr, mainAddr, len(dataBytes))
		if err != nil {
			t.Error(err)
			continue
		}
		if completion == nil {
			t.Error("should necessarily have been able to transmit")
			continue
		}
		// fast-forward simulation so that packet gets sent
		sim.Advance(sim.Now().Add(time.Millisecond * 10))

		// transfer packet from terminal to device
		if !terminalOut.HasPacketAvailable() {
			t.Error("should have been a packet ready to transmit")
			continue
		}
		requestData := terminalOut.ReceivePacket()
		if corruptRequest {
			// scramble one random byte to any value besides the existing value
			requestData[r.Intn(len(requestData))] ^= uint8(1 + r.Intn(255))
			// and maybe chop some bytes off the end, if we're feeling mischievous
			if r.Intn(4) == 0 {
				requestData = requestData[:r.Intn(len(requestData))]
			}
		}
		deviceIn.SendPacket(requestData)

		// if corrupted, that must mean the header is no longer intact
		if corruptRequest {
			testDevice.AssumeCorruptCommand()
		} else {
			testDevice.AssumeAttemptRead(extAddr, mainAddr, increment, uint32(len(dataBytes)), dataBytes, status)
		}
		// fast-forward simulation so that packet gets received
		sim.Advance(sim.Now().Add(time.Millisecond * 10))
		// make sure the expectation was met
		testDevice.CheckMocksCalled()

		if corruptRequest {
			if deviceOut.HasPacketAvailable() {
				t.Error("should not be any reply produced if request was corrupt")
			}
			continue
		} else if !deviceOut.HasPacketAvailable() {
			t.Error("if intact, a reply should have been produced")
			continue
		}

		// transfer reply packet from terminal to device
		replyData := deviceOut.ReceivePacket()
		if corruptReply {
			// scramble one random byte to any value besides the existing value
			replyData[r.Intn(len(replyData))] ^= uint8(1 + r.Intn(255))
			// and maybe chop some bytes off the end, if we're feeling mischievous
			if r.Intn(4) == 0 {
				replyData = replyData[:r.Intn(len(replyData))]
			}
		}
		terminalIn.SendPacket(replyData)

		// before this point, the transaction should not have completed
		if completion.Completed() {
			t.Error("transaction should not have prematurely completed")
		}

		// let the reply be received
		sim.Advance(sim.Now().Add(time.Millisecond * 10))
		if corruptReply {
			if completion.Completed() {
				t.Error("corrupted reply should have been dropped")
			}
			errcount := rd.rt.CheckErrors()
			if errcount != 1 {
				t.Errorf("remote terminal expected 1 error, but encountered %d", errcount)
			}
			continue
		}
		// if no corruption, make sure that the reply returned
		if !completion.Completed() {
			t.Error("10 milliseconds should have been plenty of time for completion")
		} else {
			// and that it held the correct data
			if completion.Status() != status {
				t.Errorf("received status %d != expected status %d", completion.Status(), status)
			}
			testpoint.AssertPacketsMatch(t, completion.Data(), dataBytes)
			// and let the simulation fast-forward a bit to catch any other immediate errors
			sim.Advance(sim.Now().Add(time.Millisecond * 100))
		}
	}

	errcount := rd.rt.CheckErrors()
	if errcount > 0 {
		t.Errorf("remote terminal encountered %d error(s)", errcount)
	}
}

func TestRmapWriteCorruption(t *testing.T) {
	var seed int64 = 456
	var destLogical, sourceLogical, destKey uint8 = 200, 201, 202

	sim := component.MakeSimControllerSeeded(seed)
	// mocked local device
	testDevice := &TestDevice{T: t}
	// packet nodes
	deviceInN, deviceOutN := packetlink.MakePacketNode(sim), packetlink.MakePacketNode(sim)
	terminalInN, terminalOutN := packetlink.MakePacketNode(sim), packetlink.MakePacketNode(sim)
	deviceIn, deviceOut := deviceInN.Sink(), deviceOutN.Source()
	terminalIn, terminalOut := terminalInN.Sink(), terminalOutN.Source()
	// attach RMAP device
	PublishLocalDevice(sim, testDevice, destLogical, destKey, fwmodel.PacketWire{
		Source: deviceInN.Source(),
		Sink:   deviceOutN.Sink(),
	})
	// attach remote terminal
	rd, err := MakeRemoteTerminal(sim, fwmodel.PacketWire{
		Source: terminalInN.Source(),
		Sink:   terminalOutN.Sink(),
	}).Attach(RemoteAddressing{
		DestinationPath:    packet.Path{}, // use logical addressing, so that we don't have to strip paths
		DestinationLogical: destLogical,
		DestinationKey:     destKey,
		SourcePath:         packet.Path{},
		SourceLogical:      sourceLogical,
	})
	if err != nil {
		t.Fatal(err)
	}

	r := sim.Rand()

	for trial := 0; trial < 2000; trial++ {
		// check for lingering errors
		errcount := rd.rt.CheckErrors()
		if errcount > 0 {
			t.Errorf("remote terminal encountered %d error(s)", errcount)
		}

		extAddr := uint8(r.Uint32())
		mainAddr := r.Uint32()
		acknowledge := r.Intn(2) == 0
		verify := r.Intn(2) == 0
		increment := r.Intn(2) == 0

		var corruptRequest, corruptReply bool
		switch r.Intn(6) % 4 {
		case 0: // more likely
			corruptRequest, corruptReply = false, true
		case 1: // more likely
			corruptRequest, corruptReply = true, false
		case 2: // less likely
			corruptRequest, corruptReply = false, false
		case 3: // less likely
			corruptRequest, corruptReply = true, true
		}

		t.Logf("TRIAL %d: corruptRequest=%v, corruptReply=%v\n", trial, corruptRequest, corruptReply)

		dataBytes := testpoint.RandPacket(r)
		status := uint8(r.Uint32())
		corruptStatus := uint8(1 + r.Intn(255)) // always nonzero

		// make sure nodes are clean
		for deviceOut.HasPacketAvailable() {
			deviceOut.ReceivePacket()
			t.Error("unexpected packet in device output node")
		}
		for terminalOut.HasPacketAvailable() {
			terminalOut.ReceivePacket()
			t.Error("unexpected packet in terminal output node")
		}

		// transmit request
		completion, err := rd.Write(acknowledge, verify, increment, extAddr, mainAddr, dataBytes)
		if err != nil {
			t.Error(err)
			continue
		}
		if completion == nil {
			t.Error("should necessarily have been able to transmit")
			continue
		}
		// fast-forward simulation so that packet gets sent
		sim.Advance(sim.Now().Add(time.Millisecond * 10))

		// transfer packet from terminal to device
		if !terminalOut.HasPacketAvailable() {
			t.Error("should have been a packet ready to transmit")
			continue
		}
		requestData := terminalOut.ReceivePacket()
		if corruptRequest {
			// scramble one random byte to any value besides the existing value
			requestData[r.Intn(len(requestData))] ^= uint8(1 + r.Intn(255))
			// and maybe chop some bytes off the end, if we're feeling mischievous
			if r.Intn(4) == 0 {
				requestData = requestData[:r.Intn(len(requestData))]
			}
		}
		deviceIn.SendPacket(requestData)

		// unlike read commands, there are multiple possibilities for corrupted write commands
		if corruptRequest {
			// first possibility: corrupted header
			testDevice.AssumeCorruptCommand()
			// second possibility: corrupted data
			if verify {
				// if verified, we'll get a notification
				testDevice.AssumeWriteCorrupt(extAddr, mainAddr, increment, corruptStatus)
			} else {
				// if not verified, might be treated normally! but... have the wrong bytes.
				testDevice.AssumeAttemptWritePotentiallyCorrupted(extAddr, mainAddr, increment, dataBytes, status)
			}
		} else {
			testDevice.AssumeAttemptWrite(extAddr, mainAddr, increment, dataBytes, status)
		}
		// fast-forward simulation so that packet gets received
		sim.Advance(sim.Now().Add(time.Millisecond * 10))
		// make sure EITHER expectation was met, but don't require all
		var wasHeaderCorrupt = false
		if corruptRequest && testDevice.CbCorruptCommand == nil {
			// then we received a CorruptCommand, and don't need the others
			testDevice.CbWriteCorrupt = nil
			testDevice.CbAttemptWrite = nil
			wasHeaderCorrupt = true
		} else if corruptRequest && testDevice.CbWriteCorrupt == nil && testDevice.CbAttemptWrite == nil {
			// then we received either a WriteCorrupt or an AttemptWrite, and don't need the corrupt command
			testDevice.CbCorruptCommand = nil
		}
		// perform final check
		testDevice.CheckMocksCalled()

		if !acknowledge {
			if deviceOut.HasPacketAvailable() {
				t.Error("should not be any reply produced when acknowledge not set")
			}
			// before this point, the transaction should not have completed
			if !completion.Completed() {
				t.Error("transaction should have immediately completed")
			} else if stat := completion.Status(); stat != 0 {
				t.Errorf("got unexpected status %d != 0 (when unacknowledged)", stat)
			}
			continue
		}

		if wasHeaderCorrupt {
			if deviceOut.HasPacketAvailable() {
				t.Error("should not be any reply produced if request was corrupt")
			}
			if completion.Completed() {
				t.Error("transaction should not have prematurely completed")
			}
			continue
		} else if !deviceOut.HasPacketAvailable() {
			t.Error("if header was not corrupted, a reply should have been produced")
			continue
		}

		// transfer reply packet from terminal to device
		replyData := deviceOut.ReceivePacket()
		if corruptReply {
			// scramble one random byte to any value besides the existing value
			replyData[r.Intn(len(replyData))] ^= uint8(1 + r.Intn(255))
			// and maybe chop some bytes off the end, if we're feeling mischievous
			if r.Intn(4) == 0 {
				replyData = replyData[:r.Intn(len(replyData))]
			}
		}
		terminalIn.SendPacket(replyData)

		// before this point, the transaction should not have completed
		if completion.Completed() {
			t.Error("transaction should not have prematurely completed")
		}

		// let the reply be received
		sim.Advance(sim.Now().Add(time.Millisecond * 10))
		if corruptReply {
			if completion.Completed() {
				t.Error("corrupted reply should have been dropped")
			}
			errcount := rd.rt.CheckErrors()
			if errcount != 1 {
				t.Errorf("remote terminal expected 1 error, but encountered %d", errcount)
			}
			continue
		}
		// if no reply corruption, make sure that the reply returned
		if !completion.Completed() {
			t.Error("10 milliseconds should have been plenty of time for completion")
		} else {
			// if reply not corrupted, then our status should depend on whether the request data was corrupt
			// (if the request header was corrupt, we would have already stopped)
			if corruptRequest && verify {
				if completion.Status() != corruptStatus {
					t.Errorf("received status %d != expected corruption status %d", completion.Status(), corruptStatus)
				}
			} else {
				if completion.Status() != status {
					t.Errorf("received status %d != expected success status %d", completion.Status(), status)
				}
			}
			// and let the simulation fast-forward a bit to catch any other immediate errors
			sim.Advance(sim.Now().Add(time.Millisecond * 100))
		}
	}

	errcount := rd.rt.CheckErrors()
	if errcount > 0 {
		t.Errorf("remote terminal encountered %d error(s)", errcount)
	}
}
