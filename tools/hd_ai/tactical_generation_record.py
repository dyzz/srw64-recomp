"""Save built-in imagegen outputs and provenance, retaining superseded versions."""
import sys,json,hashlib,shutil,struct,zlib,datetime
from pathlib import Path
root=Path(__file__).resolve().parents[2]/'assets/hd-ai/tactical-kit'
meta=json.load(sys.stdin) if sys.argv[1]=='-' else json.loads(Path(sys.argv[1]).read_text())
rp=root/'outputs/generation-records.json'
doc=json.loads(rp.read_text())
for item in meta:
    job=item['job']; src=Path(item['source']); dst=root/job['output']
    raw=src.read_bytes(); digest=hashlib.sha256(raw).hexdigest()
    assert raw[:8]==b'\x89PNG\r\n\x1a\n'
    pos=8
    while pos<len(raw):
        n=struct.unpack_from('>I',raw,pos)[0]; kind=raw[pos+4:pos+8]; data=raw[pos+8:pos+8+n]
        assert zlib.crc32(kind+data)&0xffffffff==struct.unpack_from('>I',raw,pos+8+n)[0]
        pos+=n+12
        if kind==b'IEND': break
    assert pos==len(raw)
    if dst.exists() and hashlib.sha256(dst.read_bytes()).hexdigest()!=digest:
        old=hashlib.sha256(dst.read_bytes()).hexdigest()
        archive=root/'outputs/archive'/f'{dst.stem}-{old[:12]}.png'
        archive.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(dst,archive)
        for rec in doc['records']:
            if rec.get('output')==job['output']:
                rec['output']=str(archive.relative_to(root))
                rec['status']='superseded preview; preserved in archive'
    shutil.copy2(src,dst)
    if not any(r.get('sha256')==digest and r.get('output')==job['output'] for r in doc['records']):
        doc['records'].append({
            'id':job['id'],'family':job['family'],
            'created_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'tool':'image_gen.imagegen (built-in)','model':'not exposed by tool',
            'input':job['input'],'input_sha256':hashlib.sha256((root/job['input']).read_bytes()).hexdigest(),
            'prompt_file':job['prompt'],'prompt':job['prompt_text'],'style_reference':job.get('style_reference'),
            'output':job['output'],'generated_source':str(src),
            'output_size':list(struct.unpack_from('>II',raw,16)),'sha256':digest,
            'status':job.get('generation_status','generated; visual inspection recorded; registration/runtime pending'),
            'visual_review':item.get('review',{}),
            **{k:job[k] for k in ('revision','edit_target_source','reference_images','style_reference_sha256') if k in job}
        })
    print(job['id'],list(struct.unpack_from('>II',raw,16)),digest[:12])
rp.write_text(json.dumps(doc,ensure_ascii=False,indent=2)+'\n')
