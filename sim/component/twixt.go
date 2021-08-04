package component

import (
	"github.com/celskeggs/hailburst/sim/model"
)

type marker struct{}

type TwixtIO struct {
	ctx    model.SimContext
	waitCh chan marker
	doneCh chan marker
	runOk  bool
	halted bool
}

func (ti *TwixtIO) enter() {
	if !ti.halted {
		ti.runOk = true
		ti.waitCh <- marker{}
		<-ti.doneCh
		if !ti.runOk {
			panic("should have been running")
		}
		ti.runOk = false
	}
}

func (ti *TwixtIO) Yield() {
	if !ti.runOk {
		panic("should be running")
	}
	ti.doneCh <- marker{}
	<-ti.waitCh
	if !ti.runOk {
		panic("should be running")
	}
}

func subscribeAll(events []model.EventSource, cb func()) (cancel func()) {
	var cancels []func()
	for _, e := range events {
		cancels = append(cancels, e.Subscribe(cb))
	}
	return func() {
		for _, c := range cancels {
			c()
		}
	}
}

func (ti *TwixtIO) YieldWait(events ...model.EventSource) {
	cancel := subscribeAll(events, ti.enter)
	defer cancel()

	ti.Yield()
}

func (ti *TwixtIO) YieldUntil(time model.VirtualTime) {
	cancel := ti.ctx.SetTimer(time, "sim.components.Twixt/Until", ti.enter)
	defer cancel()

	ti.Yield()
}

type TwixtFunc func(*TwixtIO)

// BuildTwixt runs a function in an imperative side thread, returning to the simulation on each Yield().
func BuildTwixt(ctx model.SimContext, events []model.EventSource, main TwixtFunc) {
	ti := &TwixtIO{
		ctx:    ctx,
		waitCh: make(chan marker),
		doneCh: make(chan marker),
	}
	go func() {
		<-ti.waitCh
		defer func() {
			ti.halted = true
			ti.doneCh <- marker{}
		}()
		main(ti)
	}()
	ctx.Later("sim.components.Twixt/Enter", ti.enter)
	_ = subscribeAll(events, ti.enter)
}
