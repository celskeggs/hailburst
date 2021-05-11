package component

type NullEventSource struct{}

func (ns NullEventSource) Subscribe(callback func()) (cancel func()) {
	// no events, so no need to track who has subscribed
	return func() {}
}
