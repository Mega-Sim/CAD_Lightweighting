"""Research-side representation probes.

These helpers deliberately use only the Python standard library. They do not own
Source Truth and never decide whether a candidate is valid. They only return cheap
signals that the C++ verifier/evaluator may use to prioritize expensive trials.
"""

from __future__ import annotations

from dataclasses import dataclass, asdict
import cmath
import json
import math
from typing import Iterable, Sequence


Point3 = tuple[float, float, float]


@dataclass(frozen=True)
class ProbeResult:
    name: str
    score: float
    estimated_ratio: float
    details: dict[str, float | int | str]

    def to_json(self) -> str:
        return json.dumps(asdict(self), sort_keys=True)


def _axis_variances(points: Sequence[Point3]) -> tuple[float, float, float]:
    if not points:
        return (0.0, 0.0, 0.0)
    means = tuple(sum(p[i] for p in points) / len(points) for i in range(3))
    return tuple(
        sum((p[i] - means[i]) ** 2 for p in points) / len(points)
        for i in range(3)
    )


def tensor_probe(points: Sequence[Point3]) -> ProbeResult:
    """Cheap low-rank suitability probe, not an actual tensor codec.

    A geometry whose variance is concentrated into fewer axes is a better candidate
    for later CP/Tucker/TT experiments. The returned score is only a search hint.
    """
    variances = sorted(_axis_variances(points), reverse=True)
    total = sum(variances)
    concentration = 0.0 if total == 0.0 else variances[0] / total
    ratio = max(0.05, min(1.0, 1.0 - 0.65 * concentration))
    return ProbeResult(
        name="tensor",
        score=concentration,
        estimated_ratio=ratio,
        details={"variance_x_ranked_0": variances[0], "variance_total": total},
    )


def wavelet_probe(values: Sequence[float]) -> ProbeResult:
    """Estimate multi-resolution compressibility from first-difference sparsity."""
    if len(values) < 2:
        return ProbeResult("wavelet", 0.0, 1.0, {"samples": len(values)})
    diffs = [values[i] - values[i - 1] for i in range(1, len(values))]
    rms_signal = math.sqrt(sum(v * v for v in values) / len(values)) or 1.0
    rms_diff = math.sqrt(sum(v * v for v in diffs) / len(diffs))
    smoothness = max(0.0, min(1.0, 1.0 - rms_diff / rms_signal))
    ratio = max(0.08, min(1.0, 1.0 - 0.7 * smoothness))
    return ProbeResult(
        name="wavelet",
        score=smoothness,
        estimated_ratio=ratio,
        details={"samples": len(values), "rms_diff": rms_diff, "rms_signal": rms_signal},
    )


def spectral_probe(values: Sequence[float], max_bins: int = 32) -> ProbeResult:
    """Small O(n^2) DFT probe for periodicity; intended for sampled candidate signals."""
    n = min(len(values), max_bins)
    if n < 2:
        return ProbeResult("spectral", 0.0, 1.0, {"samples": n})
    sample = list(values[:n])
    spectrum: list[float] = []
    for k in range(n):
        coefficient = sum(
            sample[t] * cmath.exp(-2j * math.pi * k * t / n)
            for t in range(n)
        )
        spectrum.append(abs(coefficient))
    total = sum(spectrum) or 1.0
    strongest = max(spectrum[1:], default=0.0)
    concentration = strongest / total
    ratio = max(0.08, min(1.0, 1.0 - 0.75 * concentration))
    return ProbeResult(
        name="spectral",
        score=concentration,
        estimated_ratio=ratio,
        details={"samples": n, "strongest_non_dc": strongest, "spectrum_sum": total},
    )


def flatten_points(points: Iterable[Point3]) -> list[float]:
    return [coordinate for point in points for coordinate in point]
