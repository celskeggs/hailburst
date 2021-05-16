package main

import (
	"bytes"
	"encoding/binary"
	"encoding/csv"
	"fmt"
	"log"
	"math/rand"
	"os"
	"sim/fakewire/fwmodel"
	"sim/model"
	"sim/timesync"
	"time"
)

type CSVWriter struct {
	writer *csv.Writer
}

func MakeCSVWriter(path string) (*CSVWriter, error) {
	f, err := os.Create(path)
	if err != nil {
		return nil, err
	}
	cw := csv.NewWriter(f)
	if err := cw.Write([]string{"Record ID", "Timestamp", "Iteration", "Total Errors", "Marker Returned", "Last Marker Sent"}); err != nil {
		return nil, err
	}
	cw.Flush()
	if err := cw.Error(); err != nil {
		return nil, err
	}
	return &CSVWriter{
		writer: cw,
	}, nil
}

func (cw *CSVWriter) WriteError(recordId uint, timestamp model.VirtualTime) {
	cw.writeInternal(recordId, uint64(timestamp), "", "", "", "")
}

func (cw *CSVWriter) Write(recordId uint, timestamp model.VirtualTime, iteration uint32, totalErrors uint32, markerReturned uint32, lastMarkerSent uint32) {
	cw.writeInternal(recordId, uint64(timestamp), iteration, totalErrors, markerReturned, lastMarkerSent)
}

func (cw *CSVWriter) writeInternal(vals... interface{}) {
	parts := make([]string, len(vals))
	for i := 0; i < 6; i++ {
		parts[i] = fmt.Sprintf("%v", vals[i])
	}
	err := cw.writer.Write(parts)
	if err != nil {
		panic("could not keep writing to CSV output (error during write)")
	}
	cw.writer.Flush()
	if cw.writer.Error() != nil {
		panic("could not keep writing to CSV output (error during flush)")
	}
}

const MagicNumber = 0x2C90BA11

func packetMain(ctx model.SimContext, source fwmodel.PacketSource, sink fwmodel.PacketSink) {
	var lastMarkerSent uint32

	cw, err := MakeCSVWriter("experiment-out.csv")
	if err != nil {
		panic("error while initializing CSV writer: " + err.Error())
	}

	r := rand.New(rand.NewSource(12345))
	// writer
	pending := true
	var writePump func()
	writePump = func() {
		if pending && sink.CanAcceptPacket() {
			// send next marker
			var packet [4]byte
			marker := lastMarkerSent + 100000 + (r.Uint32() % 100)
			binary.BigEndian.PutUint32(packet[:], marker)
			sink.SendPacket(packet[:])
			lastMarkerSent = marker

			// wait a second before sending another
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
	var recordId uint
	readPump := func() {
		for source.HasPacketAvailable() {
			recordId += 1
			var packet struct {
				Magic uint32
				Iteration uint32
				Total uint32
				Marker uint32
			}
			packetBytes := source.ReceivePacket()
			if len(packetBytes) != 16 {
				fmt.Printf("ERROR on packet length: %d instead of 16\n", packetBytes)
				cw.WriteError(recordId, ctx.Now())
				continue
			}
			err := binary.Read(bytes.NewReader(packetBytes), binary.BigEndian, &packet)
			if err != nil {
				panic("unexpected binary decoding error: " + err.Error())
			}
			if packet.Magic != MagicNumber {
				fmt.Printf("ERROR on packet magic number: %x instead of %x\n", packet.Magic, MagicNumber)
				cw.WriteError(recordId, ctx.Now())
				continue
			}
			cw.Write(recordId, ctx.Now(), packet.Iteration, packet.Total, packet.Marker, lastMarkerSent)
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
