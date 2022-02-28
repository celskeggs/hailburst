package collector

import (
	"fmt"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/telecomm/transport"
	"io"
	"log"
	"os"
	"strings"
)

const WIDTH = 117

type renderer struct {
	sim        model.SimContext
	underlying ActivityCollector
	output     io.WriteCloser
}

const (
	ColorBlack   = "0"
	ColorRed     = "1"
	ColorGreen   = "2"
	ColorYellow  = "3"
	ColorBlue    = "4"
	ColorMagenta = "5"
	ColorCyan    = "6"
	ColorWhite   = "7"
)

func WithColor(text string, color string) string {
	return fmt.Sprintf("\033[1;3%sm%s\033[0m", color, text)
}

func (a *renderer) display(color, left, mid, right string) {
	if a.output != nil {
		padding := WIDTH - len(left) - len(mid) - len(right)
		if padding < 0 {
			padding = 0
		}
		padLeft := strings.Repeat(" ", padding/2)
		padRight := strings.Repeat(" ", padding-len(padLeft))
		_, err := fmt.Fprintln(a.output, WithColor(left + padLeft + mid + padRight + right, color))
		if err != nil {
			log.Printf("Activity log print error: %v", err)
			err = a.output.Close()
			if err != nil {
				log.Printf("Activity log close error: %v", err)
			}
			a.output = nil
		}
	}
}

func s(text string, a ...interface{}) string {
	return fmt.Sprintf(text, a...)
}

func v(timestamp model.VirtualTime) string {
	return fmt.Sprintf("[%v]", timestamp)
}

func vL(timestamp model.VirtualTime, text string, a ...interface{}) string {
	return fmt.Sprintf("[%v] %s", timestamp, fmt.Sprintf(text, a...))
}

func vR(timestamp model.VirtualTime, text string, a ...interface{}) string {
	return fmt.Sprintf("%s [%v]", fmt.Sprintf(text, a...), timestamp)
}

func (a *renderer) OnCommandUplink(command transport.Command, sendTimestamp model.VirtualTime) {
	a.display(ColorMagenta, v(sendTimestamp), "", vR(a.sim.Now(), "UPLINK %v -------->", command))
	a.underlying.OnCommandUplink(command, sendTimestamp)
}

func (a *renderer) OnTelemetryErrors(byteErrors int, packetErrors int) {
	if byteErrors != 0 {
		a.display(ColorCyan, vL(a.sim.Now(), "<-------- DOWNLINK %v CORRUPT BYTES", byteErrors), "", "")
	}
	if packetErrors != 0 {
		a.display(ColorCyan, vL(a.sim.Now(), "<-------- DOWNLINK %v CORRUPT PACKETS", packetErrors), "", "")
	}
	a.underlying.OnTelemetryErrors(byteErrors, packetErrors)
}

func (a *renderer) OnTelemetryDownlink(telemetry transport.Telemetry, remoteTimestamp model.VirtualTime) {
	a.display(ColorCyan, vL(a.sim.Now(), "<-------- DOWNLINK %v", telemetry), "", v(remoteTimestamp))
	a.underlying.OnTelemetryDownlink(telemetry, remoteTimestamp)
}

func (a *renderer) OnSetMagnetometerPower(powered bool) {
	if powered {
		a.display(ColorYellow, "", "S/C SET MAGNETOMETER POWER = ON", v(a.sim.Now()))
	} else {
		a.display(ColorYellow, "", "S/C SET MAGNETOMETER POWER = OFF", v(a.sim.Now()))
	}
	a.underlying.OnSetMagnetometerPower(powered)
}

func (a *renderer) OnMeasureMagnetometer(x, y, z int16) {
	a.display(ColorYellow, "", s("S/C SAMPLE MAGNETOMETER <%d,%d,%d>", x, y, z), v(a.sim.Now()))
	a.underlying.OnMeasureMagnetometer(x, y, z)
}

func MakeActivityRenderer(sim model.SimContext, logPath string, underlying ActivityCollector) ActivityCollector {
	logOutput, err := os.Create(logPath)
	if err != nil {
		log.Panic(err)
	}
	a := &renderer{
		sim:        sim,
		underlying: underlying,
		output:     logOutput,
	}
	a.display(ColorYellow, v(a.sim.Now()), "ACTIVITY LOG STARTS NOW", v(a.sim.Now()))
	return a
}
