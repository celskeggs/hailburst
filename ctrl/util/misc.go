package util

import "os"

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
