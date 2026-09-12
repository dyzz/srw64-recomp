"""Execute the frozen 42-output plan with one sequential worker per model."""
from __future__ import annotations
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys
import time

from tools.hd_ai.run_benchmark import ROOT, DEFAULT_OUT, PRICES


def main() -> None:
    env_file=Path(sys.argv[1])
    samples=json.loads((DEFAULT_OUT/'samples.json').read_text())['samples']
    assert len(samples)==6
    planned=sum(price*(1 if '2026-06-22' in model else 2)*len(samples) for model,price in PRICES.items())
    assert round(planned,2)==17.64
    def worker(model: str) -> None:
        previous=0.0
        for sample in samples:
            for candidate in range(1,2 if '2026-06-22' in model else 3):
                marker=DEFAULT_OUT/'runs'/f"{sample['id']}--{model}--{candidate}"/'request.json'
                if marker.exists(): continue
                gap=31 if '2026-06-22' in model else (13 if model=='qwen-image-3.0-pro' else 4)
                time.sleep(max(0,gap-(time.monotonic()-previous)))
                previous=time.monotonic()
                result=subprocess.run([sys.executable,str(ROOT/'tools/hd_ai/run_benchmark.py'),
                    '--sample',sample['id'],'--model',model,'--candidate',str(candidate),
                    '--env-file',str(env_file)],capture_output=True,text=True)
                print(result.stdout.strip(),flush=True)
                if result.returncode: raise RuntimeError('worker failed; no automatic inference retry')
                status=json.loads(marker.read_text())['status']
                if status!='completed':
                    print(json.dumps({'stopped_model':model,'reason':status}),flush=True)
                    return
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(worker,PRICES))


if __name__=='__main__': main()
