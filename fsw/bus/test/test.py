#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys

N = 2000
MAXPROC = 25

if (len(sys.argv) > 3 or
        (len(sys.argv) >= 2 and not sys.argv[1].isdigit()) or
        (len(sys.argv) >= 3 and not sys.argv[2].isdigit())):
    sys.exit("Usage: test.py <TRIALS> <MAXPROC>")

if len(sys.argv) >= 2:
    N = int(sys.argv[1])
if len(sys.argv) >= 3:
    MAXPROC = int(sys.argv[2])

print("%d trials, %d processes" % (N, MAXPROC))

processes = []
failed = []


def scan():
    global errors
    nproc = []
    for pdir, proc in processes:
        if proc.poll() is not None:
            if proc.returncode == 0:
                print("Done:", pdir)
            else:
                print("Error:", pdir, proc.returncode)
                failed.append(pdir)
        else:
            nproc.append((pdir, proc))
    processes[:] = nproc


testdir = "testdir/"
if os.path.exists(testdir):
    shutil.rmtree(testdir)
os.mkdir(testdir)

for i in range(N):
    pdir = os.path.join(testdir, "test-p%d/" % i)
    os.mkdir(pdir)
    while len(processes) >= MAXPROC:
        scan()
    with open(os.path.join(pdir, "out.log"), "w") as out:
        proc = subprocess.Popen(["./build-linux/exchange_test", pdir], stdout=out, stderr=subprocess.STDOUT)
    processes.append((pdir, proc))

while len(processes) > 0:
    scan()

print("Total number of errors:", len(failed))
for f in failed:
    print("->", f)
