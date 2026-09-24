#!/usr/bin/env bash
# Full dialogue translation for one locale: draft, resume pass, AI review, resume pass, report.
#   tools/translation/run_full.sh <locale> <tag> [draft|review|all]
# Needs SRW64_DASHSCOPE_ENV (or a repository .env) and the local text export.
# Every stage resumes: finished batches are skipped, so rerunning after a failure is safe.
set -euo pipefail
cd "$(dirname "$0")/../.."
locale="$1" tag="$2" stage="${3:-all}"
export PYTHONPATH=src
run=(.venv/bin/python -B tools/translation/run_mt.py)
draft_model="${DRAFT_MODEL:-deepseek-v4.1-flash}"
review_model="${REVIEW_MODEL:-deepseek-v4-pro-0813}"
jobs="${JOBS:-6}"

if [[ "$stage" == draft || "$stage" == all ]]; then
  for pass in 1 2; do
    echo "== draft pass $pass ($draft_model)"
    "${run[@]}" run --tag "$tag" --locale "$locale" --kind all --model "$draft_model" --jobs "$jobs"
  done
  "${run[@]}" report --tag "$tag" | head -20
fi
if [[ "$stage" == review || "$stage" == all ]]; then
  for pass in 1 2; do
    echo "== review pass $pass ($review_model)"
    "${run[@]}" review --tag "$tag-review" --draft "$tag" --locale "$locale" --kind all --model "$review_model" --jobs "$jobs"
  done
  "${run[@]}" report --tag "$tag-review" | head -20
fi
