"""Synthetic read traces. Only the Python standard library is required."""

import random
import math
from itertools import accumulate


def scenarios(suite, capacity):
    """Yield (family, parameters); each family gets equal weight in the report."""
    for item in suite["scenarios"]:
        parameters = dict(item["parameters"])
        for name, specification in item.get("scaled", {}).items():
            multiplier, offset = specification
            parameters[name] = int(multiplier * capacity) + offset
        yield item["family"], parameters


def generate(family, parameters, count, seed):
    rng = random.Random(seed)
    p = parameters
    size = p.get("w", 1)
    if family == "cyclic":
        return [i % size for i in range(count)]
    if family == "scan":
        result = []
        first = 0
        while len(result) < count:
            result.extend(range(first, first + size))
            first += p["shift"]
        return result[:count]
    if family == "uniform":
        return [rng.randrange(size) for _ in range(count)]
    if family == "normal":
        result = []
        mean = (size - 1) / 2
        while len(result) < count:
            key = math.floor(rng.gauss(mean, p["sigma"]))
            if 0 <= key < size:
                result.append(key)
        return result
    if family == "zipf":
        weights = [1 / rank ** p["alpha"] for rank in range(1, size + 1)]
        return rng.choices(range(size), weights=weights, k=count)
    if family == "hotspot":
        hot = p.get("hot", max(1, size // 10))
        return [rng.randrange(hot) if rng.random() < p["p"]
                else rng.randrange(hot, size) for _ in range(count)]
    if family == "hot_scan":
        result = []
        next_scan_key = p["hot"]
        while len(result) < count:
            result.extend(rng.randrange(p["hot"]) for _ in range(p["hot_requests"]))
            result.extend(range(next_scan_key, next_scan_key + p["scan"]))
            next_scan_key += p["scan"]
        return result[:count]
    if family == "streams":
        component_seeds = [rng.getrandbits(64) for _ in range(3)]
        traces = [generate("zipf", {"w": size, "alpha": 1.0}, count, component_seeds[0]),
                  generate("cyclic", {"w": size}, count, component_seeds[1]),
                  generate("hotspot", {"w": size, "p": 0.9}, count, component_seeds[2])]
        positions = [0, 0, 0]
        result = []
        cumulative = list(accumulate(p["weights"]))
        for _ in range(count):
            stream = rng.choices(range(3), cum_weights=cumulative, k=1)[0]
            result.append(stream * size + traces[stream][positions[stream]])
            positions[stream] += 1
        return result
    raise ValueError(f"Unknown workload: {family}")
