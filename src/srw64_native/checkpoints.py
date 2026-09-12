"""Storage prototype for atomic save collections, not a game autosave service.

The game adapter must establish node eligibility and full state coverage before
using this store. The current SRAM + RNG-table schema has no production restore
consumer; it omits reseed clocks and reader state and must not be advertised as
complete progress recovery. Presentation is outside compatibility.
"""
from __future__ import annotations

from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import uuid


class CheckpointError(ValueError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sync_dir(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def write_new(path: Path, data: bytes) -> None:
    with path.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def atomic_json(path: Path, data: dict) -> None:
    fd, name = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write((json.dumps(data, ensure_ascii=False, sort_keys=True) + '\n').encode())
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        sync_dir(path.parent)
    finally:
        temporary.unlink(missing_ok=True)


@contextmanager
def locked(root: Path):
    root.mkdir(parents=True, exist_ok=True)
    with (root / 'store.lock').open('a') as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        yield


def validate_identity(identity: dict) -> None:
    if not isinstance(identity, dict) or set(identity) != {'baseline', 'state_schema', 'rules'} or identity['baseline'] != 'srw64-jp-rev0':
        raise CheckpointError('Unknown checkpoint identity')
    if identity['state_schema'] != 'srw64.original-sram-with-rng.v1' or identity['rules'] != {}:
        raise CheckpointError('Unsupported state schema or rules')


def commit(root: Path, members: dict[str, bytes], identity: dict, metadata: dict, *, retain: int = 5) -> str:
    validate_identity(identity)
    if type(retain) is not int or not 2 <= retain <= 20:
        raise CheckpointError('Retain between 2 and 20 complete generations')
    if set(members) != {'sram.bin', 'extensions.json'} or len(members['sram.bin']) != 0x8000:
        raise CheckpointError('Checkpoint requires SRAM and extension state')
    validate_extensions(json.loads(members['extensions.json']))
    if metadata.get('node_verified') is not True:
        raise CheckpointError('Cannot publish an unverified safety node')
    with locked(root):
        previous = read_index(root)
        generation = uuid.uuid4().hex
        directory = root / generation
        directory.mkdir()
        for name, data in members.items():
            write_new(directory / name, data)
        manifest = {'schema': 'srw64.checkpoint-generation.v1', 'identity': identity,
                    'metadata': metadata, 'members': {name: {'size': len(data), 'sha256': digest(data)} for name, data in members.items()}}
        manifest_bytes = (json.dumps(manifest, ensure_ascii=False, sort_keys=True) + '\n').encode()
        write_new(directory / 'manifest.json', manifest_bytes)
        sync_dir(directory)
        read_generation(root, {'id': generation, 'manifest_sha256': digest(manifest_bytes)}, identity)
        # A single index commits the complete set. Orphans from interruptions
        # are never considered for recovery and never promoted by scanning.
        entries = [{'id': generation, 'manifest_sha256': digest(manifest_bytes)}, *previous['generations']][:retain]
        atomic_json(root / 'index.json', {'schema': 'srw64.checkpoint-index.v1', 'generations': entries})
        # Old generations remain physically intact for now; rotation bounds the
        # recovery list without destructive automatic garbage collection.
        return generation


def validate_extensions(data: dict) -> None:
    if not isinstance(data, dict) or set(data) != {'schema', 'rng_index', 'rng_table', 'read_state'} or data['schema'] != 'srw64.checkpoint-extensions.v1':
        raise CheckpointError('Unsupported extension members')
    if type(data['rng_index']) is not int or not 0 <= data['rng_index'] <= 520:
        raise CheckpointError('Invalid RNG index')
    if not isinstance(data['rng_table'], str) or not re.fullmatch(r'[0-9a-f]{4168}', data['rng_table']):
        raise CheckpointError('Invalid 521-word RNG table')
    if data['read_state'] != {}:
        raise CheckpointError('Reader state version not supported yet')


def read_index(root: Path) -> dict:
    path = root / 'index.json'
    if not path.exists():
        return {'schema': 'srw64.checkpoint-index.v1', 'generations': []}
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError) as error:
        raise CheckpointError('Unreadable checkpoint index; do not scan uncommitted directories') from error
    if not isinstance(data, dict) or data.get('schema') != 'srw64.checkpoint-index.v1' or not isinstance(data.get('generations'), list):
        raise CheckpointError('Invalid checkpoint index; do not scan uncommitted directories')
    seen = set()
    for entry in data['generations']:
        if (not isinstance(entry, dict) or set(entry) != {'id', 'manifest_sha256'}
                or not isinstance(entry['id'], str) or not re.fullmatch(r'[0-9a-f]{32}', entry['id'])
                or not isinstance(entry['manifest_sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', entry['manifest_sha256'])
                or entry['id'] in seen):
            raise CheckpointError('Invalid checkpoint index entry; do not scan uncommitted directories')
        seen.add(entry['id'])
    return data


def read_generation(root: Path, entry: dict, identity: dict) -> tuple[dict, dict[str, bytes]]:
    generation = entry.get('id', '')
    if not isinstance(generation, str) or not re.fullmatch(r'[0-9a-f]{32}', generation):
        raise CheckpointError('Invalid generation identifier')
    directory = root / generation
    if directory.is_symlink():
        raise CheckpointError('Symlink checkpoint generation')
    manifest_path = directory / 'manifest.json'
    if manifest_path.is_symlink():
        raise CheckpointError('Symlink checkpoint manifest')
    raw = manifest_path.read_bytes()
    if digest(raw) != entry.get('manifest_sha256'):
        raise CheckpointError('Manifest integrity failure')
    manifest = json.loads(raw)
    if not isinstance(manifest, dict) or manifest.get('schema') != 'srw64.checkpoint-generation.v1' or manifest.get('identity') != identity:
        raise CheckpointError('Checkpoint state or rules are incompatible')
    if set(manifest.get('members', {})) != {'sram.bin', 'extensions.json'}:
        raise CheckpointError('Incomplete checkpoint collection')
    members = {}
    for name, expected in manifest['members'].items():
        path = directory / name
        if path.is_symlink():
            raise CheckpointError('Symlink checkpoint member')
        data = path.read_bytes()
        if len(data) != expected['size'] or digest(data) != expected['sha256']:
            raise CheckpointError(f'Checkpoint member integrity failure: {name}')
        members[name] = data
    if len(members['sram.bin']) != 0x8000 or manifest['metadata'].get('node_verified') is not True:
        raise CheckpointError('Invalid checkpoint state or safety node')
    validate_extensions(json.loads(members['extensions.json']))
    return manifest, members


def recover(root: Path, identity: dict) -> tuple[dict, dict[str, bytes], list[dict]]:
    validate_identity(identity)
    rejected = []
    with locked(root):
        for entry in read_index(root)['generations']:
            try:
                manifest, members = read_generation(root, entry, identity)
                return manifest, members, rejected
            except (CheckpointError, OSError, ValueError, KeyError, TypeError, AttributeError) as error:
                rejected.append({'id': entry.get('id'), 'reason': str(error)})
    raise CheckpointError(f'No compatible intact generation: {rejected}')
