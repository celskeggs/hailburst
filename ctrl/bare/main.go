package main

import (
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/util"
	"log"
	"os"
	"os/exec"
	"strings"
	"time"
)

func main() {
	if util.HasArg("--help") {
		fmt.Printf("Usage: go run ./ctrl/bare [--rebuild]\n")
		return
	}
	if util.HasArg("--clean") {
		// build the image
		fmt.Printf("Cleaning makefile...\n")
		cmd := exec.Command("make", "clean")
		cmd.Dir = "bare-arm"
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			log.Fatal(err)
		}
	}
	if util.HasArg("--rebuild") {
		// build the image
		fmt.Printf("Rebuilding makefile...\n")
		cmd := exec.Command("make", "kernel")
		cmd.Dir = "bare-arm"
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			log.Fatal(err)
		}
	}
	gdbCmd := []string{
		"../gdbroot/bin/gdb",
		"-ex", "symbol-file bare-arm/kernel",
		"-ex", "target remote :1234",
		"-ex", "maintenance packet Qqemu.PhyMemMode:1",
		"-ex", "set pagination off",
		"-ex", "source ./bare-arm/ctrl.py",
		"-ex", "log_inject ./injections.csv",
	}
	if util.HasArg("--irradiate") {
		gdbCmd = append(gdbCmd, "-ex", "campaign 10000 100ms")
	} else if util.HasArg("--run") {
		gdbCmd = append(gdbCmd, "-ex", "stepvt 20s")
	}
	fmt.Printf("Launching applications...\n")
	// remove old logs; ignore any errors
	guestLog := "guest.log"
	_ = os.Remove(guestLog)

	p := util.Processes{}
	p.LaunchInTerminal("\""+strings.Join(gdbCmd, "\" \"")+"\"", "GDB", ".")
	time.Sleep(time.Millisecond * 100)
	cmd := []string{
		"../qemu/build/qemu-system-arm",
		"-S", "-s",
		"-M", "virt",
		"-m", "20",
		"-kernel", "bare-arm/kernel",
		"-monitor", "stdio",
		"-parallel", "none",
		"-icount", "shift=1,sleep=off",
		"-d", "guest_errors",
		"-vga", "none",
		"-serial", "file:" + guestLog,
		"-chardev", "vc,id=ts0",
		"-device", "virtio-serial-device",
		"-device", "virtserialport,name=tsvc0,chardev=ts0",
		"-global", "virtio-mmio.force-legacy=false",
		// "-nographic",
	}
	waitMain := p.LaunchInTerminal(strings.Join(cmd, " "), "QEMU", ".")
	for i := 0; i < 10 && !util.Exists(guestLog); i++ {
		time.Sleep(time.Millisecond * 100)
	}
	p.LaunchInTerminal("tail -f "+guestLog, "Guest Log", ".")
	waitMain()
	fmt.Printf("Interrupting all...\n")
	p.Interrupt()
	fmt.Printf("Waiting for all to terminate...\n")
	p.WaitAll()
}
