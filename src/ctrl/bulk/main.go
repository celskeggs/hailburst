package main

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path"
	"time"
)

type PortAssignment struct {
	ThreadNum int
	PortNum   int
}

const NumThreads = 4

func main() {
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

	threadFree := make(chan PortAssignment, NumThreads)
	for n := 0; n < NumThreads; n++ {
		threadFree <- PortAssignment{
			ThreadNum: n,
			PortNum:   51200 + n,
		}
	}

	total := 0

	running := true
	for running {
		select {
		case <-halt:
			running = false
		case assignment := <-threadFree:
			total += 1
			log.Printf("Launching batch job #%d...", total)
			go func(assignment PortAssignment) {
				stamp := fmt.Sprintf("W%v-", assignment.ThreadNum) + time.Now().Format("2006-01-02T15:04:05")
				cmd := exec.Command(
					"go", "run", "ctrl/batch",
					path.Join(workdir, stamp), fmt.Sprint(assignment.PortNum))
				cmd.Stdout = os.Stdout
				cmd.Stderr = os.Stderr
				err := cmd.Run()
				if err != nil {
					log.Printf("error during trial: %v", err)
				}
				// return free thread token to the pool
				threadFree <- assignment
			}(assignment)
			// so that all generated timestamps are unique
			time.Sleep(time.Millisecond * 1200)
		}
	}

	log.Printf("tearing down...")
	for n := 0; n < NumThreads; n++ {
		<-threadFree
	}
	log.Printf("Done! Executed %d batch jobs.", total)
}
