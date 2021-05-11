package main

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"time"
)

func LaunchInTerminal(path string, cwd string) {
	cmd := exec.Command("xterm", "-fa", "Monospace", "-fs", "10", "-e", fmt.Sprintf("%s || read -p 'Press enter to exit...'", path))
	cmd.Dir = cwd
	fmt.Printf("Running: %s with %v\n", cmd.Path, cmd.Args)
	err := cmd.Run()
	if err != nil {
		log.Printf("Error on command %q: %v", path, err)
	}
}

func TimesyncSocketExists() bool {
	_, err := os.Stat("timesync-test.sock")
	return err == nil
}

func main() {
	if len(os.Args) >= 2 && os.Args[1] == "--rebuild" {
		fmt.Printf("Running apps-rebuild...\n")
		cmd := exec.Command("./make.sh", "apps-rebuild")
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			log.Fatal(err)
		}
		fmt.Printf("Running make...\n")
		cmd2 := exec.Command("./make.sh")
		cmd2.Stdout = os.Stdout
		cmd2.Stderr = os.Stderr
		if err := cmd2.Run(); err != nil {
			log.Fatal(err)
		}
	}
	// NOTE: current issue happens between 3220ms and 3222ms
	fmt.Printf("Launching applications...\n")
	// remove old sockets; ignore any errors
	_ = os.Remove("timesync-test.sock")
	go LaunchInTerminal("./sim.sh", ".")
	for i := 0; i < 10 && !TimesyncSocketExists(); i++ {
		time.Sleep(time.Millisecond * 100)
	}
	go LaunchInTerminal("./gdb.sh", "./bare-arm")
	time.Sleep(time.Millisecond * 100)
	LaunchInTerminal("./run.sh", ".")
}
