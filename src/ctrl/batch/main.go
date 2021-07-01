package main

import (
	"bufio"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path"
	"strconv"
	"time"
)

type Processes struct {
	Cmds []*exec.Cmd
}

func (p *Processes) Launch(path string, args []string, logfile string) {
	cmd := exec.Command(path, args...)

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

	fmt.Printf("Running: %s with %v\n", cmd.Path, cmd.Args)
	if err := cmd.Start(); err != nil {
		log.Printf("Error launching command %q: %v", path, err)
	}
	p.Cmds = append(p.Cmds, cmd)
}

func (p *Processes) Interrupt() {
	for _, cmd := range p.Cmds {
		if err := cmd.Process.Signal(os.Interrupt); err != nil {
			log.Printf("Error when interrupting (%q): %v", cmd.Path, err)
		}
	}
}

func (p *Processes) WaitAll() {
	for _, p := range p.Cmds {
		if err := p.Wait(); err != nil {
			log.Printf("Error while waiting: %v", err)
		}
	}
}

func Exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func main() {
	if len(os.Args) != 3 {
		log.Fatalf("usage: %s <dir> <port>", os.Args[0])
	}
	origDir, err := os.Getwd()
	if err != nil {
		log.Fatal(err)
	}
	port, err := strconv.ParseUint(os.Args[2], 10, 16)
	if err != nil {
		log.Fatal(err)
	}
	// create and move into the specified output directory
	directory := os.Args[1]
	if err := os.Mkdir(directory, 0777); err != nil {
		log.Fatal(err)
	}
	if err := os.Chdir(directory); err != nil {
		log.Fatal(err)
	}

	fmt.Printf("Launching applications...\n")

	// monitor newly created requirements.log
	f, err := os.OpenFile("requirements.log", os.O_RDONLY|os.O_CREATE, 0666)
	if err != nil {
		log.Fatal(err)
	}
	defer func(f *os.File) {
		err := f.Close()
		if err != nil {
			log.Fatal(err)
		}
	}(f)

	p := Processes{}
	timesyncSocket := "timesync.sock"
	p.Launch("go", []string{"run", "sim/experiments/requirements"}, "sim.log")
	for i := 0; i < 10 && !Exists(timesyncSocket); i++ {
		time.Sleep(time.Millisecond * 100)
	}
	cmd := []string{
		path.Join(origDir, "../qemu/build/qemu-system-arm"),
		"-S", "-gdb", fmt.Sprintf("tcp::%v", port),
		"-M", "virt",
		"-m", "20",
		"-kernel", path.Join(origDir, "tree/images/zImage"),
		"-monitor", "stdio",
		"-parallel", "none",
		"-icount", "shift=1,sleep=off",
		"-vga", "none",
		"-chardev", "timesync,id=ts0,path=" + timesyncSocket,
		"-serial", "file:guest.log",
		"-device", "virtio-serial-device",
		"-device", "virtserialport,name=tsvc0,chardev=ts0",
		"-nographic",
	}
	p.Launch(cmd[0], cmd[1:], "qemu.log")
	// wait until it looks like QEMU is set up, to avoid GDB connection timeouts
	for i := 0; i < 10 && !Exists("guest.log"); i++ {
		time.Sleep(time.Millisecond * 100)
	}
	time.Sleep(time.Millisecond * 100)
	p.Launch("gdb-multiarch", []string{
		"-batch",
		"-ex", fmt.Sprintf("target remote :%v", port),
		"-ex", "maintenance packet Qqemu.PhyMemMode:1",
		"-ex", "set pagination off",
		"-ex", "source " + path.Join(origDir, "bare-arm/ctrl.py"),
		"-ex", "log_inject ./injections.csv",
		"-ex", "stepvt 3s",
		"-ex", "campaign 10000 100ms",
		"-ex", "printf \"Campaign concluded\n\"",
		"-ex", "monitor info vtime",
	}, "gdb.log")
	go func() {
		log.Printf("Monitoring requirements log...")
		br := bufio.NewReader(f)
		for {
			line, err := br.ReadString('\n')
			if err == io.EOF {
				// keep rereading until more data is added to the file
				time.Sleep(time.Second)
				continue
			}
			if err != nil {
				if err.Error() == "use of closed file" {
					// execution has ended by subprocess failure
					break
				}
				log.Fatal(err)
			}
			fmt.Print(line)
			if line == "Failure detected in experiment.\n" {
				// execution needs to be cut off, but let the simulation run a little longer before killing it...
				time.Sleep(time.Second * 10)
				fmt.Printf("Interrupting all...\n")
				p.Interrupt()
				break
			}
		}
	}()
	fmt.Printf("Waiting for all to terminate...\n")
	p.WaitAll()
}
