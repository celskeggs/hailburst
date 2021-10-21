package main

import (
	"encoding/json"
	"github.com/celskeggs/hailburst/ctrl/trial"
	"log"
	"os"
)

func ParseBoolSetting(arg, falseStr, trueStr, errMsg string) bool {
	if arg == falseStr {
		return false
	} else if arg == trueStr {
		return true
	} else {
		log.Fatalf("%s: %s", errMsg, arg)
		return false // just to make Go happy; will never run
	}
}

func main() {
	if len(os.Args) != 2 {
		log.Fatalf("usage: %s <JSON>", os.Args[0])
	}

	var options trial.Options
	if err := json.Unmarshal([]byte(os.Args[1]), &options); err != nil {
		log.Fatal(err)
	}

	if err := os.Mkdir(options.TrialDir, 0777); err != nil {
		log.Fatal(err)
	}

	options.Launch()
}
