#!/usr/bin/env python3
"""
Build the Android directional animation library.

Consumes the GF server AnimCollection export (GF_EXPORT_ANIMS=<dir>) and
appends every clip to player_base.glb as "cNNNNN" animations, converting
from GF Z-up local quaternions to glTF Y-up.

clipId == index in the server AnimCollection == order in anim_registry.txt.

Also renames the legacy 17 clips (anim_00..anim_16) to their category names
(idle, walk, ...) so the DZFootScene fallback path keeps working.

Usage:
    python build_anim_library.py <anim_export_dir> <player_base.glb>

    <anim_export_dir> contains anim_registry.txt + c00000.anim ... (produced
    by: GF_EXPORT_ANIMS=<dir> GF_EXPORT_ANIMS_EXIT=1 gf_server --room-id=x --mode=ai_vs_ai)
"""
import json
import math
import os
import struct
import sys

GF_FPS = 100.0  # GF engine physics tick — anim frames advance at 100 fps

# GF body parts -> glTF node names (player_base.glb uses the same names)
BONE_NAMES = [
    'player', 'body', 'middle', 'neck',
    'left_shoulder', 'left_elbow', 'right_shoulder', 'right_elbow',
    'left_thigh', 'left_knee', 'left_ankle',
    'right_thigh', 'right_knee', 'right_ankle',
]

# Legacy category names, must match DZFootScene.gd ANIM_NAMES
LEGACY_NAMES = [
    'idle', 'walk', 'run', 'sprint',
    'shoot_right', 'shoot_left', 'pass_short', 'pass_long',
    'header', 'tackle', 'dribble', 'fall',
    'celebrate', 'gk_idle', 'gk_dive_left', 'gk_dive_right', 'gk_catch',
]

# Z-up -> Y-up basis rotation (-90 degrees around X), (x, y, z, w)
_H = math.sqrt(2.0) / 2.0
Q_BASIS = (-_H, 0.0, 0.0, _H)
Q_BASIS_INV = (_H, 0.0, 0.0, _H)


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def quat_zup_to_yup(q):
    """Conjugate by the Z-up -> Y-up basis rotation: q' = B * q * B^-1."""
    return quat_mul(quat_mul(Q_BASIS, q), Q_BASIS_INV)


def normalize_quat(q):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w)
    if n < 1e-12:
        return (0.0, 0.0, 0.0, 1.0)
    return (x / n, y / n, z / n, w / n)


def parse_anim_file(path):
    """Parse a GF .anim file. Returns {body_part: [(frame, quat or pos)]}."""
    tracks = {}
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('<') or line.startswith('extension'):
                break  # XML metadata / extension section starts
            parts = line.split(',')
            body_part = parts[0].strip()
            kfs = []
            i = 1
            is_root = (body_part == 'player')
            vals_per_kf = 3 if is_root else 4
            while i + vals_per_kf <= len(parts):
                try:
                    frame = int(parts[i].strip())
                    vals = tuple(float(parts[i + 1 + k].strip()) for k in range(vals_per_kf))
                except ValueError:
                    break
                kfs.append((frame, vals))
                i += 1 + vals_per_kf
            tracks[body_part] = kfs
    return tracks


def parse_glb(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'glTF' or struct.unpack('<I', data[4:8])[0] != 2:
        raise RuntimeError('Not a GLB v2 file: ' + path)
    json_len = struct.unpack('<I', data[12:16])[0]
    gltf = json.loads(data[20:20 + json_len])
    bin_off = 20 + json_len
    # chunk header: length + type
    bin_chunk_len = struct.unpack('<I', data[bin_off:bin_off + 4])[0]
    bin_data = bytearray(data[bin_off + 8:bin_off + 8 + bin_chunk_len])
    return gltf, bin_data


def write_glb(path, gltf, bin_data):
    while len(bin_data) % 4 != 0:
        bin_data += b'\x00'
    gltf['buffers'][0]['byteLength'] = len(bin_data)
    json_str = json.dumps(gltf, separators=(',', ':'))
    while len(json_str) % 4 != 0:
        json_str += ' '
    json_bytes = json_str.encode('utf-8')
    total = 12 + 8 + len(json_bytes) + 8 + len(bin_data)
    out = struct.pack('<III', 0x46546C67, 2, total)
    out += struct.pack('<II', len(json_bytes), 0x4E4F534A) + json_bytes
    out += struct.pack('<II', len(bin_data), 0x004E4942) + bytes(bin_data)
    with open(path, 'wb') as f:
        f.write(out)


def build(export_dir, glb_path):
    registry_path = os.path.join(export_dir, 'anim_registry.txt')
    if not os.path.isfile(registry_path):
        raise RuntimeError('Missing anim_registry.txt in ' + export_dir)

    entries = []
    with open(registry_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line:
                continue
            parts = line.split('\t')
            entries.append(int(parts[0]))
    print(f'[animlib] Registry: {len(entries)} clips')

    gltf, bin_data = parse_glb(glb_path)

    # Node index by name
    node_idx = {n.get('name'): i for i, n in enumerate(gltf['nodes'])}
    for bone in BONE_NAMES:
        if bone not in node_idx:
            raise RuntimeError(f'GLB is missing node "{bone}" — expected GF body-part skeleton')

    def add_accessor(floats, acc_type, comps):
        """Append float32 data, create bufferView + accessor, return index."""
        offset = len(bin_data)
        for v in floats:
            bin_data.extend(struct.pack('<f', float(v)))
        view = {'buffer': 0, 'byteOffset': offset, 'byteLength': len(floats) * 4}
        gltf['bufferViews'].append(view)
        view_idx = len(gltf['bufferViews']) - 1
        acc = {
            'bufferView': view_idx,
            'componentType': 5126,
            'count': len(floats) // comps,
            'type': acc_type,
        }
        gltf['accessors'].append(acc)
        return len(gltf['accessors']) - 1

    # Rename legacy category clips: anim_00 -> idle, ...
    for anim in gltf.get('animations', []):
        name = anim.get('name', '')
        if name.startswith('anim_'):
            try:
                cat = int(name[5:])
                if 0 <= cat < len(LEGACY_NAMES):
                    anim['name'] = LEGACY_NAMES[cat]
            except ValueError:
                pass

    anims = gltf.setdefault('animations', [])
    skipped = 0
    added = 0

    for clip_id in entries:
        anim_path = os.path.join(export_dir, 'c%05d.anim' % clip_id)
        if not os.path.isfile(anim_path):
            skipped += 1
            continue
        tracks = parse_anim_file(anim_path)

        gltf_anim = {'name': 'c%05d' % clip_id, 'channels': [], 'samplers': []}

        for bone in BONE_NAMES:
            if bone == 'player':
                continue  # root motion is driven by server positions
            kfs = tracks.get(bone)
            if not kfs:
                continue
            kfs = sorted(kfs, key=lambda kf: kf[0])
            times = [kf[0] / GF_FPS for kf in kfs]
            rots = []
            for _, q in kfs:
                qy = normalize_quat(quat_zup_to_yup(q))
                rots.extend(qy)

            time_acc = add_accessor(times, 'SCALAR', 1)
            rot_acc = add_accessor(rots, 'VEC4', 4)

            gltf_anim['samplers'].append({
                'input': time_acc, 'output': rot_acc, 'interpolation': 'LINEAR',
            })
            gltf_anim['channels'].append({
                'sampler': len(gltf_anim['samplers']) - 1,
                'target': {'node': node_idx[bone], 'path': 'rotation'},
            })

        if gltf_anim['channels']:
            anims.append(gltf_anim)
            added += 1

    write_glb(glb_path, gltf, bin_data)
    print(f'[animlib] Added {added} clips ({skipped} skipped), renamed legacy clips.')
    print(f'[animlib] Output: {glb_path} ({os.path.getsize(glb_path) / 1024 / 1024:.1f} MB)')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    build(sys.argv[1], sys.argv[2])
