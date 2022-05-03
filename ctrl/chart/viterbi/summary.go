package viterbi

import (
	"github.com/celskeggs/hailburst/ctrl/util"
	"github.com/celskeggs/hailburst/sim/verifier"
	"log"
	"math"
)

type HiddenState uint

const (
	Working HiddenState = iota
	Broken
	NumHiddenStates
)

func DefaultStateTransitions() (l [][]float64) {
	l = make([][]float64, NumHiddenStates)
	l[Working] = make([]float64, NumHiddenStates)
	l[Working][Working] = 0.999
	l[Working][Broken] = 0.001

	l[Broken] = make([]float64, NumHiddenStates)
	l[Broken][Working] = 0.001
	l[Broken][Broken] = 0.999
	return
}

type ViterbiObservationInfo struct {
	ModeDefaults [NumIndivHiddenStates][NumHiddenStates]float64
	ModeOverrides map[string][NumIndivHiddenStates][NumHiddenStates]float64
}

func DefaultSuccessLikelihoods() *ViterbiObservationInfo {
	voi := &ViterbiObservationInfo{}

	voi.ModeDefaults[Passing][Working] = 1.00
	voi.ModeDefaults[Absent][Working] = 0.90

	voi.ModeDefaults[Passing][Broken] = 0.70
	voi.ModeDefaults[Recovering][Broken] = 0.80
	voi.ModeDefaults[Partial][Broken] = 1.00
	voi.ModeDefaults[Degrading][Broken] = 0.80
	voi.ModeDefaults[Absent][Broken] = 0.60
	voi.ModeDefaults[Failing][Broken] = 1.00

	ll := voi.ModeDefaults // copy
	ll[Passing][Broken] = 1.00
	ll[Absent][Broken] = 0.90
	voi.ModeOverrides = map[string][6][2]float64{
		verifier.ReqNoTelemErrs: ll,
	}

	return voi
}

type SummaryObservation struct {
	Requirement string
	Mode        IndivHiddenState
	Duration    float64
}

func (voi *ViterbiObservationInfo) Likelihood(vo SummaryObservation) [NumHiddenStates]float64 {
	likelihoods, found := voi.ModeOverrides[vo.Requirement]
	if !found {
		likelihoods = voi.ModeDefaults
	}
	l := likelihoods[vo.Mode]
	if vo.Duration != 1.0 {
		if vo.Duration <= 0 || vo.Duration > 1.0 {
			log.Panicf("invalid duration: %f", vo.Duration)
		}
		for i := 0; i < len(l); i++ {
			l[i] = math.Pow(l[i], vo.Duration)
		}
	}
	return l
}

func ViterbiObservationsCombined(voi *ViterbiObservationInfo, observations interface{}) []float64 {
	// start with uniform likelihood
	l := make([]float64, NumHiddenStates)
	for i := HiddenState(0); i < NumHiddenStates; i++ {
		l[i] = 1.0
	}
	// apply all observations
	for _, observation := range observations.([]SummaryObservation) {
		mul := voi.Likelihood(observation)
		for i := HiddenState(0); i < NumHiddenStates; i++ {
			l[i] *= mul[i]
		}
	}
	return l
}

func InitialViterbiState() util.ViterbiState {
	transitions := DefaultStateTransitions()
	voi := DefaultSuccessLikelihoods()
	return util.ViterbiState{
		Likelihoods: []float64{
			0.99,
			0.01,
		},
		NumHiddenStates:         uint(NumHiddenStates),
		StateTransitions:        transitions,
		ObservationLikelihoodFn: func(observations interface{}) []float64 {
			return ViterbiObservationsCombined(voi, observations)
		},
	}
}
