package component

import (
	"container/heap"
	"math/rand"
	"sim/model"
	"time"
)

type simTimer struct {
	expireAt model.VirtualTime
	name     string
	callback func()
	index    int
}

type timerQueue []*simTimer

func (tq timerQueue) Len() int {
	return len(tq)
}

func (tq timerQueue) Less(i, j int) bool {
	return tq[i].expireAt.Before(tq[j].expireAt)
}

func (tq timerQueue) Swap(i, j int) {
	tq[i], tq[j] = tq[j], tq[i]
	tq[i].index = i
	tq[j].index = j
}

func (tq *timerQueue) Push(x interface{}) {
	timer := x.(*simTimer)
	timer.index = len(*tq)
	*tq = append(*tq, timer)
}

func (tq *timerQueue) Pop() interface{} {
	tqa := *tq
	timer := tqa[len(tqa)-1]
	timer.index = -1
	*tq = tqa[0 : len(tqa)-1]
	return timer
}

type SimController struct {
	currentTime model.VirtualTime
	rand        *rand.Rand

	timers timerQueue
}

var _ model.SimContext = &SimController{}

func (sc *SimController) Now() model.VirtualTime {
	return sc.currentTime
}

func (sc *SimController) SetTimer(expireAt model.VirtualTime, name string, callback func()) (cancel func()) {
	if !expireAt.TimeExists() {
		panic("attempt to set timer at nonexistent time")
	}
	timer := &simTimer{
		expireAt: expireAt,
		name:     name,
		callback: callback,
		index:    -1,
	}
	heap.Push(&sc.timers, timer)
	if timer.index == -1 {
		panic("should have a real index now")
	}
	return func() {
		if timer.index != -1 {
			heap.Remove(&sc.timers, timer.index)
			if timer.index != -1 {
				panic("should have been removed!")
			}
		}
	}
}

func (sc *SimController) Later(name string, callback func()) (cancel func()) {
	// will cause it to be executed in Advance
	return sc.SetTimer(sc.Now(), name, callback)
}

func (sc *SimController) Rand() *rand.Rand {
	return sc.rand
}

func (sc *SimController) peekNextTimerExpiry() model.VirtualTime {
	if len(sc.timers) > 0 {
		return sc.timers[0].expireAt
	}
	return model.NeverTimeout
}

func (sc *SimController) popNextTimer() *simTimer {
	if len(sc.timers) == 0 {
		panic("cannot pop from empty timer list")
	}
	timer := heap.Pop(&sc.timers).(*simTimer)
	if timer.index != -1 {
		panic("invalid timer index")
	}
	return timer
}

func (sc *SimController) runCurrentTimers() {
	// this loop will keep rerunning as long as we have timers to process at or before the current time
	for len(sc.timers) > 0 && sc.peekNextTimerExpiry().AtOrBefore(sc.Now()) {
		nextTimer := sc.popNextTimer()
		if nextTimer.expireAt.After(sc.Now()) {
			panic("invalid expiration time for timer")
		}
		// fmt.Printf("Running timer callback %s(%v) at time %v\n", nextTimer.name, nextTimer.callback, nextTimer.expireAt)
		// actual callback execution occurs here
		nextTimer.callback()
	}
}

func (sc *SimController) Advance(advanceTo model.VirtualTime) (nextTimer model.VirtualTime) {
	// move forward to the time of the next timer before or at to, and execute it
	// repeat until 'to' is reached and nothing remains to be executed at or before that time

	sc.runCurrentTimers()
	for sc.Now().Before(advanceTo) {
		// move to the next timer, or the specified time, whichever is sooner
		timeStepTo := sc.peekNextTimerExpiry()
		if timeStepTo.TimeExists() && timeStepTo.AtOrBefore(advanceTo) {
			sc.currentTime = timeStepTo
		} else {
			sc.currentTime = advanceTo
		}

		// run timers at this point in time!
		sc.runCurrentTimers()
	}

	return sc.peekNextTimerExpiry()
}

func MakeSimControllerRandomized() *SimController {
	return MakeSimControllerSeeded(time.Now().UnixNano())
}

func MakeSimControllerSeeded(seed int64) *SimController {
	return &SimController{
		currentTime: 0,
		rand:        rand.New(rand.NewSource(seed)),
	}
}
