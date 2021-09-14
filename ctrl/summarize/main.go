package main

import (
	"encoding/csv"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path"
	"reflect"
	"strconv"
	"strings"
)

type Trial struct {
	TrialName     string
	ReqFailTime   float64
	CommCeaseTime float64
	ReqFailed     string
}

func SaveCSV(rowSlice interface{}, filename string) (errOut error) {
	rowSliceV := reflect.ValueOf(rowSlice)
	rowSliceT := rowSliceV.Type()
	if rowSliceT.Kind() != reflect.Slice {
		return errors.New("not a slice")
	}
	rowT := rowSliceT.Elem()
	if rowT.Kind() != reflect.Struct {
		return errors.New("not a struct in the slice")
	}
	var columns []string
	for fieldI := 0; fieldI < rowT.NumField(); fieldI++ {
		field := rowT.Field(fieldI)
		if field.Name == "" {
			return errors.New("empty field name")
		}
		columns = append(columns, field.Name)
	}
	fileOut, err := os.Create(filename)
	if err != nil {
		return err
	}
	defer func() {
		err := fileOut.Close()
		if err != nil && errOut == nil {
			errOut = err
		}
	}()
	cw := csv.NewWriter(fileOut)
	if err := cw.Write(columns); err != nil {
		return err
	}
	for i := 0; i < rowSliceV.Len(); i++ {
		rowV := rowSliceV.Index(i)
		cells := make([]string, len(columns))
		for j := 0; j < len(cells); j++ {
			cells[j] = fmt.Sprint(rowV.Field(j).Interface())
		}
		if err := cw.Write(cells); err != nil {
			return err
		}
	}
	cw.Flush()
	return cw.Error()
}

func LoadTrial(trialdir string) (Trial, error) {
	lines, err := ioutil.ReadFile(path.Join(trialdir, "requirements.log"))
	if err != nil {
		return Trial{}, err
	}
	var elapsed float64 = -1
	var ceased float64 = -1
	failedReq := ""
	for _, line := range strings.Split(string(lines), "\n") {
		if strings.Contains(line, "time elapsed is ") && strings.Count(line, " ") == 5 {
			elapsed, err = strconv.ParseFloat(strings.Split(line, " ")[4], 64)
			if err != nil {
				return Trial{}, err
			}
			if !(elapsed > 0) {
				return Trial{}, fmt.Errorf("invalid elapsed time: %v", elapsed)
			}
		} else if strings.HasPrefix(line, "  [") && strings.Contains(line, "Failed:") {
			reduced := line[3:]
			reduced = strings.ReplaceAll(reduced, " ", "")
			reduced = strings.ReplaceAll(reduced, "]", ":")
			reduced = strings.ReplaceAll(reduced, ",", ":")
			parts := strings.Split(reduced, ":")
			if len(parts) != 7 {
				return Trial{}, fmt.Errorf("wrong number of parts in Failed: line: %q -> %d", reduced, len(parts))
			}
			failures, err := strconv.ParseUint(parts[4], 10, 32)
			if err != nil {
				return Trial{}, err
			}
			curReq := parts[0]
			if failures > 0 {
				if failedReq != "" {
					log.Printf("WARNING: found more than one failed req: %q %q in %v", failedReq, parts[0], trialdir)
					failedReq += "+" + curReq
				} else {
					failedReq = curReq
				}
			}
		} else if strings.HasPrefix(line, "Experiment: monitor reported I/O ceased at ") && strings.HasSuffix(line, " seconds") && strings.Count(line, " ") == 7 {
			ceased, err = strconv.ParseFloat(strings.Split(line, " ")[6], 64)
			if err != nil {
				return Trial{}, err
			}
			if ceased < 0 {
				return Trial{}, fmt.Errorf("invalid ceased time: %v", ceased)
			}
		}
	}
	return Trial{
		TrialName:     path.Base(trialdir),
		ReqFailTime:   elapsed,
		CommCeaseTime: ceased,
		ReqFailed:     failedReq,
	}, nil
}

const SummaryName = "summary.csv"

func main() {
	workdir := "trials"

	var trials []Trial
	fis, err := ioutil.ReadDir(workdir)
	if err != nil {
		log.Fatal(err)
	}
	for _, fi := range fis {
		name := fi.Name()
		if name == SummaryName || strings.HasPrefix(name, ".") {
			continue
		}
		if !fi.IsDir() {
			log.Fatalf("not a directory: %s", name)
		}
		if !strings.HasPrefix(name, "W") {
			log.Fatalf("bad trial name format: %s", name)
		}
		t, err := LoadTrial(path.Join(workdir, name))
		if err != nil {
			log.Fatal(err)
		}
		trials = append(trials, t)
	}

	sumPath := path.Join(workdir, SummaryName)
	if err := SaveCSV(trials, sumPath); err != nil {
		// make sure summary file is removed, if possible
		_ = os.Remove(sumPath)
		log.Fatal(err)
	}
}
