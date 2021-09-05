package main

import (
	"fmt"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/spacecraft"
	"github.com/celskeggs/hailburst/sim/timesync"
	"log"
	"os"
	"time"
)

func main() {
	logFile, err := os.OpenFile("requirements.log", os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0666)
	if err != nil {
		log.Fatalf("Encountered error while opening requirements log: %v", err)
	}
	defer func() {
		err := logFile.Close()
		if err != nil {
			log.Printf("Encountered error while closing log file: %v", err)
		}
	}()
	_, err = logFile.WriteString("\nExperiment started.\n")
	if err == nil {
		err = logFile.Sync()
	}
	if err != nil {
		log.Fatalf("Encountered error while writing to log file: %v", err)
	}
	var injectIOErrors bool
	if os.Getenv("SIM_INJECT_IO_ERRORS") == "true" {
		injectIOErrors = true
		log.Printf("IO error injection enabled by environment variable")
	}
	app := spacecraft.BuildSpacecraft(func(elapsed time.Duration, explanation string) {
		_, err := fmt.Fprintf(logFile, "Experiment: time elapsed is %f seconds\n%s\nFailure detected in experiment.\n", elapsed.Seconds(), explanation)
		if err == nil {
			err = logFile.Sync()
		}
		if err != nil {
			log.Printf("Encountered error while writing to log file: %v", err)
		}
		log.Printf("Wrote failure information to log file")
	}, "reqs-raw.log", injectIOErrors, "io-dump.csv")
	mon := MakeMonitor(app, time.Second*2, time.Second, func(lastTxmit model.VirtualTime) {
		_, err := fmt.Fprintf(logFile, "Experiment: monitor reported I/O ceased at %f seconds\n", lastTxmit.Since(model.TimeZero).Seconds())
		if err == nil {
			err = logFile.Sync()
		}
		if err != nil {
			log.Printf("Encountered error while writing to log file: %v", err)
		}
		log.Printf("Wrote monitor halt information to log file")
	})
	profiler, err := MakeProfiler("profile.csv", mon)
	if err != nil {
		log.Fatalf("Cannot initialize profiler: %v", err)
	}
	err = timesync.Simple("./timesync.sock", profiler)
	if err != nil {
		log.Fatalf("Encountered top-level error: %v", err)
	}
}
