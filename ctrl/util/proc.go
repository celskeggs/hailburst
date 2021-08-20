package util

import (
	"fmt"
	"log"
	"os"
	"os/exec"
)

type void struct{}

type Processes struct {
	Processes    []*os.Process
	DoneChannels []<-chan void
}

func (p *Processes) LaunchInTerminal(path string, title string, cwd string) (wait func()) {
	cmd := exec.Command("xterm", "-fa", "Monospace", "-T", title, "-fs", "10", "-e", fmt.Sprintf("(%s); read -p 'Press enter to exit...'", path))
	cmd.Dir = cwd
	fmt.Printf("Running: %s with %v\n", cmd.Path, cmd.Args)
	if err := cmd.Start(); err != nil {
		log.Printf("Error launching command %q: %v", path, err)
	}
	p.Processes = append(p.Processes, cmd.Process)
	done := make(chan void)
	p.DoneChannels = append(p.DoneChannels, done)
	go func() {
		defer close(done)
		if err := cmd.Wait(); err != nil {
			log.Printf("Error on command %q: %v", path, err)
		}
	}()
	return func() {
		<-done
	}
}

func (p *Processes) Interrupt() {
	for _, proc := range p.Processes {
		if err := proc.Signal(os.Interrupt); err != nil {
			log.Printf("Error when interrupting: %v", err)
		}
	}
	p.Processes = nil
}

func (p *Processes) WaitAll() {
	for _, donech := range p.DoneChannels {
		<-donech
	}
}
