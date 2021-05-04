package component

import "sim/model"

type EventDispatcher struct {
	ctx          model.SimContext
	subscribers  map[uint64]func()
	nextIndex    uint64
	pendingLater bool
}

func MakeEventDispatcher(ctx model.SimContext) *EventDispatcher {
	return &EventDispatcher{
		ctx:         ctx,
		subscribers: map[uint64]func(){},
		nextIndex:   0,
	}
}

func (ed *EventDispatcher) Subscribe(callback func()) (cancel func()) {
	ed.subscribers[ed.nextIndex] = callback
	ed.nextIndex += 1
	return func() {
		delete(ed.subscribers, ed.nextIndex)
	}
}

func (ed *EventDispatcher) Dispatch() {
	for _, f := range ed.subscribers {
		f()
	}
}

func (ed *EventDispatcher) DispatchLater() {
	if !ed.pendingLater {
		ed.pendingLater = true
		ed.ctx.Later(func() {
			ed.pendingLater = false
			ed.Dispatch()
		})
	}
}
