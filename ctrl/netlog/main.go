package main

import (
	"encoding/hex"
	"fmt"
	"github.com/celskeggs/hailburst/sim/fakewire/codec"
	"log"
	"os"
	"strings"
)

func main() {
	var ioDump string
	var channels []string
	var usage bool
	for i := 1; i < len(os.Args); i++ {
		if strings.HasPrefix(os.Args[i], "-") {
			usage = true
			break
		} else if ioDump == "" {
			ioDump = os.Args[i]
		} else {
			channels = append(channels, os.Args[i])
		}
	}
	if ioDump == "" || usage {
		log.Printf("Usage: %s <io-dump.csv> [<channels> ...]", os.Args[0])
		os.Exit(1)
	}
	records, err := ReadIODump(ioDump)
	if err != nil {
		log.Fatal(err)
	}
	records, discovered := filterChannels(records, channels)
	if len(records) == 0 {
		log.Printf("List of available channels:")
		for _, channel := range discovered {
			log.Printf(" -> %s", channel)
		}
		log.Fatal("No channels selected.")
	}
	colors := generateColors(channels)
	displayRecords(records, colors)
}

var colorStarts = []string {
	"\033[1;31m",
	"\033[1;32m",
	"\033[1;33m",
	"\033[1;34m",
	"\033[1;35m",
	"\033[1;36m",
	"\033[1;37m",
}
const colorEnd = "\033[0m"

func generateColors(channels []string) map[string]func(string)string {
	colors := map[string]func(string) string{}
	for i, channel := range channels {
		colorStart := colorStarts[i % len(colorStarts)]
		colors[channel] = func(text string) string {
			return colorStart + text + colorEnd
		}
	}
	return colors
}

func displayRecords(records []Record, colors map[string]func(string)string) {
	maxNameLen := 0
	for channel := range colors {
		if len(channel) > maxNameLen {
			maxNameLen = len(channel)
		}
	}
	decoders := map[string]*codec.Decoder{}
	snippets := map[string][]string{}
	for channel := range colors {
		channel := channel
		decoders[channel] = codec.MakeDecoder(func(bytes []byte) {
			snippets[channel] = append(snippets[channel], hex.EncodeToString(bytes))
		}, func(char codec.ControlChar, u uint32) {
			snippet := char.String()
			if char.IsParametrized() {
				snippet = fmt.Sprintf("%s(0x%08x)", snippet, u)
			} else {
				if u != 0 {
					panic("incoherent")
				}
			}
			snippets[channel] = append(snippets[channel], snippet)
		})
	}
	for _, record := range records {
		color := colors[record.Channel]
		padded := record.Channel + strings.Repeat(" ", maxNameLen-len(record.Channel))

		fmt.Println(color(
			fmt.Sprintf("[%v] %s: decoding %d bytes...", record.Timestamp, padded, len(record.DataBytes))))

		decoders[record.Channel].Decode(record.DataBytes)
		for _, group := range groupSnippets(snippets[record.Channel]) {
			fmt.Println(color(fmt.Sprintf("[%v] %s: %v", record.Timestamp, padded, strings.Join(group, " "))))
		}
		snippets[record.Channel] = nil
	}
}

func groupSnippets(snippets []string) (groups [][]string) {
	last := ""
	for _, snippet := range snippets {
		endIdx := len(groups) - 1
		if len(groups) == 0 || len(groups[endIdx]) >= 6 ||
			snippet == codec.ChStartPacket.String() || last == codec.ChEndPacket.String() {
			groups = append(groups, []string{snippet})
		} else {
			groups[endIdx] = append(groups[endIdx], snippet)
		}
		last = snippet
	}
	return
}

func filterChannels(records []Record, channels []string) (out []Record, discovered []string) {
	selectedMap := map[string]bool{}
	discoveredMap := map[string]bool{}
	for _, channel := range channels {
		selectedMap[channel] = true
	}
	for _, record := range records {
		if selectedMap[record.Channel] {
			out = append(out, record)
		}
		if !discoveredMap[record.Channel] {
			discoveredMap[record.Channel] = true
			discovered = append(discovered, record.Channel)
		}
	}
	return
}
