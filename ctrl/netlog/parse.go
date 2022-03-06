package main

import (
	"encoding/csv"
	"encoding/hex"
	"errors"
	"fmt"
	"github.com/celskeggs/hailburst/sim/model"
	"io"
	"os"
	"strconv"
)

type Record struct {
	Timestamp model.VirtualTime
	Channel   string
	DataBytes []byte
}

// TODO: unify this with the implementation in ctrl/chart/scans

func ReadIODump(path string) ([]Record, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	cr := csv.NewReader(f)
	cr.FieldsPerRecord = 3
	firstRow, err := cr.Read()
	if err != nil {
		if err == io.EOF {
			err = io.ErrUnexpectedEOF
		}
		return nil, err
	}
	if len(firstRow) != 3 || firstRow[0] != "Nanoseconds" || firstRow[1] != "Channel" || firstRow[2] != "Hex Bytes" {
		return nil, fmt.Errorf("invalid first row: %v", firstRow)
	}

	var records []Record
	for {
		row, err := cr.Read()
		if err == io.EOF {
			break
		} else if err != nil {
			return nil, err
		}
		// decode timestamp
		eventNs, err := strconv.ParseUint(row[0], 10, 64)
		if err != nil {
			return nil, err
		}
		eventTime, ok := model.FromNanoseconds(eventNs)
		if !ok {
			return nil, fmt.Errorf("invalid timestamp: %v", eventNs)
		}
		// decode channel
		channel := row[1]
		if channel == "" {
			return nil, errors.New("invalid empty string for channel")
		}
		// decode data
		hexBytes, err := hex.DecodeString(row[2])
		if err != nil {
			return nil, err
		}
		// insert into scan
		records = append(records, Record{
			Timestamp: eventTime,
			Channel:   channel,
			DataBytes: hexBytes,
		})
	}
	return records, nil
}
