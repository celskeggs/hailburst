import os
import shutil
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.enums import ENUM_RELOC_TYPE_ARM
from elftools.elf.relocation import RelocationHandler

POINTER_SIZE = 4
REPLICA_SECTION = "replicas"
RELOC_TYPE = ENUM_RELOC_TYPE_ARM['R_ARM_ABS32']

linker = None
archiver = None
objcopy = None
replica_script = None
excise = None
workdir = None
passthrough = []
inputs = []

argv = sys.argv[1:]
while argv:
    if argv[0] == "--linker" and len(argv) > 1:
        linker = argv[1]
        argv = argv[2:]
    elif argv[0] == "--archiver" and len(argv) > 1:
        archiver = argv[1]
        argv = argv[2:]
    elif argv[0] == "--objcopy" and len(argv) > 1:
        objcopy = argv[1]
        argv = argv[2:]
    elif argv[0] == "--replica-script" and len(argv) > 1:
        replica_script = argv[1]
        argv = argv[2:]
    elif argv[0] == "--excise" and len(argv) > 1:
        excise = argv[1]
        argv = argv[2:]
    elif argv[0] == "--workdir" and len(argv) > 1:
        workdir = argv[1]
        argv = argv[2:]
    elif argv[0] in ["-o", "-T"] and len(argv) > 1:
        passthrough += argv[0:2]
        argv = argv[2:]
    elif argv[0] in ["--fatal-warnings"]:
        passthrough.append(argv[0])
        argv = argv[1:]
    elif argv[0][0] == "-":
        sys.exit("Unrecognized argument: %s" % argv[0])
    else:
        passthrough.append(argv[0])
        inputs.append(argv[0])
        argv = argv[1:]


if not linker or not archiver or not objcopy or not replica_script or not excise or not workdir:
    sys.exit("Missing linker or archiver or objcopy or replica script or excise or workdir path")


def get_relocation_list(elf):
    replicas = elf.get_section_by_name(REPLICA_SECTION)
    if replicas is None:
        return []
    if replicas.data_size % POINTER_SIZE != 0:
        sys.exit("ERROR: replicas section must be a multiple of the pointer size")
    # data should be all zeroes, because it's all stored in relocations
    if replicas.data() != b"\0" * replicas.data_size:
        sys.exit("ERROR: unexpected nonzero data in replicas section")
    relocs = RelocationHandler(elf).find_relocations_for_section(replicas)
    if relocs.num_relocations() * POINTER_SIZE != replicas.data_size:
        sys.exit("ERROR: inappropriate number of relocations for replicas section size")
    symtable = elf.get_section(relocs['sh_link'])
    symbols = []
    for i in range(replicas.data_size // POINTER_SIZE):
        rl = relocs.get_relocation(i)
        if i * POINTER_SIZE != rl["r_offset"]:
            sys.exit("ERROR: replica mismatched with expected relocation offset")
        if rl["r_info_type"] != RELOC_TYPE:
            sys.exit("ERROR: relocation found to have incorrect type")
        symbols.append(symtable.get_symbol(rl["r_info_sym"]).name)
    return symbols


link_map = {}
direct_objects = []
archives = []
for input in inputs:
    if input.endswith(".o"):
        direct_objects.append(input)
        with open(input, "rb") as f:
            symbols = get_relocation_list(ELFFile(f))
            if len(symbols) % 2 != 0:
                sys.exit("Expected an even number of symbols in replicas section")
            for prime, replica in zip(symbols[::2], symbols[1::2]):
                if replica in link_map:
                    sys.exit("Duplicate request to build symbol replica: %s" % replica)
                link_map[replica] = prime
    elif input.endswith(".a"):
        archives.append(input)
    else:
        sys.exit("Unrecognized file type: %s" % input)


def call_proc(args, ok=(0,)):
    rc = subprocess.call(args)
    if rc not in ok:
        print("Process failed. Arguments: " + " ".join(args), file=sys.stderr)
        sys.exit(1)
    return rc


def main(tempdir):
    shutil.rmtree(tempdir)
    os.mkdir(tempdir)

    linker_args = [linker] + passthrough
    if link_map:
        # we need to protect our replicas from the possibility of replicating global mutable data
        # unfortunately, the linker won't let us discard sections that are actually used... so we have to
        # use objcopy to do it ourselves.
        all_objects = direct_objects[:]
        extractid = 0
        for archive in archives:
            archive_dir = os.path.join(tempdir, "extract%d" % extractid)
            os.mkdir(archive_dir)
            extractid += 1
            call_proc([archiver, "x", "--output", archive_dir, "--", archive])
            all_objects += [os.path.join(archive_dir, o) for o in os.listdir(archive_dir)]

        safety_objects = []
        safeid = 0
        for object in all_objects:
            safe_object = os.path.join(tempdir, "safe%d_%s" % (safeid, os.path.basename(object)))
            safeid += 1
            rc = call_proc([excise, object, safe_object], ok=(0, 42))
            if rc == 42:
                print("Skipping safing object code: %s" % object)
                continue
            assert rc == 0
            safety_objects.append(safe_object)
        safety_archive = os.path.join(tempdir, "safety.a")
        call_proc([archiver, "rc", "--", safety_archive] + safety_objects)

        partials = {}
        for target in sorted(set(link_map.values())):
            assert "/" not in target
            target_path = os.path.join(tempdir, "t_" + target + ".o")
            call_proc([linker, "--relocatable", "--fatal-warnings", "--unresolved-symbols=ignore-all",
                       "-T", replica_script, "-u", target, "-o", target_path, safety_archive])
            partials[target] = target_path

        specials = []
        for replica, target in sorted(link_map.items()):
            target_path = partials[target]
            assert "/" not in replica
            replica_path = os.path.join(tempdir, "r_" + replica + ".o")
            call_proc([objcopy,
                       "--keep-global-symbol=%s" % replica,
                       "--redefine-sym", "%s=%s" % (target, replica),
                       target_path, replica_path])
            specials.append(replica_path)
            print("Replicated object code for symbol: %s -> %s" % (target, replica))

        special_archive = os.path.join(tempdir, "special.a")
        call_proc([archiver, "rc", special_archive] + specials)

        linker_args.append(special_archive)

    call_proc(linker_args)


if __name__ == '__main__':
    main(workdir)
