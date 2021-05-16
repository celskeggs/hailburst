package main

import (
	"fmt"
	"log"
	"sim/fakewire/fwmodel"
	"sim/model"
	"sim/timesync"
	"time"
)

func packetMain(ctx model.SimContext, source fwmodel.PacketSource, sink fwmodel.PacketSink) {
	// writer
	iter := 0
	pending := true
	var writePump func()
	writePump = func() {
		if pending && sink.CanAcceptPacket() {
			sink.SendPacket([]byte(fmt.Sprintf("I AM MESSAGE %d FROM TESTBENCH\n", iter)))
			iter += 1
			pending = false
			ctx.SetTimer(ctx.Now().Add(time.Second), "packetMain/writePump", func() {
				pending = true
				writePump()
			})
		}
	}
	sink.Subscribe(writePump)
	ctx.Later("packetMain/writePump", writePump)

	// reader
	readPump := func() {
		for source.HasPacketAvailable() {
			packet := source.ReceivePacket()
			fmt.Printf("Received packet: %q\n", string(packet))
		}
	}
	source.Subscribe(readPump)
	ctx.Later("packetMain/readPump", readPump)
}

func main() {
	app := MakePacketApp(packetMain)
	err := timesync.Simple("./timesync-test.sock", app)
	if err != nil {
		log.Fatalf("Encountered top-level error: %v", err)
	}
}
