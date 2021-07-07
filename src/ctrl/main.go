package main

import (
	"bufio"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"strings"
	"time"
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

func Exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func hasArg(name string) bool {
	for _, a := range os.Args[1:] {
		if a == name {
			return true
		}
	}
	return false
}

func main() {
	if hasArg("--help") {
		fmt.Printf("Usage: ./ctrl.sh [--rebuild]\n")
		return
	}
	if hasArg("--rebuild") {
		// first part is just to confirm that the code IS compilable (using the host toolchain)
		fmt.Printf("Running make app/make clean as a precheck\n")
		cmd := exec.Command("bash", "-c", "make app && make clean")
		cmd.Dir = "ext/package/apps/src"
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			log.Fatal(err)
		}
		// then actually build the real image, with the target toolchain
		fmt.Printf("Running apps-dirclean...\n")
		cmd0 := exec.Command("./make.sh", "apps-dirclean")
		cmd0.Stdout = os.Stdout
		cmd0.Stderr = os.Stderr
		if err := cmd0.Run(); err != nil {
			log.Fatal(err)
		}
		fmt.Printf("Running apps-rebuild...\n")
		cmd1 := exec.Command("./make.sh", "apps-rebuild")
		cmd1.Stdout = os.Stdout
		cmd1.Stderr = os.Stderr
		if err := cmd1.Run(); err != nil {
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
	gdbCmd := []string{
		"gdb",
		"-ex", "target remote :1234",
		"-ex", "maintenance packet Qqemu.PhyMemMode:1",
		"-ex", "set pagination off",
		"-ex", "source ./bare-arm/ctrl.py",
		"-ex", "log_inject ./injections.csv",
	}
	if hasArg("--irradiate") {
		gdbCmd = append(gdbCmd, "-ex", "campaign 10000 100ms")
	} else if hasArg("--run") {
		gdbCmd = append(gdbCmd, "-ex", "stepvt 20s")
	}
	fmt.Printf("Launching applications...\n")
	// remove old sockets; ignore any errors
	timesyncSocket := "timesync.sock"
	guestLog := "guest.log"
	_, _ = os.Remove(timesyncSocket), os.Remove(guestLog)

	f, err := os.OpenFile("requirements.log", os.O_RDONLY|os.O_CREATE, 0666)
	if err != nil {
		log.Fatal(err)
	}
	_, err = f.Seek(0, io.SeekEnd)
	if err != nil {
		log.Fatal(err)
	}

	p := Processes{}
	p.LaunchInTerminal("go run sim/experiments/requirements |& tee sim.log", "Simulation", ".")
	for i := 0; i < 10 && !Exists(timesyncSocket); i++ {
		time.Sleep(time.Millisecond * 100)
	}
	p.LaunchInTerminal("\"" + strings.Join(gdbCmd, "\" \"") + "\"", "GDB", ".")
	time.Sleep(time.Millisecond * 100)
	cmd := []string{
		"../qemu/build/qemu-system-arm",
		"-S", "-s",
		"-M", "virt",
		"-m", "20",
		"-kernel", "tree/images/zImage",
		"-monitor", "stdio",
		"-parallel", "none",
		"-icount", "shift=1,sleep=off",
		"-vga", "none",
		"-chardev", "timesync,id=ts0,path=" + timesyncSocket,
		"-serial", "file:" + guestLog,
		"-device", "virtio-serial-device",
		"-device", "virtserialport,name=tsvc0,chardev=ts0",
		"-nographic",
	}
	waitMain := p.LaunchInTerminal(strings.Join(cmd, " "), "QEMU", ".")
	for i := 0; i < 10 && !Exists(guestLog); i++ {
		time.Sleep(time.Millisecond * 100)
	}
	p.LaunchInTerminal("tail -f "+guestLog, "Guest Log", ".")
	if hasArg("--monitor") {
		log.Printf("Monitoring requirements log...")
		br := bufio.NewReader(f)
		for {
			line, err := br.ReadString('\n')
			if err == io.EOF {
				// keep rereading until more data is added to the file
				continue
			}
			if err != nil {
				log.Fatal(err)
			}
			fmt.Print(line)
			if line == "Failure detected in experiment.\n" {
				break
			}
		}
		// let the simulation run a little longer before killing it...
		time.Sleep(time.Second * 10)
	} else {
		waitMain()
	}
	fmt.Printf("Interrupting all...\n")
	p.Interrupt()
	fmt.Printf("Waiting for all to terminate...\n")
	p.WaitAll()
}
