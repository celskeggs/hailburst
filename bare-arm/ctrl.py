import csv
import math
import random

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

    assert nvalue == rnvalue and nvalue != ovalue, "mismatched values: o=%x n=%x rn=%x" % (ovalue, nvalue, rnvalue)
    global_writer.write(address, ovalue, nvalue)
    print("Injected bitflip into address %x: old value %x -> new value %x" % (address, ovalue, nvalue))


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
        return

    iterations = int(args[0])
    mtbf = parse_time(args[1])
    assert mtbf > 0

    if args[2:]:
        rand_addr = False
        address = int(gdb.parse_and_eval(args[2]))
        bytewidth = int(args[3]) if args[3:] else 4
        if iterations < 1 or mtbf <= 0 or bytewidth < 1 or address < 0:
            print("Invalid number for argument.")
            return
    else:
        rand_addr = True
        bytewidth = 1

    for i in range(iterations):
        if rand_addr:
            address = sample_address()
        inject_bitflip(address, bytewidth)

        ns_to_failure = sample_geo(mtbf_to_rate(mtbf))
        step_ns(ns_to_failure)


