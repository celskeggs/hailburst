import math
import random





def probabilities(rate_per_tick, n):
	total = 1.0
	px = []
	for i in range(n):
		p = rate_per_tick * total
		total -= p
		px.append(p)
	return px, total


def percentile_50th_boundary(rate_per_tick):
    return math.log(1 / 2) / math.log(1 - rate_per_tick)


def survival_rate(rate_per_tick, n):
    return (1 - rate_per_tick) ** n

print(*probabilities(0.1, 6))
print(*probabilities(0.1, 7))
print(percentile_50th_boundary(0.001))
print(survival_rate(0.001, 692))

def sample_geo(r):
    return math.ceil(math.log(random.random()) / math.log(1 - r))

def sample_geo_n(r, n):
    totals = []
    for i in range(n):
        idx = sample_geo(r) - 1
        while len(totals) <= idx:
            totals.append(0)
        totals[idx] += 1
    return totals

def sample_old_n(r, n):
    totals = []
    for i in range(n):
        c = 0
        while random.random() >= r:
            c += 1
        while len(totals) <= c:
            totals.append(0)
        totals[c] += 1
    return totals

print(sample_geo_n(0.1, 30000))
print(sample_old_n(0.1, 30000))
