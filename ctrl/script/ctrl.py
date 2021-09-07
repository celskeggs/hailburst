import csv
import math
import random
import re

import gdb


def qemu_hmp(cmdstr):
    return gdb.execute("monitor %s" % cmdstr, to_string=True).strip()


def now():
    text = qemu_hmp("info vtime")
    assert text.startswith("virtual time: ") and text.endswith(" ns") and text.count(" ") == 3, "invalid output: %r" % text
    return int(text.split(" ")[2])


class MemoryRange:
    def __init__(self, start, end, priority, kind, name):
        assert priority == 0
        self.start, self.end, self.kind, self.name = start, end, kind, name

    @staticmethod
    def parse(line):
        # example:
        # "  0000000000000000-000000000000ffff (prio 0, i/o): io"
        parts = line.strip().replace(", ", " ").replace("-", " ", 1).replace("): ", " ").split(" ")
        assert len(parts) == 6, "invalid parts: %r from parsing %r" % (parts, line)
        start_range, end_range, prio_text, prio_num, kind, name = parts
        assert len(start_range) == len(end_range) == len("000000000000ffff")
        assert prio_text == "(prio"
        assert kind in ("i/o", "ram", "romd"), "TODO: add support for memory kind %s" % kind
        return MemoryRange(
            start = int(start_range, 16),
            end = int(end_range, 16),
            priority = int(prio_num, 10),
            kind = kind,
            name = name,
        )


class FlatView:
    def __init__(self):
        self.ranges = []

    @staticmethod
    def parse(lines):
        fv = FlatView()
        for line in lines:
            fv.ranges.append(MemoryRange.parse(line))
        return fv

    def ram_ranges(self):
        return [(r.start, r.end) for r in self.ranges if r.kind == "ram"]

    def random_address(self):
        ranges = self.ram_ranges()
        lens = [(end - start) for start, end in ranges]
        offset = random.randint(0, sum(lens) - 1)
        for start, end in ranges:
            offset += start
            if offset < end:
                return offset
            offset -= end
        assert False, "should have been in range!"


def mtree():
    # TODO: clean up this parser to handle errors more cleanly
    lines = qemu_hmp("info mtree -f").split("\n")
    views = {}
    curnames = None
    scanning = False
    for line in lines:
        line = line.rstrip()
        if line.startswith("FlatView #"):
            curnames = []
            scanning = False
        elif line.startswith(' AS "'):
            assert line.count('"') == 2, "invalid AS line: %r" % line
            assert not scanning, "expected not scanning"
            cn = line.split('"')[1]
            curnames.append(cn)
            views[cn] = []
        elif line.startswith(' Root '):
            assert not scanning, "expected not scanning"
            scanning = True
        elif line.startswith('  No rendered FlatView'):
            assert scanning, "expected scanning"
            for cn in curnames:
                assert views[cn] == []
                del views[cn]
            curnames = None
        elif line.startswith('  '):
            assert scanning and curnames, "invalid state to begin view"
            for name in curnames:
                views[name].append(line)
        else:
            assert not line, "unexpected line: %r" % line

    return {name: FlatView.parse(body) for name, body in views.items()}


def sample_address():
    return mtree()["memory"].random_address()


def sample_geo(r):
    return math.ceil(math.log(random.random()) / math.log(1 - r))


def mtbf_to_rate(mtbf):
    return 1 - 0.5 ** (1 / mtbf)


time_units = {
    "": 1,
    "ns": 1,
    "us": 1000,
    "ms": 1000 * 1000,
    "s": 1000 * 1000 * 1000,
    "m": 60 * 1000 * 1000 * 1000,
}


def parse_time(s):
    for unit, mul in sorted(time_units.items()):
        if s.endswith(unit):
            try:
                res = int(s[:-len(unit)])
            except ValueError:
                continue  # try the next unit
            if res <= 0:
                raise ValueError("expected positive number of %s in %r" % (unit, s))
            return res * mul
    raise ValueError("could not parse units in %r" % s)


def step_ns(ns):
    start = now()
    gdb.execute("monitor stop_delayed %d" % ns)
    gdb.execute("continue")
    end = now()
    print("Executed from t=%d ns to t=%d ns (total = %d ns)" % (start, end, end - start))


class CSVWriter:
    def __init__(self):
        self.writer = None
        self.flushable = None
        self.injection_num = 0

    def open(self, pathname):
        if self.writer is not None:
            print("CSV already opened. Not reopening.")
            return
        self.flushable = open(pathname, 'w', newline='')
        self.writer = csv.writer(self.flushable)
        self.writer.writerow(['Injection #', 'Injection Time', 'Address', 'Old Value', 'New Value'])
        self.flushable.flush()
        print("Writing log of injections to", pathname)

    def write(self, address, old_value, new_value):
        if self.writer is None:
            # not recording
            return
        self.injection_num += 1
        self.writer.writerow([str(self.injection_num), str(now()), hex(address), hex(old_value), hex(new_value)])
        self.flushable.flush()

    def close(self):
        if self.writer is None:
            print("CSV not open. Cannot close.")
            return
        self.flushable.close()
        self.writer = None
        self.flushable = None


global_writer = CSVWriter()


def inject_bitflip(address, bytewidth):
    assert bytewidth >= 1, "invalid bytewidth: %d" % bytewidth
    inferior = gdb.selected_inferior()
    # endianness doesn't actually matter for this purpose, so always use little-endian
    ovalue = int.from_bytes(inferior.read_memory(address, bytewidth), "little")
    nvalue = ovalue ^ (1 << random.randint(0, bytewidth * 8 - 1))
    inferior.write_memory(address, int.to_bytes(nvalue, bytewidth, "little"))

    rnvalue = int.from_bytes(inferior.read_memory(address, bytewidth), "little")

    assert nvalue == rnvalue and nvalue != ovalue, "mismatched values: o=0x%x n=0x%x rn=0x%x" % (ovalue, nvalue, rnvalue)
    global_writer.write(address, ovalue, nvalue)
    print("Injected bitflip into address 0x%x: old value 0x%x -> new value 0x%x" % (address, ovalue, nvalue))


cached_reg_list = None
def list_registers():
    global cached_reg_list
    if cached_reg_list is None:
        # we can avoid needing to handle float and 'union neon_q', because on ARM, there are d# registers that alias
        # to all of the more specialized registers in question.
        cached_reg_list = [(r, gdb.selected_frame().read_register(r).type.sizeof)
                           for r in gdb.selected_frame().architecture().registers()
                           if str(gdb.selected_frame().read_register(r).type) not in ("float", "union neon_q")]
    return cached_reg_list[:]


def inject_register_bitflip(register_name):
    value = gdb.selected_frame().read_register(register_name)
    if str(value.type) in ("long", "long long", "void *", "void (*)()"):
        lookup = None
    elif str(value.type) == "union neon_d":
        lookup = "u64"
    else:
        raise RuntimeError("not handled: inject_register_bitflip into register %s of type %s" % (register_name, value.type))

    intval = int(value[lookup] if lookup else value)
    bitcount = 8 * value.type.sizeof
    bitmask = (1 << bitcount) - 1
    newval = intval ^ (1 << random.randint(0, bitcount - 1))
    gdb.execute("set $%s%s = %d" % (register_name, "." + lookup if lookup else "", newval))

    reread = gdb.selected_frame().read_register(register_name)
    rrval = int(reread[lookup] if lookup else reread)
    if (newval & bitmask) == (rrval & bitmask):
        print("Injected bitflip into register %s: old value 0x%x -> new value 0x%x" % (register_name, intval, rrval))
        return True
    elif (intval & bitmask) == (rrval & bitmask):
        print("Bitflip could not be injected into register %s. (0x%x -> 0x%x ignored.)" % (register_name, intval, newval))
        return False
    else:
        raise RuntimeError("double-mismatched register values on register %s: o=0x%x n=0x%x rr=0x%x" % (register_name, intval, newval, rrval))


class BuildCmd(gdb.Command):
    def __init__(self, target):
        self.__doc__ = target.__doc__
        super(BuildCmd, self).__init__(
            target.__name__, gdb.COMMAND_USER
        )
        self.target = target

    def complete(self, text, word):
        return gdb.COMPLETE_NONE

    def invoke(self, args, from_tty):
        return self.target(args)


@BuildCmd
def listram(args):
    """List all RAM ranges allocated by QEMU."""

    print("QEMU RAM list:")
    memory = mtree()["memory"]
    for start, end in memory.ram_ranges():
        print("  RAM allocated from %x to %x" % (start, end))
    print("Sampled index: %x" % memory.random_address())


@BuildCmd
def listreg(args):
    """List all CPU registers available in QEMU."""

    print("QEMU CPU register list:")
    lr = list_registers()
    maxlen = max(len(r.name) for r, nb in lr)
    for register, num_bytes in lr:
        print("  REG:", register.name.rjust(maxlen), "->", num_bytes)


@BuildCmd
def stepvt(args):
    """Step QEMU virtual time forward by the specified amount of time, defaulting to nanoseconds."""

    args = args.strip()
    if not args:
        print("Expected time argument.")
        return

    ns = parse_time(args)
    assert ns > 0

    step_ns(ns)


@BuildCmd
def log_inject(args):
    """Log all injections to a CSV file"""

    args = args.strip().rsplit(" ", 1)
    if len(args) != 1:
        print("usage: log_inject <filename>")
        print("Log all injections to a CSV file.")
        print("usage: log_inject --close")
        print("Stop logging injections.")
        return

    if args[0] == "--close":
        global_writer.close()
    else:
        global_writer.open(args[0])


@BuildCmd
def inject(args):
    """Inject a bitflip at an address."""

    args = args.strip().rsplit(" ", 1)
    if len(args) > 2:
        print("usage: inject [<address>] [<bytewidth>]")
        print("if no address specified, will be randomly selected, and bytewidth will default to 1")
        print("otherwise, bytewidth defaults to 4 bytes")
        return

    if args and args[0]:
        address = int(gdb.parse_and_eval(args[0]))
        bytewidth = int(args[1]) if args[1:] else 4
        if bytewidth < 1 or address < 0:
            print("invalid bytewidth or address")
            return
    else:
        address = sample_address()
        bytewidth = 1

    inject_bitflip(address, bytewidth)


def inject_reg_internal(register_name):
    registers = [r.name for r, nb in list_registers()]
    if register_name:
        regexp = re.compile("^" + ".*".join(re.escape(segment) for segment in register_name.split("*")) + "$")
        registers = [rname for rname in registers if regexp.match(rname)]
    if not registers:
        print("No registers found!")
        return
    # this is the order to try them in
    random.shuffle(registers)

    for reg in registers:
        # keep retrying until we find a register that we CAN successfully inject into
        if inject_register_bitflip(reg):
            break

        print("Trying another register...")
    else:
        print("Out of registers to try!")


@BuildCmd
def inject_reg(args):
    """Inject a bitflip into a register."""

    arg = args.strip()
    if arg.count(" ") > 0:
        print("usage: inject_reg [<register name>]")
        print("if no register specified, will be randomly selected")
        print("a pattern involving wildcards can be specified if desired")
        return

    inject_reg_internal(register_name=arg)


@BuildCmd
def stepmtbf(args):
    """Fast-forward to the next sampled injection event."""

    args = args.strip()
    if not args:
        print("Expected time argument.")
        return

    mtbf = parse_time(args)
    assert mtbf > 0

    ns_to_failure = sample_geo(mtbf_to_rate(mtbf))
    step_ns(ns_to_failure)


@BuildCmd
def campaign(args):
    """Repeatedly inject bitflips into system and then fast-forwards."""

    args = args.strip().split(" ")
    if len(args) < 2 or len(args) > 4:
        print("usage: campaign <iterations> <mtbf> [<address>] [<bytewidth>]")
        print("bytewidth defaults to 4 bytes if address specified")
        print("if <address> starts with reg, then register injections are performed instead")
        print("specify address as reg:X to specify register X for the register injection")
        return

    iterations = int(args[0])
    mtbf = parse_time(args[1])
    assert mtbf > 0

    is_reg = False

    if args[2:]:
        rand_addr = False
        if args[2].startswith("reg") and args[2][3:4] in (":", ""):
            is_reg = True
            address = args[2][4:]
        else:
            address = int(gdb.parse_and_eval(args[2]))
            bytewidth = int(args[3]) if args[3:] else 4
            if iterations < 1 or mtbf <= 0 or bytewidth < 1 or address < 0:
                print("Invalid number for argument.")
                return
    else:
        rand_addr = True
        bytewidth = 1

    for i in range(iterations):
        if is_reg:
            inject_reg_internal(address)
        else:
            if rand_addr:
                address = sample_address()
            inject_bitflip(address, bytewidth)

        ns_to_failure = sample_geo(mtbf_to_rate(mtbf))
        step_ns(ns_to_failure)