package component

import (
	"fmt"
	"sim/model"
	"sort"
)

type EventDispatcher struct {
	ctx          model.SimContext
	laterName    string
	subscribers  map[uint64]func()
	sorted       []func()
	nextIndex    uint64
	pendingLater bool
}

func MakeEventDispatcher(ctx model.SimContext, name string) *EventDispatcher {
	return &EventDispatcher{
		ctx:         ctx,
		laterName:   fmt.Sprintf("%s/DispatchLater", name),
		subscribers: map[uint64]func(){},
		sorted:      nil,
		nextIndex:   0,
	}
}

func (ed *EventDispatcher) rebuildSorted() {
	var ints []uint64
	for k := range ed.subscribers {
		ints = append(ints, k)
	}
	sort.Slice(ints, func(i, j int) bool {
		return ints[i] < ints[j]
	})
	ed.sorted = make([]func(), len(ints))
	for i, k := range ints {
		ed.sorted[i] = ed.subscribers[k]
	}
}

func (ed *EventDispatcher) Subscribe(callback func()) (cancel func()) {
	// fmt.Printf("---> subscribed on %p\n", ed)
	ed.subscribers[ed.nextIndex] = callback
	ed.rebuildSorted()
	ed.nextIndex += 1
	return func() {
		delete(ed.subscribers, ed.nextIndex)
		ed.rebuildSorted()
	}
}

func (ed *EventDispatcher) Dispatch() {
	for _, f := range ed.sorted {
		f()
	}
}

func (ed *EventDispatcher) DispatchLater() {
	if !ed.pendingLater {
		// fmt.Printf("---> dispatch armed for later on %p\n", ed)
		ed.pendingLater = true
		ed.ctx.Later(ed.laterName, func() {
			// fmt.Printf("---> dispatch triggered now on %p (%d subscribers)\n", ed, len(ed.subscribers))
			ed.pendingLater = false
			ed.Dispatch()
		})
	} else {
		// fmt.Printf("---> dispatch ALREADY armed for later on %p\n", ed)
	}
}
