package viterbi

import (
	"github.com/celskeggs/hailburst/ctrl/util"
)

type IndivHiddenState uint

const (
	Passing IndivHiddenState = iota
	Recovering
	Partial
	Degrading
	Failing
	Absent
	NumIndivHiddenStates
)

func DefaultIndivStateTransitions() (l [][]float64) {
	l = make([][]float64, NumIndivHiddenStates)
	l[Passing] = make([]float64, NumIndivHiddenStates)
	l[Passing][Passing] = 0.994
	l[Passing][Degrading] = 0.002
	l[Passing][Partial] = 0.002
	l[Passing][Absent] = 0.002

	l[Degrading] = make([]float64, NumIndivHiddenStates)
	l[Degrading][Degrading] = 0.5
	l[Degrading][Failing] = 0.5

	l[Failing] = make([]float64, NumIndivHiddenStates)
	l[Failing][Failing] = 0.996
	l[Failing][Recovering] = 0.002
	l[Failing][Absent] = 0.002

	l[Partial] = make([]float64, NumIndivHiddenStates)
	l[Partial][Partial] = 0.996
	l[Partial][Passing] = 0.002
	l[Partial][Degrading] = 0.002

	l[Recovering] = make([]float64, NumIndivHiddenStates)
	l[Recovering][Recovering] = 0.4
	l[Recovering][Passing] = 0.2
	l[Recovering][Partial] = 0.2
	l[Recovering][Absent] = 0.2

	l[Absent] = make([]float64, NumIndivHiddenStates)
	l[Absent][Passing] = 0.002
	l[Absent][Absent] = 0.996
	l[Absent][Degrading] = 0.002

	return
}

func IndivObservationLikelihood(success interface{}) []float64 {
	var l [NumIndivHiddenStates]float64
	if success == nil {
		l[Passing] = 0.9
		l[Partial] = 0.8
		l[Degrading] = 0.8
		l[Recovering] = 0.8
		l[Failing] = 0.9
		l[Absent] = 1.0
	} else if success.(bool) {
		l[Passing] = 1.0
		l[Partial] = 0.8
		l[Degrading] = 0.4
		l[Recovering] = 0.6
		l[Failing] = 0.1
		l[Absent] = 0.0
	} else {
		l[Passing] = 0.0
		l[Partial] = 0.8
		l[Degrading] = 0.6
		l[Recovering] = 0.4
		l[Failing] = 1.0
		l[Absent] = 0.0
	}
	return l[:]
}

func IndividualRequirementState() util.ViterbiState {
	var initialStates [NumIndivHiddenStates]float64
	initialStates[Passing] = 0.97
	initialStates[Partial] = 0.01
	initialStates[Failing] = 0.01
	initialStates[Absent] = 0.01
	return util.ViterbiState{
		Likelihoods:             initialStates[:],
		NumHiddenStates:         uint(NumIndivHiddenStates),
		StateTransitions:        DefaultIndivStateTransitions(),
		ObservationLikelihoodFn: IndivObservationLikelihood,
	}
}
