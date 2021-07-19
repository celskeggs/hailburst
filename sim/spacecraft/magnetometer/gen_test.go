package magnetometer

import (
	"github.com/celskeggs/hailburst/sim/component"
	"testing"
	"time"
)

func TestGen(t *testing.T) {
	// TODO: can I actually test anything meaningful here?
	sim := component.MakeSimControllerSeeded(1)
	me := MakeRandomMagneticEnvironment(sim)
	for i := 0; i < 50; i++ {
		x, y, z := me.MeasureNow()
		t.Logf("[%v] X=%v, Y=%v, Z=%v", sim.Now(), x, y, z)
		sim.Advance(sim.Now().Add(time.Second))
	}
}
