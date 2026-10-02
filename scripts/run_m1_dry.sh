#!/usr/bin/env bash
set -euo pipefail
./build/cadopt --input tests/fixtures/minimal.dxf --dry-run --report build/m1_report.json
