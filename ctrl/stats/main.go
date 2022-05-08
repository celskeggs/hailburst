package main

import (
	"bytes"
	"encoding/csv"
	"github.com/celskeggs/hailburst/ctrl/chart/scans"
	"io/ioutil"
	"log"
	"os"
	"path"
	"strconv"
	"time"
)

func ScanStats(dirs []string) string {
	var data bytes.Buffer
	c := csv.NewWriter(&data)
	fields := []string{
		"Trial", "MeanTimeToFailure", "MeanTimeToRecovery", "BestTimeToFailure",
		"BestTimeToRecovery", "WorstTimeToFailure", "WorstTimeToRecovery", "OkPercent",
	}
	if err := c.Write(fields); err != nil {
		panic(err)
	}
	for _, dir := range dirs {
		_, stats, err := scans.ScanReqSummaryViterbi(path.Join(dir, "reqs-raw.log"))
		if err != nil {
			log.Printf("Error while scanning %q: %v", dir, err)
			continue
		}
		var columns [8]string
		durations := []time.Duration{
			stats.MeanTimeToFailure, stats.MeanTimeToRecovery, stats.BestTimeToFailure,
			stats.BestTimeToRecovery, stats.WorstTimeToFailure, stats.WorstTimeToRecovery,
		}
		columns[0] = dir
		for i, duration := range durations {
			columns[1+i] = strconv.FormatFloat(duration.Seconds(), 'f', 3, 64)
		}
		columns[7] = strconv.FormatFloat(stats.OkPercent, 'f', 1, 64)
		if err := c.Write(columns[:]); err != nil {
			panic(err)
		}
	}
	c.Flush()
	if err := c.Error(); err != nil {
		panic(err)
	}
	return data.String()
}

func main() {
	if len(os.Args) < 2 {
		log.Fatalf("Usage: %s <trial-dir> <trial-dir> [...]", path.Base(os.Args[0]))
	}
	stats := ScanStats(os.Args[1:])
	err := ioutil.WriteFile("stats.csv", []byte(stats), 0o644)
	if err != nil {
		log.Fatalln(err)
	}
	log.Print("Generated stats.csv.")
}
