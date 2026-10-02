"""Append-only experiment record helpers for long-running research runs."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import json
from pathlib import Path
from typing import Iterable


@dataclass
class ExperimentRecord:
    drawing_fingerprint: str
    strategy: str
    candidate_ids: list[str]
    estimated_bytes: float | None
    exact_dwg_bytes: int | None
    valid: bool
    failure_reason: str | None
    runtime_seconds: float
    seed: int | None
    trace_summary: dict[str, int | float | str]
    created_at: str = ""

    def normalized(self) -> "ExperimentRecord":
        if not self.created_at:
            self.created_at = datetime.now(timezone.utc).isoformat()
        return self


def append_record(path: str | Path, record: ExperimentRecord) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    payload = asdict(record.normalized())
    with target.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(payload, sort_keys=True, ensure_ascii=False) + "\n")


def read_records(path: str | Path) -> list[ExperimentRecord]:
    target = Path(path)
    if not target.exists():
        return []
    result: list[ExperimentRecord] = []
    with target.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            result.append(ExperimentRecord(**json.loads(line)))
    return result


def best_valid_record(records: Iterable[ExperimentRecord]) -> ExperimentRecord | None:
    valid = [record for record in records if record.valid and record.exact_dwg_bytes is not None]
    if not valid:
        return None
    return min(valid, key=lambda record: (record.exact_dwg_bytes, record.estimated_bytes or float("inf"), record.strategy))
