package transport

import (
	"github.com/celskeggs/hailburst/sim/component"
	"github.com/celskeggs/hailburst/sim/model"
	"github.com/celskeggs/hailburst/sim/telecomm"
	"testing"
	"time"
)

func TestCodersSimple(t *testing.T) {
	sim := component.MakeSimControllerSeeded(88888888, model.TimeZero)

	var received []*CommPacket

	txmit, rxcv := telecomm.MakePathway(sim, time.Microsecond*800)

	AttachReceiver(sim, rxcv, func(packet *CommPacket) {
		received = append(received, packet)
	}, func(errcount int) {
		t.Errorf("should never hit an error callback without corruption, yet got %d errors", errcount)
	})

	planned := make([]*CommPacket, 30)
	for i := 0; i < len(planned); i++ {
		planned[i] = RandCommPacket(sim.Rand())
	}
	pending := planned

	sender := MakeSender(sim, txmit)
	txHelper := func() {
		if len(pending) > 0 {
			pktCopy := *pending[0]
			if !sender.Send(&pktCopy) {
				t.Error("sender should never fail to transmit the first time!")
			}
			pending = pending[1:]
			// try transmitting again, just to make sure we won't be allowed to
			if len(pending) > 0 {
				pktCopy2 := *pending[0]
				if sender.Send(&pktCopy2) {
					t.Error("sender should always fail to transmit the second time!")
				}
			}
		}
	}
	sender.Subscribe(txHelper)
	sim.Later("sim.telecomm.ground.TestCodersSimple", txHelper)

	// fast forward ahead
	sim.Advance(sim.Now().Add(1000 * time.Second))

	if len(pending) != 0 {
		t.Errorf("expected all packets to be sent")
	}
	if len(received) != len(planned) {
		t.Errorf("wrong number of packets received: %d instead of %d", len(received), len(planned))
	}
	for i := 0; i < len(received) && i < len(planned); i++ {
		if !CommPacketsEqual(received[i], planned[i]) {
			t.Errorf("packets at index %d mismatched:\nreceived: %v\nexpected: %v", i, received[i], planned[i])
		}
	}
}
