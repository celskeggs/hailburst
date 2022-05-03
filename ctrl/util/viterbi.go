package util

type ViterbiState struct {
	// variable
	Backtrack   [][]uint // at each time step, the most likely prior state
	Likelihoods []float64
	// constant
	NumHiddenStates uint
	StateTransitions [][]float64
	ObservationLikelihoodFn func(interface{})[]float64
}

func (v *ViterbiState) NextPeriod(observation interface{}) {
	priors := v.Likelihoods
	posteriors := make([]float64, v.NumHiddenStates)
	backtrack := make([]uint, v.NumHiddenStates)

	lObs := v.ObservationLikelihoodFn(observation)

	var largest float64 // for scaling
	for outcome := uint(0); outcome < v.NumHiddenStates; outcome++ {
		var bestLikelihood float64
		var bestPrevious uint
		for previous := uint(0); previous < v.NumHiddenStates; previous++ {
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
	for outcome := uint(0); outcome < v.NumHiddenStates; outcome++ {
		posteriors[outcome] /= largest
	}

	v.Likelihoods = posteriors
	v.Backtrack = append(v.Backtrack, backtrack)
}

func (v *ViterbiState) ExtractPath() []uint {
	var bestLikelihood float64
	var bestOutcome uint
	for outcome := uint(0); outcome < v.NumHiddenStates; outcome++ {
		likelihood := v.Likelihoods[outcome]
		if likelihood > bestLikelihood {
			bestLikelihood = likelihood
			bestOutcome = outcome
		}
	}
	// generate path in reverse
	reversePath := make([]uint, len(v.Backtrack) + 1)
	reversePath[len(v.Backtrack)] = bestOutcome
	lastOutcome := bestOutcome
	for i := len(v.Backtrack) - 1; i >= 0; i-- {
		lastOutcome = v.Backtrack[i][lastOutcome]
		reversePath[i] = lastOutcome
	}
	return reversePath
}
