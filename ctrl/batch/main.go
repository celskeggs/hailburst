package main

import (
	"bufio"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path"
	"strconv"
	"strings"
	"sync"
	"time"
)

type void struct{}

type Processes struct {
	Cmds           []*exec.Cmd
	WaitChannels   []chan void
	WaitAnyChannel chan void

	WaitAnyOnce sync.Once
}

func (p *Processes) Launch(path string, args []string, logfile string) (pid int) {
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
		log.Fatalf("Error launching command %q: %v", path, err)
	}
	p.Cmds = append(p.Cmds, cmd)
	waitCh := make(chan void)
	p.WaitChannels = append(p.WaitChannels, waitCh)

	go func() {
		defer p.WaitAnyOnce.Do(func() {
			close(p.WaitAnyChannel)
		})
		defer close(waitCh)
		if err := cmd.Wait(); err != nil {
			log.Printf("Error while waiting for %s: %v", path, err)
		} else {
			log.Printf("Finished execution of %s", path)
		}
	}()
	return cmd.Process.Pid
}

func (p *Processes) Signal(signal os.Signal) {
	for _, cmd := range p.Cmds {
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

func (p *Processes) WaitAny() {
	// wait until any of the processes terminate
	_, _ = <-p.WaitAnyChannel
}

func (p *Processes) WaitAllTimeout(timeout time.Duration) (timedout bool) {
	// wait until ALL of the processes terminate
	timeoutCh := time.After(timeout)
	for _, ch := range p.WaitChannels {
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
	for _, ch := range p.WaitChannels {
		_, _ = <-ch
	}
}

func Exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func HasSocket(pid int) (bool, error) {
	procdir := fmt.Sprintf("/proc/%d/fd/", pid)
	ents, err := ioutil.ReadDir(procdir)
	if err != nil {
		return false, err
	}
	for _, ent := range ents {
		// find FDs (integers)
		if _, err := strconv.ParseUint(ent.Name(), 10, 64); err == nil {
			fdPath := path.Join(procdir, ent.Name())
			dest, err := os.Readlink(fdPath)
			if err == nil && strings.HasPrefix(dest, "socket:") {
				return true, nil
			}
		}
	}
	return false, nil
}

func main() {
	if len(os.Args) != 6 {
		log.Fatalf("usage: %s <dir> <port> <plat> <mode> <stop>", os.Args[0])
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
	plat, mode, stop := os.Args[3], os.Args[4], os.Args[5]

	var imageArg []string
	var symbolFile string
	ramMB := 20
	if plat == "freertos" {
		imageArg = []string{
			"-bios", path.Join(origDir, "fsw/build-freertos/bootrom-bin"),
		}
		symbolFile = path.Join(origDir, "fsw/build-freertos/kernel")
		ramMB = 4
	} else if plat == "linux" {
		imageArg = []string{
			"-kernel", path.Join(origDir, "tree/images/zImage"),
		}
	} else {
		log.Fatalf("unknown platform: %s", plat)
	}

	out, err := os.Create("batch.log")
	if err != nil {
		log.Fatal(err)
	}
	os.Stdout = out
	os.Stderr = out
	log.SetOutput(out)

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

	p := Processes{
		WaitAnyChannel: make(chan void),
	}
	timesyncSocket := "timesync.sock"
	p.Launch(path.Join(origDir, "experiment-proc"), nil, "sim.log")
	for i := 0; i < 10 && !Exists(timesyncSocket); i++ {
		time.Sleep(time.Millisecond * 100)
	}
	cmd := []string{
		path.Join(origDir, "../qemu/build/qemu-system-arm"),
		"-S", "-gdb", fmt.Sprintf("tcp::%v", port),
		"-M", "virt,x-enable-load-dtb=false,x-enable-watchdog=true",
		"-m", fmt.Sprintf("%d", ramMB),
	}
	cmd = append(cmd, imageArg...)
	cmd = append(cmd,
		"-monitor", "stdio",
		"-parallel", "none",
		"-icount", "shift=1,sleep=off",
		"-d", "guest_errors",
		"-vga", "none",
		"-serial", "file:guest.log",
		"-chardev", "timesync,id=ts0,path="+timesyncSocket,
		"-device", "virtio-serial-device",
		"-device", "virtserialport,name=tsvp0,chardev=ts0",
		"-global", "virtio-mmio.force-legacy=false",
		"-nographic",
	)
	qemuPid := p.Launch(cmd[0], cmd[1:], "qemu.log")
	// wait until it looks like QEMU is set up, to avoid GDB connection timeouts
	for i := 0; i < 10; i++ {
		hasSocket, err := HasSocket(qemuPid)
		if err != nil {
			log.Printf("Error while checking for socket: %v; skipping check.", err)
			break
		}
		if hasSocket {
			break
		}
	}

	gdbCmd := []string{
		"-batch",
	}
	if symbolFile != "" {
		gdbCmd = append(gdbCmd, "-ex", "symbol-file "+symbolFile)
	}
	gdbCmd = append(gdbCmd,
		"-ex", fmt.Sprintf("target remote :%v", port),
		"-ex", "maintenance packet Qqemu.PhyMemMode:1",
		"-ex", "set pagination off",
		"-ex", "source "+path.Join(origDir, "ctrl/script/ctrl.py"),
		"-ex", "log_inject ./injections.csv",
	)
	if mode == "reg" {
		gdbCmd = append(gdbCmd,
			"-ex", "campaign 1000 1s reg",
		)
	} else if mode == "mem" {
		gdbCmd = append(gdbCmd,
			"-ex", "campaign 10000 100ms",
		)
	} else {
		log.Fatalf("unknown mode: %q", mode)
	}
	gdbCmd = append(gdbCmd,
		"-ex", "printf \"Campaign concluded\n\"",
		"-ex", "monitor info vtime",
	)

	p.Launch(path.Join(origDir, "../gdbroot/bin/gdb"), gdbCmd, "gdb.log")
	if stop == "req" {
		go func() {
			log.Printf("Monitoring requirements log...")
			br := bufio.NewReader(f)
			ioCeased := false
			reqFailed := false
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
				if strings.HasPrefix(line, "Experiment: monitor reported I/O ceased") {
					ioCeased = true
				}
				if line == "Failure detected in experiment.\n" {
					reqFailed = true
				}
				if ioCeased && reqFailed {
					// execution needs to be cut off, but let the simulation run a little longer before killing it...
					time.Sleep(time.Second * 10)
					fmt.Printf("Interrupting all...\n")
					p.Signal(os.Interrupt)
					break
				}
			}
		}()
	} else if stop != "none" {
		panic("invalid stop")
	}

	fmt.Printf("Waiting for any to terminate...\n")
	p.WaitAny()
	fmt.Printf("Interrupting any remaining processes...\n")
	p.Signal(os.Interrupt)
	fmt.Printf("Waiting for remaining processes to terminate normally...\n")
	if timedout := p.WaitAllTimeout(time.Second); timedout {
		fmt.Printf("Killing any remaining processes...\n")
		p.Signal(os.Kill)
		fmt.Printf("Waiting for remaining processes to terminate abnormally...\n")
		p.WaitAll()
	}
}
