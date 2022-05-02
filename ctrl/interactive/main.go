package main

import (
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/trial"
	"github.com/celskeggs/hailburst/ctrl/util"
	"log"
	"os"
)

func usage() {
	fmt.Printf(
		"Usage: ./ctrl.sh [--verbose] [--clean] [--rebuild] [--linux] [--irradiate] [--run] [--monitor] " +
			"[--no-watchdog] [--trial-dir <directory>]\n",
	)
}

func main() {
	if util.HasArg("--help") {
		return
	}

	options := trial.Options{
		HailburstDir: ".",
		TrialDir:     "./trial-last",
		GdbPort:      1234,
		RedirectLogs: false,
		Interactive:  true,
	}

	for i := 1; i < len(os.Args); i++ {
		arg := os.Args[i]
		switch arg {
		case "--help":
			usage()
			return
		case "--verbose":
			options.Verbose = true
		case "--clean":
			options.Clean = true
		case "--rebuild":
			options.Rebuild = true
		case "--linux":
			options.Linux = true
		case "--irradiate":
			options.Irradiate = true
		case "--run":
			options.Run = true
		case "--monitor":
			options.Monitor = true
		case "--no-watchdog":
			options.NoWatchdog = true
		case "--trial-dir":
			i++
			if i < len(os.Args) {
				options.TrialDir = os.Args[i]
			} else {
				usage()
				return
			}
		default:
			usage()
			return
		}
	}

	if err := os.Mkdir(options.TrialDir, 0777); err != nil && !os.IsExist(err) {
		log.Fatal(err)
	}
	options.Launch()
}
