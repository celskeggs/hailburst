package main

import (
	"fmt"
	"log"
	"os"
	"sim/spacecraft"
	"sim/timesync"
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
	app := spacecraft.BuildSpacecraft(func(elapsed time.Duration, explanation string) {
		_, err := logFile.WriteString(fmt.Sprintf("Experiment: time elapsed is %v\n%s\nFailure detected in experiment.\n", elapsed, explanation))
		if err == nil {
			err = logFile.Sync()
		}
		if err != nil {
			log.Printf("Encountered error while writing to log file: %v", err)
		}
		log.Printf("Wrote failure information to log file")
	})
	profiler, err := MakeProfiler("profile.csv", app)
	if err != nil {
		log.Fatalf("Cannot initialize profiler: %v", err)
	}
	err = timesync.Simple("./timesync.sock", profiler)
	if err != nil {
		log.Fatalf("Encountered top-level error: %v", err)
	}
}
