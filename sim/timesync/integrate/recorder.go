package integrate

import (
	"encoding/csv"
	"encoding/hex"
	"github.com/celskeggs/hailburst/sim/model"
	"log"
	"os"
	"strconv"
)

type Recorder struct {
	sim    model.SimContext
	output *csv.Writer
}

const (
	ToSoftware = "ToSoftware"
	ToHardware = "ToHardware"
)

func (r *Recorder) Record(direction string, from []byte) {
	err := r.output.Write([]string{
		strconv.FormatUint(r.sim.Now().Nanoseconds(), 10),
		direction,
		hex.EncodeToString(from),
	})
	r.output.Flush()
	if err == nil {
		err = r.output.Error()
	}
	if err != nil {
		log.Fatal(err)
	}
}

func MakeRecorder(sim model.SimContext, path string) *Recorder {
	w, err := os.Create(path)
	if err != nil {
		log.Fatal(err)
	}
	cw := csv.NewWriter(w)
	err = cw.Write([]string{"Nanoseconds", "Direction", "Hex Bytes"})
	cw.Flush()
	if err == nil {
		err = cw.Error()
	}
	if err != nil {
		log.Fatal(err)
	}

	return &Recorder{
		sim:    sim,
		output: cw,
	}
}
