"""Aliyun (DashScope) image-edit requests for the HD generators.

`run_one` sends one frozen sample once and never retries a POST: the request
record is written before sending, so an interrupted run cannot pay twice. Every
output folder carries its own spending reservation cap.

    .venv/bin/python -m tools.hd_ai.aliyun --output DIR --sample ID --model MODEL --env-file .env
"""
from __future__ import annotations

import argparse
import base64
import fcntl
import hashlib
import io
import json
from pathlib import Path
import time
import urllib.error
import urllib.parse
import urllib.request

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
PRICES = {"qwen-image-3.0-pro": .52, "qwen-image-3.0": .20,
          "wan2.7-image-pro": .50, "qwen-image-2.0-pro-2026-06-22": .50}


def load_env(path: Path) -> dict:
    """DASHSCOPE_API_KEY and DASHSCOPE_BASE_URL from a dotenv file; other keys are ignored."""
    config = {}
    for line in path.read_text().splitlines():
        line = line.strip().removeprefix('export ')
        if line and not line.startswith('#') and '=' in line:
            key, value = line.split('=', 1)
            if key.strip() in ('DASHSCOPE_API_KEY', 'DASHSCOPE_BASE_URL'):
                config[key.strip()] = value.strip().strip('"\'')
    return config


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run_one(out: Path, sample: dict, model: str, candidate: int, config: dict) -> dict:
    folder = out / "runs" / f"{sample['id']}--{model}--{candidate}"
    folder.mkdir(parents=True, exist_ok=True)
    report_path = folder / "request.json"
    if report_path.exists():
        return json.loads(report_path.read_text())
    price = PRICES[model]
    data = (out/sample["input"]).read_bytes()
    parameters = {"n":1,"watermark":False,"seed":640901+candidate,
                  "size":sample["output_size"],"prompt_extend":False}
    # Optional reference images (for example a style sample) follow the input as 图2, 图3.
    references = [(out/path).read_bytes() for path in sample.get("references", [])]
    body = {"model":model,"input":{"messages":[{"role":"user","content":[
        {"image":"data:image/png;base64,"+base64.b64encode(data).decode()},
        *({"image":"data:image/png;base64,"+base64.b64encode(r).decode()} for r in references),
        {"text":sample["prompt"]}]}]},"parameters":parameters}
    report = {"schema":"srw64.ali-image-request.v1","sample_id":sample["id"],
              "model":model,"candidate":candidate,"parameters":parameters,
              "input_sha256":digest(data),"prompt":sample["prompt"],
              **({"reference_sha256":[digest(r) for r in references]} if references else {}),
              "reserved_cny":price,"price_is_estimate":True,"status":"request_started",
              "started_at_utc":time.strftime("%Y-%m-%dT%H:%M:%SZ",time.gmtime())}
    def save() -> None:
        temporary=report_path.with_suffix('.tmp')
        temporary.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
        temporary.replace(report_path)
    # Serialize only the spending reservation across processes, not inference.
    with (out/'.budget.lock').open('a') as guard:
        fcntl.flock(guard,fcntl.LOCK_EX)
        if report_path.exists(): return json.loads(report_path.read_text())
        reserved=sum(json.loads(f.read_text())["reserved_cny"] for f in (out/'runs').glob('*/request.json'))
        if reserved+price>18.12+1e-8:
            raise RuntimeError('benchmark spending reservation would exceed CNY 18.12')
        save()  # Also prevents duplicate POST after a process interruption.
    start=time.monotonic()
    url=urllib.parse.urlsplit(config["DASHSCOPE_BASE_URL"])
    if url.scheme!="https" or not url.hostname.endswith(".aliyuncs.com") or url.username or url.query or url.fragment:
        raise RuntimeError("unsupported configured API origin")
    endpoint=urllib.parse.urlunsplit((url.scheme,url.netloc,"/api/v1/services/aigc/multimodal-generation/generation","",""))
    request=urllib.request.Request(endpoint,data=json.dumps(body).encode(),headers={
        "Authorization":"Bearer "+config["DASHSCOPE_API_KEY"],"Content-Type":"application/json"})
    try:
        with urllib.request.urlopen(request,timeout=600) as response:
            result=json.load(response);report["http_status"]=response.status
        report["request_id"]=result.get("request_id")
        report["usage"]=result.get("usage")
        contents=result.get("output",{}).get("choices",[])
        images=[c["image"] for choice in contents for c in choice.get("message",{}).get("content",[]) if "image" in c]
        if len(images)!=1:
            report.update(status="unexpected_response",error_code=result.get("code"),output_keys=list(result.get("output",{})))
            save();return report
        report["status"]="inference_succeeded_download_pending";save()
        # Signed result URLs are not printed or written to public manifests.
        with urllib.request.urlopen(images[0],timeout=90) as response:
            output=response.read()
        image=Image.open(io.BytesIO(output));image.load()
        extension={"PNG":"png","JPEG":"jpg","WEBP":"webp"}.get(image.format)
        if not extension: raise ValueError("unexpected generated image format")
        path=folder/f"output.{extension}";path.write_bytes(output)
        report.update(status="completed",output=str(path.relative_to(out)),output_sha256=digest(output),
                      dimensions=list(image.size),mode=image.mode,image_format=image.format)
    except urllib.error.HTTPError as error:
        report.update(status="http_error",http_status=error.code)
        try:
            error_body=json.loads(error.read())
            report["error_code"]=error_body.get("code")
            # The API message may echo inputs or the host: keep only code and ID.
            report["request_id"]=error_body.get("request_id")
        except (ValueError,UnicodeError): pass
    except Exception as error:
        report.update(status="uncertain_or_download_error",error_type=type(error).__name__)
    report["elapsed_seconds"]=round(time.monotonic()-start,2);save()
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description="Send one frozen sample from OUTPUT/samples.json once.")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sample", required=True)
    parser.add_argument("--model", choices=PRICES, required=True)
    parser.add_argument("--candidate", type=int, choices=(1, 2), default=1)
    parser.add_argument("--env-file", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads((args.output / "samples.json").read_text())
    assert manifest["schema"] == "srw64.hd-ai-samples.v1"
    sample = next(s for s in manifest["samples"] if s["id"] == args.sample)
    report = run_one(args.output, sample, args.model, args.candidate, load_env(args.env_file))
    print(json.dumps({k: report.get(k) for k in ("sample_id", "model", "candidate", "status", "http_status",
                                                 "error_code", "dimensions", "elapsed_seconds")}), flush=True)


if __name__ == "__main__":
    main()
