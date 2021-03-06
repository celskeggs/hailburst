package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/trial"
	"log"
	"math"
	"os"
	"os/exec"
	"path"
	"runtime"
	"strconv"
	"time"
)

type PortAssignment struct {
	ThreadNum int
	PortNum   int
}

func main() {
	var maxTrials uint32 = math.MaxUint32
	var numCPUs = runtime.NumCPU()
	options := trial.Options{
		HailburstDir: ".",
		RedirectLogs: true,
		Verbose:      true,
		Clean:        false,
		Rebuild:      false,
		Linux:        false,
		Irradiate:    true,
		RegisterMode: false,
		Run:          false,
		Monitor:      true,
		Interactive:  false,
	}
	for i := 1; i < len(os.Args); i++ {
		if os.Args[i] == "--max" {
			maxTrials64, err := strconv.ParseUint(os.Args[i+1], 10, 32)
			if err != nil {
				log.Fatal(err)
			}
			maxTrials = uint32(maxTrials64)
			if maxTrials == 0 {
				log.Fatal("cannot run only zero trials")
			}
			i++
		} else if os.Args[i] == "--cpus" {
			numCPUs64, err := strconv.ParseInt(os.Args[i+1], 10, 0)
			if err != nil {
				log.Fatal(err)
			}
			numCPUs = int(numCPUs64)
			if numCPUs <= 0 {
				log.Fatal("cannot run only positive numbers of CPUs in parallel")
			}
			i++
		} else if os.Args[i] == "--mode" {
			mode := os.Args[i+1]
			if mode == "mem" {
				options.RegisterMode = false
			} else if mode == "reg" {
				options.RegisterMode = true
			} else {
				log.Fatalf("unrecognized mode option: %q", mode)
			}
			i++
		} else if os.Args[i] == "--platform" {
			platform := os.Args[i+1]
			if platform == "vivid" {
				options.Linux = false
			} else if platform == "linux" {
				options.Linux = true
			} else {
				log.Fatalf("unrecognized platform option: %q", platform)
			}
			i++
		} else if os.Args[i] == "--keep-going" {
			options.Monitor = false
		} else {
			log.Fatalf("unknown argument: %q", os.Args[i])
		}
	}

	// build everything before we start
	compilationOptions := options
	compilationOptions.Rebuild = true
	compilationOptions.Build()

	workdir := "trials"

	if err := os.Mkdir(workdir, 0777); err != nil && !os.IsExist(err) {
		log.Fatal(err)
	}

	halt := make(chan struct{})
	go func() {
		input := bufio.NewReader(os.Stdin)
		_, err := input.ReadString('\n')
		if err != nil {
			log.Fatal(err)
		}
		fmt.Println("Stopping after current trials complete...")
		// otherwise, this indicates that we should stop!
		halt <- struct{}{}
	}()

	threadFree := make(chan PortAssignment, numCPUs)
	for n := 0; n < numCPUs; n++ {
		threadFree <- PortAssignment{
			ThreadNum: n,
			PortNum:   51200 + n,
		}
	}

	var total uint32 = 0

	running := true
	for running && total < maxTrials {
		select {
		case <-halt:
			running = false
		case assignment := <-threadFree:
			total += 1
			log.Printf("Launching batch job #%d...", total)
			go func(assignment PortAssignment) {
				stamp := fmt.Sprintf("W%v-", assignment.ThreadNum) + time.Now().Format("2006-01-02T15:04:05")
				localOptions := options
				localOptions.TrialDir = path.Join(workdir, stamp)
				localOptions.GdbPort = uint(assignment.PortNum)
				optsJson, err := json.Marshal(localOptions)
				if err != nil {
					log.Fatalf("error encoding local options: %v", err)
				}
				cmd := exec.Command(compilationOptions.BatchBinaryPath(), string(optsJson))
				cmd.Stdout = os.Stdout
				cmd.Stderr = os.Stderr
				if err := cmd.Run(); err != nil {
					log.Printf("error during trial: %v", err)
				}
				// sleep by at least a second so that all generated timestamps are unique within each worker
				time.Sleep(time.Millisecond * 1200)
				// return free thread token to the pool
				threadFree <- assignment
			}(assignment)
		}
	}

	log.Printf("Finished launching all batch jobs; waiting for completion.")
	for n := 0; n < numCPUs; n++ {
		<-threadFree
	}
	log.Printf("Done! Executed %d batch jobs.", total)
}
