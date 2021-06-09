package magnetometer

import (
	"math"
	"sim/model"
	"time"
)

type randomMagneticEnvironment struct {
	ctx model.SimContext
	x, y, z float64
	lastTimestamp model.VirtualTime
}

func convF64ToU16(v float64) int16 {
	if v >= 1 {
		return math.MaxInt16
	} else if v > 0 {
		return int16(math.MaxInt16 * v)
	} else if v <= -1 {
		return math.MinInt16
	} else if v < 0 {
		return int16(math.MinInt16 * -v)
	} else {
		return 0
	}
}

const clampThreshold = 1.1

func clamp(x float64) float64 {
	if x > clampThreshold {
		return clampThreshold
	} else if x < -clampThreshold {
		return -clampThreshold
	} else {
		return x
	}
}

func (r *randomMagneticEnvironment) update(seconds float64) {
	rand := r.ctx.Rand()
	mul := math.Exp2((rand.Float64() - 0.5) * seconds)
	r.x *= mul
	r.y *= mul
	r.z *= mul
	r.x += (rand.Float64() - 0.5) * seconds
	r.y += (rand.Float64() - 0.5) * seconds
	r.z += (rand.Float64() - 0.5) * seconds
	r.x = clamp(r.x)
	r.y = clamp(r.y)
	r.z = clamp(r.z)
}

func (r *randomMagneticEnvironment) MeasureNow() (x, y, z int16) {
	totalTimeStep := r.ctx.Now().Since(r.lastTimestamp)
	for totalTimeStep > time.Millisecond {
		r.update(time.Millisecond.Seconds())
		totalTimeStep -= time.Millisecond
	}
	r.update(totalTimeStep.Seconds())
	return convF64ToU16(r.x), convF64ToU16(r.y), convF64ToU16(r.z)
}

func MakeRandomMagneticEnvironment(ctx model.SimContext) MagneticEnvironment {
	return &randomMagneticEnvironment{
		ctx:           ctx,
		x:             0,
		y:             0,
		z:             0,
		lastTimestamp: ctx.Now(),
	}
}