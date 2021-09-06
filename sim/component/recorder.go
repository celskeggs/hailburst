package component

import (
	"encoding/csv"
	"encoding/hex"
	"errors"
	"fmt"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/hashicorp/go-multierror"
	"log"
	"os"
	"strconv"
)

type CSVByteRecorder struct {
	sim    model.SimContext
	output *csv.Writer
}

func (r *CSVByteRecorder) IsRecording() bool {
	return r.output != nil
}

func (r *CSVByteRecorder) Record(channel string, dataBytes []byte) {
	if channel == "" {
		panic("invalid empty channel name")
	}
	if r.output == nil {
		// not recording; discard
		return
	}
	err := r.output.Write([]string{
		strconv.FormatUint(r.sim.Now().Nanoseconds(), 10),
		channel,
		hex.EncodeToString(dataBytes),
	})
	r.output.Flush()
	if err == nil {
		err = r.output.Error()
	}
	if err != nil {
		log.Fatal(err)
	}
}

func MakeNullCSVRecorder() *CSVByteRecorder {
	return &CSVByteRecorder{
		output: nil,
	}
}

func MakeCSVRecorder(sim model.SimContext, path string) *CSVByteRecorder {
	w, err := os.Create(path)
	if err != nil {
		log.Fatal(err)
	}
	cw := csv.NewWriter(w)
	err = cw.Write([]string{"Nanoseconds", "Channel", "Hex Bytes"})
	cw.Flush()
	if err == nil {
		err = cw.Error()
	}
	if err != nil {
		log.Fatal(err)
	}

	return &CSVByteRecorder{
		sim:    sim,
		output: cw,
	}
}

type Record struct {
	Timestamp model.VirtualTime
	Channel   string
	Bytes     []byte
}

func DecodeRecording(path string) (records []Record, re error) {
	r, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer func() {
		if err := r.Close(); err != nil {
			re = multierror.Append(re, err)
		}
	}()
	recordsRaw, err := csv.NewReader(r).ReadAll()
	if err != nil {
		return nil, err
	}
	if len(recordsRaw) < 1 {
		return nil, errors.New("no header found")
	}
	if len(recordsRaw[0]) != 3 || recordsRaw[0][0] != "Nanoseconds" || recordsRaw[0][1] != "Channel" || recordsRaw[0][2] != "Hex Bytes" {
		return nil, fmt.Errorf("invalid header: %v", recordsRaw[0])
	}
	for _, record := range recordsRaw[1:] {
		if len(record) != 3 {
			return nil, fmt.Errorf("invalid data record: %v", record)
		}
		// decode timestamp
		timestampNS, err := strconv.ParseUint(record[0], 10, 64)
		if err != nil {
			return nil, err
		}
		timestamp, ok := model.FromNanoseconds(timestampNS)
		if !ok {
			return nil, fmt.Errorf("invalid timestamp: %v", record[0])
		}
		// decode channel
		channel := record[1]
		if channel == "" {
			return nil, errors.New("invalid empty string channel")
		}
		// decode hex bytes
		dataBytes, err := hex.DecodeString(record[2])
		if err != nil {
			return nil, err
		}
		records = append(records, Record{
			Timestamp: timestamp,
			Channel:   channel,
			Bytes:     dataBytes,
		})
	}
	return records, nil
}
