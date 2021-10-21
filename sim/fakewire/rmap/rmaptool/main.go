package main

import (
	"fmt"
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/fakewire/rmap/packet"
	"log"
	"os"
	"reflect"
	"strings"
)

type void struct{}

func ShouldDecode(channel string) bool {
	return strings.HasPrefix(channel, "Packet:")
}

func main() {
	if len(os.Args) != 2 {
		log.Fatalf("usage: %s <recording.csv>", os.Args[0])
	}
	recording, err := component.DecodeRecording(os.Args[1])
	if err != nil {
		log.Fatal(err)
	}
	allChannels := map[string]void{}
	for _, record := range recording {
		allChannels[record.Channel] = void{}
	}
	var maxWidth int
	for channel, _ := range allChannels {
		if ShouldDecode(channel) {
			if len(channel) > maxWidth {
				maxWidth = len(channel)
			}
		} else {
			log.Printf("Not decoding channel: %q", channel)
		}
	}
	fmt.Printf(" ---- start of recording ----\n")
	for _, record := range recording {
		if !ShouldDecode(record.Channel) {
			continue
		}
		channelPadded := record.Channel + strings.Repeat(" ", maxWidth-len(record.Channel))
		pkt, err := packet.DecodePacket(record.Bytes)
		if err != nil {
			fmt.Printf("%v [%s] decoding error: %v\n", record.Timestamp, channelPadded, err)
		} else {
			validity := "invalid"
			if pkt.IsValid() {
				validity = "valid"
			}
			rv := reflect.ValueOf(pkt)
			fmt.Printf("%v [%s] packet type %v (%s)\n", record.Timestamp, channelPadded, rv.Type(), validity)
			fieldNames := make([]string, rv.Type().NumField())
			var maxFieldWidth int
			for fi := 0; fi < len(fieldNames); fi++ {
				fname := rv.Type().Field(fi).Name
				fieldNames[fi] = fname
				if len(fname) > maxFieldWidth {
					maxFieldWidth = len(fname)
				}
			}
			for fi := 0; fi < len(fieldNames); fi++ {
				fname := fieldNames[fi]
				namePad := fname + strings.Repeat(" ", maxFieldWidth-len(fname))
				fmt.Printf("  --> %s = %v\n", namePad, rv.Field(fi).Interface())
			}
		}
	}
	fmt.Printf(" ----- end of recording -----\n")
}
