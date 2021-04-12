import math
import random

import gdb


def now():
    text = gdb.execute("monitor info vtime", to_string=True).strip()
    assert text.startswith("virtual time: ") and text.endswith(" ns") and text.count(" ") == 3, "invalid output: %q" % text
    return int(text.split(" ")[2])


def sample_geo(r):
    return math.ceil(math.log(random.random()) / math.log(1 - r))


def mtbf_to_rate(mtbf):
    return 1 - 0.5 ** (1 / mtbf)


def step_ns(ns):
    start = now()
    gdb.execute("monitor stop_delayed %d" % ns)
    gdb.execute("continue")
    end = now()
    print("Executed from t=%d ns to t=%d ns (total = %d ns)" % (start, end, end - start))


def inject_bitflip(address, bytewidth):
    assert bytewidth >= 1, "invalid bytewidth: %d" % bytewidth
    inferior = gdb.selected_inferior()
    # endianness doesn't actually matter for this purpose, so always use little-endian
    ovalue = int.from_bytes(inferior.read_memory(address, bytewidth), "little")
    nvalue = ovalue ^ (1 << random.randint(0, bytewidth * 8 - 1))
    inferior.write_memory(address, int.to_bytes(nvalue, bytewidth, "little"))

    rnvalue = int.from_bytes(inferior.read_memory(address, bytewidth), "little")

    assert nvalue == rnvalue and nvalue != ovalue, "mismatched values: o=%x n=%x rn=%x" % (ovalue, nvalue, rnvalue)
    print("Injected bitflip into address %x: old value %x -> new value %x" % (address, ovalue, nvalue))


class StepNsCmd(gdb.Command):
    """Steps QEMU virtual time forward by the specified number of nanoseconds"""

    def __init__(self):
        super(StepNsCmd, self).__init__(
            "stepns", gdb.COMMAND_USER
        )

    def complete(self, text, word):
        return gdb.COMPLETE_NONE

    def invoke(self, args, from_tty):
        if not args.isdigit():
            print("Expected ns argument.")
            return
        ns = int(args)
        if ns <= 0:
            print("Expected positive number of ns.")
            return
        step_ns(ns)


class Inject(gdb.Command):
    """Injects a bitflip at the specified address"""

    def __init__(self):
        super(Inject, self).__init__(
            "inject", gdb.COMMAND_USER
        )

    def complete(self, text, word):
        return gdb.COMPLETE_NONE

    def invoke(self, args, from_tty):
        args = args.strip().rsplit(" ", 1)
        if len(args) < 1 or len(args) > 2:
            print("usage: inject <address> [<bytewidth>]")
            print("bytewidth defaults to 4 bytes")
            return

        address = int(gdb.parse_and_eval(args[0]))
        bytewidth = int(args[1]) if args[1:] else 4
        if bytewidth < 1 or address < 0:
            print("invalid bytewidth or address")
            return

        inject_bitflip(address, bytewidth)


class StepMTBF(gdb.Command):
    """Fast-forwards to the next sampled injection event."""

    def __init__(self):
        super(StepMTBF, self).__init__(
            "stepmtbf", gdb.COMMAND_USER
        )

    def complete(self, text, word):
        return gdb.COMPLETE_NONE

    def invoke(self, args, from_tty):
        if not args.isdigit():
            print("Expected ns argument.")
            return
        mtbf = int(args)
        if mtbf <= 0:
            print("Expected positive number of ns.")
            return

        ns_to_failure = sample_geo(mtbf_to_rate(mtbf))

        step_ns(ns_to_failure)


class Campaign(gdb.Command):
    """Repeatedly injects bitflips into system and then fast-forwards."""

    def __init__(self):
        super(Campaign, self).__init__(
            "campaign", gdb.COMMAND_USER
        )

    def complete(self, text, word):
        return gdb.COMPLETE_NONE

    def invoke(self, args, from_tty):
        args = args.strip().split(" ")
        if len(args) < 3 or len(args) > 4:
            print("usage: campaign <iterations> <mtbf> <address> [<bytewidth>]")
            print("bytewidth defaults to 4 bytes")
            return

        iterations = int(args[0])
        mtbf = int(args[1])
        address = int(gdb.parse_and_eval(args[2]))
        bytewidth = int(args[3]) if args[3:] else 4
        if iterations < 1 or mtbf <= 0 or bytewidth < 1 or address < 0:
            print("Invalid number for argument.")
            return

        for i in range(iterations):
            inject_bitflip(address, bytewidth)

            ns_to_failure = sample_geo(mtbf_to_rate(mtbf))
            step_ns(ns_to_failure)


StepNsCmd()
Inject()
StepMTBF()
Campaign()
