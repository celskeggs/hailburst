package trial

import (
	"bufio"
	"fmt"
	"github.com/celskeggs/hailburst/ctrl/util"
	"io"
	"log"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strings"
	"time"
)

type void struct{}

type Options struct {
	HailburstDir string
	TrialDir     string
	GdbPort      uint
	RedirectLogs bool
	Verbose      bool
	Clean        bool
	Rebuild      bool
	Linux        bool
	Irradiate    bool
	RegisterMode bool
	Run          bool
	Monitor      bool
	Interactive  bool
	NoWatchdog   bool
	QemuTrace    string
}

func RemoveIfExists(path string) {
	if err := os.Remove(path); err != nil && !os.IsNotExist(err) {
		log.Fatal(err)
	}
}

func WaitUntilExists(path string, timeout time.Duration) {
	for i := 0; i < 10 && !util.Exists(path); i++ {
		time.Sleep(timeout / 10)
	}
}

func RunBuildCmd(dir string, prog string, args ...string) {
	cmd := exec.Command(prog, args...)
	cmd.Dir = dir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		log.Fatal(err)
	}
}

func (opt Options) FswSourceDir() string {
	return path.Join(opt.HailburstDir, "fsw")
}

func (opt Options) BuildLinux() {
	if opt.Rebuild {
		log.Printf("Confirming that code can compile outside buildroot")
		RunBuildCmd(opt.FswSourceDir(), "scons", "-j4")
	}
	if opt.Clean {
		log.Printf("Cleaning build directory...")
		RunBuildCmd(opt.HailburstDir, "./make.sh", "apps-dirclean")
	}
	if opt.Rebuild {
		log.Printf("Building app with target toolchain...")
		RunBuildCmd(opt.HailburstDir, "./make.sh", "apps-rebuild")
		log.Printf("Rebuilding kernel image...")
		RunBuildCmd(opt.HailburstDir, "./make.sh")
	}
}

func (opt Options) BuildVivid() {
	if opt.Clean {
		log.Printf("Cleaning FSW directory...")
		RunBuildCmd(opt.FswSourceDir(), "scons", "-c")
	}
	if opt.Rebuild {
		log.Printf("Rebuilding FSW image...")
		RunBuildCmd(opt.FswSourceDir(), "scons", "-j4")
	}
}

func (opt Options) ExperimentBinaryPath() string {
	return path.Join(opt.HailburstDir, "sim/requirements-bin")
}

func (opt Options) LogDecoderBinaryPath() string {
	return path.Join(opt.HailburstDir, "sim/debuglog-bin")
}

func (opt Options) BatchBinaryPath() string {
	return path.Join(opt.HailburstDir, "ctrl/batch-bin")
}

func (opt Options) BuildHostTools() {
	if opt.Rebuild {
		RunBuildCmd(opt.HailburstDir, "go", "build",
			"-o", opt.ExperimentBinaryPath(), "./sim/experiments/requirements")
		RunBuildCmd(opt.HailburstDir, "go", "build",
			"-o", opt.LogDecoderBinaryPath(), "./ctrl/debuglog")
		RunBuildCmd(opt.HailburstDir, "go", "build",
			"-o", opt.BatchBinaryPath(), "./ctrl/batch")
	}
}

func (opt Options) Build() {
	if opt.Linux {
		opt.BuildLinux()
	} else {
		opt.BuildVivid()
	}
	opt.BuildHostTools()
}

func (opt Options) RAM() (megabytes int) {
	// NOTE: the fact that amounts of RAM are different means that these configurations cannot be used for comparison!
	if opt.Linux {
		// need more memory for Linux
		return 20
	} else {
		// need a lot less memory for Vivid
		return 3
	}
}

func (opt Options) QemuLoadFswArg() []string {
	if opt.Linux {
		return []string{
			"-kernel", path.Join(opt.HailburstDir, "tree/images/zImage"),
		}
	} else {
		return []string{
			"-bios", path.Join(opt.FswSourceDir(), "build-vivid/bootrom-bin"),
		}
	}
}

func (opt Options) SymbolFile() string {
	if opt.Linux {
		return path.Join(opt.HailburstDir, "tree/build/apps-1.0/build-linux/app")
	} else {
		return path.Join(opt.FswSourceDir(), "build-vivid/kernel")
	}
}

func (opt Options) GdbPath() string {
	return path.Join(opt.HailburstDir, "../gdbroot/bin/gdb")
}

func (opt Options) GdbCmds() []string {
	cmds := []string{
		"symbol-file " + opt.SymbolFile(),
		fmt.Sprintf("target remote :%d", opt.GdbPort),
		"maintenance packet Qqemu.PhyMemMode:1",
		"set pagination off",
		"source " + path.Join(opt.HailburstDir, "ctrl/script/ctrl.py"),
		"log_inject " + path.Join(opt.TrialDir, "./injections.csv") + " " + path.Join(opt.TrialDir, "./inject.gdb"),
		"set history filename " + path.Join(opt.HailburstDir, ".gdbhistory"),
		"set history save on",
		"set history remove-duplicates 1",
	}
	if opt.Run {
		cmds = append(cmds, "stepvt 30s")
	}
	if opt.Irradiate {
		if opt.RegisterMode {
			cmds = append(cmds, "continuous 2000 200ms 300ms reg")
		} else {
			cmds = append(cmds, "continuous 2000 100ms 150ms mem")
		}
		cmds = append(cmds,
			"printf \"Campaign concluded\n\"",
			"monitor info vtime",
		)
	}
	return cmds
}

func (opt Options) GdbArgs() []string {
	cmdline := []string{opt.GdbPath()}
	if !opt.Interactive {
		cmdline = append(cmdline, "-batch")
	}
	for _, cmd := range opt.GdbCmds() {
		cmdline = append(cmdline, "-ex", cmd)
	}
	return cmdline
}

func (opt Options) TimesyncSocketPath() string {
	return path.Join(opt.TrialDir, "timesync.sock")
}

func (opt Options) ActivityLog() string {
	return path.Join(opt.TrialDir, "activity.log")
}

func (opt Options) GuestLog() string {
	return path.Join(opt.TrialDir, "guest.log")
}

func (opt Options) RequirementsLog() string {
	return path.Join(opt.TrialDir, "requirements.log")
}

func (opt Options) CleanTrialDir() {
	// remove old socket and logging output; ignore if they already don't exist, because that's what we want
	RemoveIfExists(opt.TimesyncSocketPath())
	RemoveIfExists(opt.GuestLog())
}

func (opt Options) MonitorRequirementsLog() <-chan void {
	f, err := os.OpenFile(opt.RequirementsLog(), os.O_RDONLY|os.O_CREATE, 0666)
	if err != nil {
		log.Fatal(err)
	}
	_, err = f.Seek(0, io.SeekEnd)
	if err != nil {
		log.Fatal(err)
	}
	ch := make(chan void)
	go func() {
		defer func() {
			if err := f.Close(); err != nil {
				log.Fatal(err)
			}
		}()
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
				break
			}
		}
		// let the simulation run a little longer before killing it...
		time.Sleep(time.Second * 10)
		ch <- void{}
	}()
	return ch
}

func (opt Options) StartSimulation(p *util.Processes) {
	simLog := path.Join(opt.TrialDir, "sim.log")
	p.LaunchHeadless([]string{opt.ExperimentBinaryPath()}, simLog, opt.TrialDir)
	if opt.Interactive {
		p.LaunchInTerminal([]string{"tail", "-f", simLog}, "Simulation Log", opt.TrialDir)
	}
	WaitUntilExists(opt.TimesyncSocketPath(), time.Second)
}

func (opt Options) StartGDB(p *util.Processes) {
	if opt.Interactive {
		p.LaunchInTerminal(opt.GdbArgs(), "GDB", opt.TrialDir)
	} else {
		p.LaunchHeadless(opt.GdbArgs(), path.Join(opt.TrialDir, "gdb.log"), opt.TrialDir)
	}
	time.Sleep(time.Millisecond * 100)
}

func (opt Options) QemuMachineType() string {
	if opt.Linux {
		// Linux needs the device tree blob to boot
		return "virt,x-enable-load-dtb=true,x-enable-watchdog=false"
	} else {
		// Vivid supports the strict watchdog
		return "virt,x-enable-load-dtb=false,x-enable-watchdog=true"
	}
}

func (opt Options) QemuBinPath() string {
	return path.Join(opt.HailburstDir, "../qemu/build/qemu-system-arm")
}

func (opt Options) QemuArgs() []string {
	cmd := []string{
		opt.QemuBinPath(),
		// set up GDB stub on the right port and wait for it to attach
		"-S", "-gdb", fmt.Sprintf("tcp::%v", opt.GdbPort),
		// configure machine type with appropriate options
		"-M", opt.QemuMachineType(),
		// set amount of memory to provision the virtual machine
		"-m", fmt.Sprintf("%d", opt.RAM()),
	}
	cmd = append(cmd, opt.QemuLoadFswArg()...)
	cmd = append(cmd,
		"-monitor", "stdio",
		"-parallel", "none",
		"-icount", "shift=1,sleep=off",
		"-d", "guest_errors",
		"-vga", "none",
		"-serial", "file:"+opt.GuestLog(),
		"-chardev", "timesync,id=ts0,path="+opt.TimesyncSocketPath(),
		"-device", "virtio-serial-device",
		"-device", "virtserialport,name=tsvp0,chardev=ts0",
		"-global", "virtio-mmio.force-legacy=false",
		"-nographic",
	)
	if opt.NoWatchdog {
		cmd = append(cmd,
			"-global", "watchdog-strict.disable-auto=true",
		)
	}
	if len(opt.QemuTrace) > 0 {
		cmd = append(cmd,
			"--trace", opt.QemuTrace,
		)
	}
	return cmd
}

func (opt Options) StartQEMU(p *util.Processes) {
	var qemuPid int
	if opt.Interactive {
		qemuPid = p.LaunchInTerminal(opt.QemuArgs(), "QEMU", opt.TrialDir)
	} else {
		qemuPid = p.LaunchHeadless(opt.QemuArgs(), path.Join(opt.TrialDir, "qemu.log"), opt.TrialDir)
	}
	// wait until it looks like QEMU is set up, to avoid GDB connection timeouts
	for i := 0; i < 10; i++ {
		hasSocket, err := util.HasSocket(qemuPid)
		if err != nil {
			log.Printf("Error while checking for socket: %v; skipping check.", err)
			break
		}
		if hasSocket {
			break
		}
		time.Sleep(time.Millisecond * 100)
	}
}

func (opt Options) StartActivityViewer(p *util.Processes) {
	WaitUntilExists(opt.ActivityLog(), time.Second)
	p.LaunchInTerminal(
		[]string{
			"tail", "-f", opt.ActivityLog(),
		},
		"Activity Log",
		opt.TrialDir,
	)
}

func (opt Options) StartViewer(p *util.Processes) {
	WaitUntilExists(opt.GuestLog(), time.Second)
	if opt.Linux {
		p.LaunchInTerminal(
			[]string{
				"tail", "-f", opt.GuestLog(),
			},
			"Guest Log",
			opt.TrialDir,
		)
	} else {
		p.LaunchInTerminal(
			[]string{
				opt.LogDecoderBinaryPath(),
				"--follow",
				"--srcdir", opt.FswSourceDir(),
				"--loglevel", "INFO",
				opt.GuestLog(),
				path.Join(opt.FswSourceDir(), "build-vivid/kernel"),
				path.Join(opt.FswSourceDir(), "build-vivid/bootrom-elf"),
			},
			"Decoded Guest Log",
			opt.TrialDir,
		)
	}
}

func (opt Options) Launch() {
	if opt.RedirectLogs {
		out, err := os.Create(path.Join(opt.TrialDir, "batch.log"))
		if err != nil {
			log.Fatal(err)
		}
		os.Stdout = out
		os.Stderr = out
		log.SetOutput(out)
	}

	var err error
	opt.TrialDir, err = filepath.Abs(opt.TrialDir)
	if err != nil {
		log.Fatal(err)
	}
	opt.HailburstDir, err = filepath.Abs(opt.HailburstDir)
	if err != nil {
		log.Fatal(err)
	}

	opt.Build()

	if opt.Verbose {
		log.Printf("Launching applications...")
	}

	opt.CleanTrialDir()

	var reqMon <-chan void
	if opt.Monitor {
		reqMon = opt.MonitorRequirementsLog()
	}

	// launch simulation
	p := util.MakeProcesses(opt.Verbose)
	opt.StartSimulation(p)
	opt.StartQEMU(p)
	opt.StartGDB(p)
	if opt.Interactive {
		opt.StartActivityViewer(p)
		opt.StartViewer(p)
	}

	// wait for simulation to finish
	select {
	case <-reqMon:
		log.Printf("Stopping because of requirements log.")
	case <-p.WaitAnyCh():
		log.Printf("Stopping because one of the processes exited.")
		if opt.Interactive {
			log.Printf("Press enter to exit.")
			_, _, _ = bufio.NewReader(os.Stdin).ReadLine()
		}
	}

	// now clean up
	log.Printf("Interrupting any remaining processes...")
	p.Signal(os.Interrupt)
	log.Printf("Waiting for remaining processes to terminate normally...")
	if timedout := p.WaitAllTimeout(time.Second); timedout {
		log.Printf("Killing any remaining processes...")
		p.Signal(os.Kill)
		log.Printf("Waiting for remaining processes to terminate abnormally...")
		p.WaitAll()
	}
}
