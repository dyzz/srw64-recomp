"""Build-directory-only WARP selection and failure diagnostics for pinned Plume.

This never edits the dependency checkout or changes successful rendering. The
compositor, shaders, commands and driver are real; WARP is software evidence.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path


def prepare(source: Path, output: Path) -> None:
    raw = source.read_bytes().replace(b"\r\n", b"\n")
    blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
    if blob != "2cdcf18f3b2e1729c2ff63684329c9c26770748c":
        raise ValueError("WARP fixture requires the pinned Plume D3D12 source")
    if source.resolve() == output.resolve():
        raise ValueError("Refusing to modify the dependency checkout")
    text = raw.decode("utf-8")
    replacements = (
        ("if (adapterDesc.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE)) {",
         "if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_REMOTE) {"),
        ("if ((adapterDesc.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE)) == 0) {",
         "if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_REMOTE) == 0) {"),
        ("device->d3d->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&d3d));", r'''const HRESULT pixel_test_result = device->d3d->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&d3d));
        if (FAILED(pixel_test_result)) {
            fprintf(stderr, "CreateGraphicsPipelineState failed 0x%lX (VS=%zu PS=%zu samples=%u RT=%u)\n", pixel_test_result,
                psoDesc.VS.BytecodeLength, psoDesc.PS.BytecodeLength, psoDesc.SampleDesc.Count, psoDesc.RTVFormats[0]);
            ID3D12InfoQueue* info = nullptr;
            if (SUCCEEDED(device->d3d->QueryInterface(IID_PPV_ARGS(&info)))) {
                for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
                    SIZE_T size = 0; info->GetMessage(i, nullptr, &size);
                    std::vector<unsigned char> storage(size);
                    auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                    if (SUCCEEDED(info->GetMessage(i, message, &size))) fprintf(stderr, "%s\n", message->pDescription);
                }
                info->Release();
            }
            throw std::runtime_error("D3D12 graphics pipeline creation failed");
        }'''),
    )
    for before, after in replacements:
        if text.count(before) != 1:
            raise ValueError("Pinned source differs")
        text = text.replace(before, after)
    text = '#include <d3d12sdklayers.h>\n#include <stdexcept>\n' + text
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8", newline="\n")
    print("WARP fixture: adapter selection and failure diagnostics in generated copy; production source unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.source, args.output)
