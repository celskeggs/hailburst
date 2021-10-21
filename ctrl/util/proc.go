package util

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"strings"
	"sync"
	"time"
)

type void struct{}

type Processes struct {
	cmds           []*exec.Cmd
	waitChannels   []<-chan void
	waitAnyChannel chan void
	waitAnyOnce    sync.Once
}

func MakeProcesses() *Processes {
	return &Processes{
		waitAnyChannel: make(chan void),
	}
}

func wrapInXterm(argv []string, title string) []string {
	cmdline := "'" + strings.Join(argv, "' '") + "'"
	return []string{
		"xterm",
		"-fa", "Monospace", "-fs", "10", // font configuration
		"-T", title,
		"-e", fmt.Sprintf("(%s); read -p 'Press enter to exit...'", cmdline),
	}
}

func (p *Processes) LaunchInTerminal(argv []string, title string, cwd string) (pid int) {
	return p.LaunchHeadless(wrapInXterm(argv, title), "", cwd)
}

func (p *Processes) LaunchHeadless(argv []string, logfile string, cwd string) (pid int) {
	cmd := exec.Command(argv[0], argv[1:]...)
	cmd.Dir = cwd
	if logfile != "" {
		out, err := os.Create(logfile)
		if err != nil {
			log.Fatal(err)
		}
		// once we pass the file to the subprocess, we can close it safely
		defer func() {
			if err := out.Close(); err != nil {
				log.Fatal(err)
			}
		}()
		cmd.Stdout = out
		cmd.Stderr = out
	} else {
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
	}

	fmt.Printf("Running: %s with %v\n", cmd.Path, cmd.Args)
	if err := cmd.Start(); err != nil {
		log.Fatalf("Error launching command %v: %v", argv, err)
	}
	p.cmds = append(p.cmds, cmd)
	waitCh := make(chan void)
	p.waitChannels = append(p.waitChannels, waitCh)

	go func() {
		defer p.waitAnyOnce.Do(func() {
			close(p.waitAnyChannel)
		})
		defer close(waitCh)
		if err := cmd.Wait(); err != nil {
			log.Printf("Error while waiting for %v: %v", argv, err)
		} else {
			log.Printf("Finished execution of %v", argv)
		}
	}()
	return cmd.Process.Pid
}

func (p *Processes) Signal(signal os.Signal) {
	for _, cmd := range p.cmds {
		proc := cmd.Process
		if proc != nil {
			if err := proc.Signal(signal); err != nil && err.Error() != "os: process already finished" {
				log.Printf("Error when signaling %q with %v: %v", cmd.Path, signal, err)
			} else {
				log.Printf("Signaled %q with %v", cmd.Path, signal)
			}
		}
	}
}

func (p *Processes) WaitAnyCh() <-chan void {
	// wait until any of the processes terminate
	return p.waitAnyChannel
}

func (p *Processes) WaitAllTimeout(timeout time.Duration) (timedout bool) {
	// wait until ALL of the processes terminate
	timeoutCh := time.After(timeout)
	for _, ch := range p.waitChannels {
		select {
		case <-timeoutCh:
			return true
		case _, _ = <-ch:
			// continue
		}
	}
	return false
}

func (p *Processes) WaitAll() {
	// wait until ALL of the processes terminate
	for _, ch := range p.waitChannels {
		_, _ = <-ch
	}
}
