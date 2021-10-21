package util

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"strconv"
	"strings"
)

func Exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func HasArg(name string) bool {
	for _, a := range os.Args[1:] {
		if a == name {
			return true
		}
	}
	return false
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
