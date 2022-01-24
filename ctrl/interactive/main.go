package main

import (
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/trial"
	"github.com/celskeggs/hailburst/ctrl/util"
	"log"
	"os"
)

func main() {
	if util.HasArg("--help") {
		fmt.Printf(
			"Usage: ./ctrl.sh [--verbose] [--clean] [--rebuild] [--linux] [--irradiate] [--run] [--monitor]\n")
		return
	}

	options := trial.Options{
		HailburstDir: ".",
		TrialDir:     "./trial-last",
		GdbPort:      1234,
		RedirectLogs: false,
		Verbose:      util.HasArg("--verbose"),
		Clean:        util.HasArg("--clean"),
		Rebuild:      util.HasArg("--rebuild"),
		Linux:        util.HasArg("--linux"),
		Irradiate:    util.HasArg("--irradiate"),
		RegisterMode: util.HasArg("--registers"),
		Run:          util.HasArg("--run"),
		Monitor:      util.HasArg("--monitor"),
		Interactive:  true,
	}
	if err := os.Mkdir(options.TrialDir, 0777); err != nil && !os.IsExist(err) {
		log.Fatal(err)
	}
	options.Launch()
}
