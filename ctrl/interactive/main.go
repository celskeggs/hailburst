package main

import (
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/trial"
	"github.com/celskeggs/hailburst/ctrl/util"
	"log"
	"os"
	"strconv"
)

func usage() {
	fmt.Printf(
		"Usage: ./ctrl.sh [--verbose] [--clean] [--rebuild] [--linux] [--irradiate mem|reg] [--run] [--monitor] " +
			"[--no-watchdog] [--background <gdb port>] [--trial-dir <directory>]\n",
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
			i++
			if i < len(os.Args) && (os.Args[i] == "mem" || os.Args[i] == "reg") {
				options.Irradiate = true
				options.RegisterMode = os.Args[i] == "reg"
			} else {
				usage()
				return
			}
		case "--run":
			options.Run = true
		case "--monitor":
			options.Monitor = true
		case "--no-watchdog":
			options.NoWatchdog = true
		case "--background":
			i++
			if i < len(os.Args) {
				port, err := strconv.ParseUint(os.Args[i], 10, 16)
				if err != nil {
					usage()
				}
				options.Interactive = false
				options.GdbPort = uint(port)
			} else {
				usage()
				return
			}
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
