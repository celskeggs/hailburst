package main

import (
	"log"
	"sim/timesync"
)

func main() {
	app := MakeTestApp()
	err := timesync.Simple("./timesync-test.sock", app)
	if err != nil {
		log.Fatalf("Encountered top-level error: %v", err)
	}
}
