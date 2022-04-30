package viterbi

import "github.com/celskeggs/hailburst/sim/verifier"

type HiddenState uint

const (
	FullyWorking HiddenState = iota
	PartiallyWorking
	CompletelyBroken
	NumHiddenStates
)

type ViterbiState struct {
	// variable
	Backtrack   [][NumHiddenStates]HiddenState // at each time step, the most likely prior state
	Likelihoods [NumHiddenStates]float64
	// constant
	StateTransitions [NumHiddenStates][NumHiddenStates]float64
	SuccessDefaults [NumHiddenStates]float64
	SuccessOverrides map[string][NumHiddenStates]float64
}

type ViterbiObservation struct {
	Requirement string
	Success     bool
}

func DefaultStateTransitions() (l [NumHiddenStates][NumHiddenStates]float64) {
	l[FullyWorking][FullyWorking] = 0.97
	l[FullyWorking][PartiallyWorking] = 0.02
	l[FullyWorking][CompletelyBroken] = 0.01

	l[PartiallyWorking][FullyWorking] = 0.01
	l[PartiallyWorking][PartiallyWorking] = 0.99
	l[PartiallyWorking][CompletelyBroken] = 0.00

	l[CompletelyBroken][FullyWorking] = 0.01
	l[CompletelyBroken][PartiallyWorking] = 0.00
	l[CompletelyBroken][CompletelyBroken] = 0.99
	return
}

func DefaultSuccessLikelihoods() (defaults [NumHiddenStates]float64, l map[string][NumHiddenStates]float64) {
	defaults[FullyWorking] = 1.00
	defaults[PartiallyWorking] = 0.70
	defaults[CompletelyBroken] = 0.05

	l = map[string][NumHiddenStates]float64{}

	var ll [NumHiddenStates]float64
	ll[FullyWorking] = 1.00
	ll[PartiallyWorking] = 0.70
	ll[CompletelyBroken] = 0.05
	l[verifier.ReqMagSetPwr] = ll

	ll[FullyWorking] = 1.00
	ll[PartiallyWorking] = 0.70
	ll[CompletelyBroken] = 0.05
	l[verifier.ReqCollectMagReadings] = ll

	ll[FullyWorking] = 1.00
	ll[PartiallyWorking] = 0.70
	ll[CompletelyBroken] = 0.05
	l[verifier.ReqHeartbeat] = ll

	ll[FullyWorking] = 1.00
	ll[PartiallyWorking] = 0.97
	ll[CompletelyBroken] = 0.97
	l[verifier.ReqDownlinkMagReadings] = ll

	ll[FullyWorking] = 1.00
	ll[PartiallyWorking] = 0.75
	ll[CompletelyBroken] = 0.99
	l[verifier.ReqNoTelemErrs] = ll
	return
}

func InitialViterbiState() ViterbiState {
	transitions := DefaultStateTransitions()
	defaults, overrides := DefaultSuccessLikelihoods()
	return ViterbiState{
		Likelihoods: [NumHiddenStates]float64{
			0.98,
			0.01,
			0.01,
		},
		StateTransitions: transitions,
		SuccessDefaults:  defaults,
		SuccessOverrides: overrides,
	}
}

func (v *ViterbiState) observationLikelihood(observation ViterbiObservation) [NumHiddenStates]float64 {
	likelihoods, found := v.SuccessOverrides[observation.Requirement]
	if !found {
		likelihoods = v.SuccessDefaults
	}
	if !observation.Success {
		for i := HiddenState(0); i < NumHiddenStates; i++ {
			likelihoods[i] = 1 - likelihoods[i]
		}
	}
	return likelihoods
}

func (v *ViterbiState) mergeObservations(observations []ViterbiObservation) (l [NumHiddenStates]float64) {
	// start with uniform likelihood
	for i := HiddenState(0); i < NumHiddenStates; i++ {
		l[i] = 1.0
	}
	// apply all observations
	for _, observation := range observations {
		mul := v.observationLikelihood(observation)
		for i := HiddenState(0); i < NumHiddenStates; i++ {
			l[i] *= mul[i]
		}
	}
	return
}

func (v *ViterbiState) NextPeriod(observations []ViterbiObservation) {
	priors := v.Likelihoods
	posteriors := [NumHiddenStates]float64{}
	backtrack := [NumHiddenStates]HiddenState{}

	lObs := v.mergeObservations(observations)

	var largest float64 // for scaling
	for outcome := HiddenState(0); outcome < NumHiddenStates; outcome++ {
		var bestLikelihood float64
		var bestPrevious HiddenState
		for previous := HiddenState(0); previous < NumHiddenStates; previous++ {
			likelihood := priors[previous] * v.StateTransitions[previous][outcome] * lObs[outcome]
			if likelihood > bestLikelihood {
				bestLikelihood = likelihood
				bestPrevious = previous
			}
		}
		posteriors[outcome] = bestLikelihood
		backtrack[outcome] = bestPrevious
		if bestLikelihood > largest {
			largest = bestLikelihood
		}
	}
	if largest <= 0 {
		panic("viterbi collapsed to no possibilities")
	}
	// rescale outcomes so that the most probable outcome is measured at 1.0
	for outcome := HiddenState(0); outcome < NumHiddenStates; outcome++ {
		posteriors[outcome] /= largest
	}

	v.Likelihoods = posteriors
	v.Backtrack = append(v.Backtrack, backtrack)
}

func (v *ViterbiState) ExtractPath() []HiddenState {
	var bestLikelihood float64
	var bestOutcome HiddenState
	for outcome := HiddenState(0); outcome < NumHiddenStates; outcome++ {
		likelihood := v.Likelihoods[outcome]
		if likelihood > bestLikelihood {
			bestLikelihood = likelihood
			bestOutcome = outcome
		}
	}
	// generate path in reverse
	reversePath := make([]HiddenState, len(v.Backtrack) + 1)
	reversePath[len(v.Backtrack)] = bestOutcome
	lastOutcome := bestOutcome
	for i := len(v.Backtrack) - 1; i >= 0; i-- {
		lastOutcome = v.Backtrack[i][lastOutcome]
		reversePath[i] = lastOutcome
	}
	return reversePath
}
