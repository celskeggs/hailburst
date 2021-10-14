package main

import (
	"bufio"
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/util"
	"io"
	"log"
	"os"
	"os/exec"
	"path"
	"strings"
	"time"
)

func main() {
	if util.HasArg("--help") {
		fmt.Printf("Usage: ./ctrl.sh [--rebuild]\n")
		return
	}
	var imageArg []string
	ramMB := 20
	isBare := util.HasArg("--freertos")
	if isBare {
		buildPath := "build-freertos/bootrom-bin"
		imageArg = []string{
			"-bios", path.Join("fsw/", buildPath),
		}
		if util.HasArg("--clean") {
			// build the image
			fmt.Printf("Cleaning FSW directory...\n")
			cmd := exec.Command("scons", "-c")
			cmd.Dir = "fsw"
			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr
			if err := cmd.Run(); err != nil {
				log.Fatal(err)
			}
		}
		if util.HasArg("--rebuild") {
			// build the image
			fmt.Printf("Rebuilding FSW directory...\n")
			cmd := exec.Command("scons", buildPath)
			cmd.Dir = "fsw"
			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr
			if err := cmd.Run(); err != nil {
				log.Fatal(err)
			}
		}
		ramMB = 4
	} else {
		imageArg = []string{
			"-kernel", "tree/images/zImage",
		}
		if util.HasArg("--rebuild") {
			// first part is just to confirm that the code IS compilable (using the host toolchain)
			fmt.Printf("Running make app/make clean as a precheck\n")
			cmd := exec.Command("scons")
			cmd.Dir = "fsw"
			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr
			if err := cmd.Run(); err != nil {
				log.Fatal(err)
			}
		}
		if util.HasArg("--clean") {
			// clean the target build
			fmt.Printf("Running apps-dirclean...\n")
			cmd0 := exec.Command("./make.sh", "apps-dirclean")
			cmd0.Stdout = os.Stdout
			cmd0.Stderr = os.Stderr
			if err := cmd0.Run(); err != nil {
				log.Fatal(err)
			}
		}
		if util.HasArg("--rebuild") {
			// then actually build the real image, with the target toolchain
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
	}
	gdbCmd := []string{
		"../gdbroot/bin/gdb",
	}
	if isBare {
		gdbCmd = append(gdbCmd, "-ex", "symbol-file fsw/build/app")
	}
	gdbCmd = append(gdbCmd,
		"-ex", "target remote :1234",
		"-ex", "maintenance packet Qqemu.PhyMemMode:1",
		"-ex", "set pagination off",
		"-ex", "source ./ctrl/script/ctrl.py",
		"-ex", "log_inject ./injections.csv",
	)
	if util.HasArg("--irradiate") {
		gdbCmd = append(gdbCmd, "-ex", "campaign 10000 100ms")
	} else if util.HasArg("--run") {
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

	p := util.Processes{}
	p.LaunchInTerminal("go run ./sim/experiments/requirements |& tee sim.log", "Simulation", ".")
	for i := 0; i < 10 && !util.Exists(timesyncSocket); i++ {
		time.Sleep(time.Millisecond * 100)
	}
	p.LaunchInTerminal("\""+strings.Join(gdbCmd, "\" \"")+"\"", "GDB", ".")
	time.Sleep(time.Millisecond * 100)
	cmd := []string{
		"../qemu/build/qemu-system-arm",
		"-S", "-s",
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
		"-serial", "file:" + guestLog,
		"-chardev", "timesync,id=ts0,path=" + timesyncSocket,
		"-device", "virtio-serial-device",
		"-device", "virtserialport,name=tsvp0,chardev=ts0",
		"-global", "virtio-mmio.force-legacy=false",
		"-nographic",
	)
	waitMain := p.LaunchInTerminal(strings.Join(cmd, " "), "QEMU", ".")
	for i := 0; i < 10 && !util.Exists(guestLog); i++ {
		time.Sleep(time.Millisecond * 100)
	}
	p.LaunchInTerminal("tail -f "+guestLog, "Guest Log", ".")
	if util.HasArg("--monitor") {
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
