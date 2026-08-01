#!/usr/bin/env python3
"""Cut pak0.pk3 down to what a chosen set of maps actually needs.

The stock pak0 is 217 MB because it carries every map plus the full voice-over
and chat-line libraries. A WebXDC app has to travel through a chat message, so
the whole archive is the download - there is no incremental fetch to hide
behind.

What this does is a fixpoint dependency walk:

    map .bsp  ->  shader names (lump 1) + entity models (lump 0)
    shader name  ->  image files, via scripts/*.shader
    .md3 model  ->  more shader names, via its surface shader table
    ... repeat until nothing new is discovered

Anything never reached, plus the opt-out audio categories, is dropped. The
walk is deliberately conservative: an unresolved name falls back to keeping
every texture that shares its stem, because a missing texture is a visible
bug while an extra one only costs bytes.

Re-runnable, and reports what it kept and dropped per category so a missing
asset can be traced back to a decision rather than guessed at.

    ./scripts/trim-pak.py --maps radar goldrush
"""

import argparse
import collections
import concurrent.futures
import os
import posixpath
import re
import struct
import subprocess
import sys
import zipfile

# Directories whose contents are loaded by cgame/ui rather than by any map, so
# the dependency walk never reaches them. Dropping these breaks the HUD, the
# player models or the menus.
ALWAYS_PREFIXES = (
    'animations/',
    'botfiles/',
    'characters/',
    'fonts/',
    'gfx/',
    'icons/',
    'menu/',
    'models/players/',
    'models/multiplayer/',
    'models/weapons2/',
    'models/weapons/',
    'models/ammo/',
    'models/powerups/',
    # impact effects: blood, splashes, bullet marks. cgame registers these
    # per hit, so nothing static references them.
    'models/weaphits/',
    'scripts/',
    # all of sound/: the walk only reaches sounds named in entities, missing
    # ambients, vehicles and anything cgame triggers by hardcoded name. Once
    # transcoded the whole tree costs a fraction of one map's textures, so
    # precision here buys nothing and risks silent gaps.
    'sound/',
    # sprite animation frames (explosions, muzzle flashes). cgame registers
    # these by name at runtime, so no map ever references them and the walk
    # cannot see them - dropping them cost 117 missing shaders.
    'sprites/',
    'text/',
    'textures/effects/',
    'textures/sfx/',
    'textures/common/',
    'textures/decals/',
    # sky shaders reach their images through a second level of indirection
    # the walk does not follow; the whole set is well under a megabyte
    'textures/skies_sd/',
    'ui/',
    'weapons/',
)

# Loose files at the root of the pak that the engine or mod expects by name.
ALWAYS_SUFFIXES = ('.cfg', '.dat', '.menu', '.h', '.txt', '.arena', '.particle')

# Categories that can go without breaking play. Voice-overs and chat lines are
# the two biggest blocks in the pak and are pure flavour.
OPTIONAL = {
    'vo':    ('sound/vo/', 'sound/vo2/'),
    'chat':  ('sound/chat/',),
    'music': ('sound/music/',),
    'video': ('video/',),
}

IMAGE_EXTS = ('.tga', '.jpg', '.jpeg', '.png', '.pcx')

# shader stage directives that name an image
SHADER_IMAGE_RE = re.compile(
    r'^\s*(?:map|clampmap|animmap|editorimage|lightimage|qer_editorimage)\s+(.+)$',
    re.IGNORECASE)


def strip_ext(path):
    root, _ = posixpath.splitext(path)
    return root


def parse_bsp(data):
    """Return (shader names, model paths) referenced by a BSP."""
    magic, _version = struct.unpack_from('<4si', data, 0)
    if magic != b'IBSP':
        raise ValueError(f'not a BSP (magic {magic!r})')

    def lump(n):
        off, ln = struct.unpack_from('<ii', data, 8 + n * 8)
        return data[off:off + ln]

    shaders = []
    slump = lump(1)
    for k in range(len(slump) // 72):
        name = slump[k * 72:k * 72 + 64].split(b'\0')[0].decode('latin-1')
        if name:
            shaders.append(name)

    # entities is a plain text lump; any value that looks like a model path
    # counts, which covers misc_gamemodel/misc_model and the door/mover models
    ents = lump(0).split(b'\0')[0].decode('latin-1', 'replace')
    models = re.findall(r'"(models/[^"]+)"', ents)
    # entity sounds ("noise" keys) name sound files directly
    sounds = re.findall(r'"(sound/[^"]+)"', ents)

    return shaders, models + sounds


def parse_md3(data):
    """Return shader names referenced by an MD3's surfaces."""
    if data[:4] != b'IDP3':
        return []
    try:
        num_surfaces = struct.unpack_from('<i', data, 84)[0]
        ofs_surfaces = struct.unpack_from('<i', data, 96)[0]
    except struct.error:
        return []

    out = []
    off = ofs_surfaces
    for _ in range(num_surfaces):
        if off + 108 > len(data) or data[off:off + 4] != b'IDP3':
            break
        num_shaders = struct.unpack_from('<i', data, off + 76)[0]
        ofs_shaders = struct.unpack_from('<i', data, off + 92)[0]
        ofs_end = struct.unpack_from('<i', data, off + 104)[0]
        for s in range(num_shaders):
            p = off + ofs_shaders + s * 68
            if p + 64 > len(data):
                break
            name = data[p:p + 64].split(b'\0')[0].decode('latin-1')
            if name:
                out.append(name)
        if ofs_end <= 0:
            break
        off += ofs_end
    return out


def parse_shader_scripts(z, names):
    """Map shader name -> set of image paths, from every scripts/*.shader."""
    table = collections.defaultdict(set)
    for entry in names:
        if not (entry.startswith('scripts/') and entry.endswith('.shader')):
            continue
        try:
            text = z.read(entry).decode('latin-1', 'replace')
        except Exception:
            continue

        # strip // comments, then walk brace depth to attribute images to the
        # shader block they appear in
        text = re.sub(r'//[^\n]*', '', text)
        current = None
        depth = 0
        for raw in text.splitlines():
            line = raw.strip()
            if not line:
                continue
            if line.startswith('{'):
                depth += line.count('{')
                continue
            if line.startswith('}'):
                depth -= line.count('}')
                if depth <= 0:
                    depth = 0
                    current = None
                continue

            depth += line.count('{') - line.count('}')

            m = SHADER_IMAGE_RE.match(line)
            if m and current:
                for tok in m.group(1).split():
                    tok = tok.strip().lower()
                    if tok in ('$lightmap', '$whiteimage', '$dynamic', '-'):
                        continue
                    table[current].add(tok)
            elif current is None and depth >= 0 and not line.startswith(('{', '}')):
                # a bare token at depth 0 opens a shader block
                if depth == 0 and ' ' not in line:
                    current = line.lower()
    return table


# asset-shaped string literals: a known top-level directory followed by a path
ASSET_STRING_RE = re.compile(
    rb'(?:models|textures|gfx|sprites|sound|ui|menu|icons|weapons|characters|levelshots)'
    rb'/[A-Za-z0-9_\-./]{2,120}')


def scan_binary_for_assets(blob):
    """Pull asset paths out of a compiled binary's string literals."""
    out = set()
    for m in ASSET_STRING_RE.finditer(blob):
        s = m.group(0).decode('latin-1').lower().rstrip('./')
        if len(s) > 6:
            out.add(s)
    return out


def encoder_cmd(quality):
    """Pick a Vorbis encoder. oggenc first - Homebrew's ffmpeg is often built
    without libvorbis, leaving only the experimental native encoder."""
    from shutil import which
    if which('oggenc'):
        return ['oggenc', '-Q', '-q', str(quality), '-o', '-', '-']
    if which('ffmpeg'):
        enc = subprocess.run(['ffmpeg', '-hide_banner', '-encoders'],
                             capture_output=True, text=True).stdout
        if 'libvorbis' in enc:
            return ['ffmpeg', '-hide_banner', '-loglevel', 'error',
                    '-f', 'wav', '-i', 'pipe:0', '-c:a', 'libvorbis',
                    '-q:a', str(quality), '-f', 'ogg', 'pipe:1']
        return ['ffmpeg', '-hide_banner', '-loglevel', 'error',
                '-f', 'wav', '-i', 'pipe:0', '-c:a', 'vorbis', '-strict', '-2',
                '-q:a', str(quality), '-f', 'ogg', 'pipe:1']
    return None


def transcode_one(payload):
    """Re-encode one WAV to Vorbis. Returns (name, ogg bytes, err)."""
    name, wav, cmd = payload
    try:
        proc = subprocess.run(cmd, input=wav, capture_output=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        detail = getattr(e, 'stderr', b'') or b''
        return name, None, (detail.decode('utf-8', 'replace').strip()
                            or str(e))[:160]

    ogg = proc.stdout
    # a "smaller" result that is empty means ffmpeg gave up on a malformed
    # source; keeping the original is always safe
    if not ogg or len(ogg) >= len(wav):
        return name, None, 'no gain'
    return name, ogg, None


def transcode_all(z, wavs, quality, jobs):
    out = {}
    skipped = []
    cmd = encoder_cmd(quality)
    if cmd is None:
        print('  no vorbis encoder found (install vorbis-tools) - keeping wav')
        return out
    print(f'  encoder: {cmd[0]}')
    payloads = [(n, z.read(n), cmd) for n in wavs]
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for i, (name, ogg, err) in enumerate(ex.map(transcode_one, payloads), 1):
            if ogg is None:
                skipped.append((name, err))
            else:
                out[name] = ogg
            if i % 200 == 0:
                print(f'    {i}/{len(payloads)}')
    if skipped:
        print(f'  kept {len(skipped)} as wav (first few): '
              + ', '.join(f'{n} [{e}]' for n, e in skipped[:3]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--maps', nargs='+', default=['radar', 'goldrush'])
    ap.add_argument('--src', default='assets/etmain')
    ap.add_argument('--out', default='assets-trimmed/etmain')
    ap.add_argument('--mod-binary', nargs='*', default=[],
                    help='loose mod binaries to mine for asset strings, e.g. '
                         'qagame.mp.wasm32.wasm which is served outside the pak')
    ap.add_argument('--mod-pak', default=None,
                    help='the legacy_*.pk3; its models reference pak0 textures, '
                         'so it has to seed the walk or those textures vanish')
    ap.add_argument('--transcode-audio', action='store_true',
                    help='re-encode .wav to Vorbis .ogg (needs ffmpeg); keeps '
                         'every sound at roughly a tenth of the size')
    ap.add_argument('--audio-quality', type=float, default=0.0,
                    help='ffmpeg libvorbis -q:a (default 0, ~64kbps)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    for k in OPTIONAL:
        ap.add_argument(f'--drop-{k}', action='store_true',
                        help=f'drop {OPTIONAL[k][0]} entirely')
    args = ap.parse_args()

    src_pak = os.path.join(args.src, 'pak0.pk3')
    if not os.path.exists(src_pak):
        sys.exit(f'{src_pak} not found')

    z = zipfile.ZipFile(src_pak)
    names = z.namelist()
    by_name = {n: i for n, i in zip(names, z.infolist())}
    lower = {n.lower(): n for n in names}

    # index texture files by extensionless path so a shader naming
    # "textures/x/y" resolves to textures/x/y.tga or .jpg
    stems = collections.defaultdict(list)
    for n in names:
        if n.lower().endswith(IMAGE_EXTS):
            stems[strip_ext(n.lower())].append(n)

    keep = set()
    dropped_maps = []

    # ---- seed from the chosen maps
    pending_shaders = set()
    pending_models = set()

    for n in names:
        if not n.startswith('maps/'):
            continue
        base = posixpath.basename(n)
        stem = base.split('.')[0].replace('_lms', '').replace('_tracemap', '')
        indir = n[len('maps/'):].split('/')[0] if '/' in n[len('maps/'):] else None
        owner = indir if indir else stem
        if owner in args.maps:
            keep.add(n)
        elif n.endswith('.bsp'):
            dropped_maps.append(n)

    for m in args.maps:
        bsp = f'maps/{m}.bsp'
        if bsp not in by_name:
            sys.exit(f'no {bsp} in the pak (have: '
                     f'{", ".join(sorted(x[5:-4] for x in names if x.endswith(".bsp")))})')
        shaders, models = parse_bsp(z.read(bsp))
        pending_shaders.update(s.lower() for s in shaders)
        pending_models.update(x.lower() for x in models)
        print(f'  {m}: {len(shaders)} shader refs, {len(models)} entity assets')

    shader_table = parse_shader_scripts(z, names)
    print(f'  parsed {len(shader_table)} shader definitions')

    # The mod pak's models and shaders point at pak0 textures. Without seeding
    # from it, anything the mod spawns rather than the map places (supply
    # stands, command posts) loses its skin.
    if args.mod_pak:
        mz = zipfile.ZipFile(args.mod_pak)
        mod_names = mz.namelist()
        before = len(pending_shaders)
        for entry in mod_names:
            low = entry.lower()
            if low.endswith(('.md3', '.mdc')):
                pending_shaders.update(s.lower() for s in parse_md3(mz.read(entry)))
            elif low.endswith('.skin'):
                # "surface,path/to/shader" per line
                text = mz.read(entry).decode('latin-1', 'replace')
                for line in text.splitlines():
                    if ',' in line:
                        ref = line.split(',', 1)[1].strip().strip('"')
                        if ref:
                            pending_shaders.add(ref.lower())
        for name, imgs in parse_shader_scripts(mz, mod_names).items():
            shader_table.setdefault(name, set()).update(imgs)
            pending_shaders.add(name)

        # cgame/qagame register plenty of assets from string literals in code
        # ("models/mapobjects/supplystands/stand_ammo.md3"). Nothing in any
        # file format points at those, so mine the wasm for asset-shaped
        # strings - it is the only way to see this whole class.
        for entry in mod_names:
            if not entry.lower().endswith('.wasm'):
                continue
            pending_shaders.update(scan_binary_for_assets(mz.read(entry)))
        print(f'  mod pak seeded {len(pending_shaders) - before} more references')

    for path in args.mod_binary:
        if os.path.exists(path):
            with open(path, 'rb') as f:
                found = scan_binary_for_assets(f.read())
            pending_shaders.update(found)
            print(f'  {posixpath.basename(path)}: {len(found)} asset strings')

    # ---- fixpoint walk
    seen_shaders = set()
    seen_models = set()

    def keep_stem(path_noext):
        """Keep every image sharing this extensionless path."""
        hit = stems.get(path_noext)
        if hit:
            keep.update(hit)
            return True
        return False

    while pending_shaders or pending_models:
        while pending_shaders:
            s = pending_shaders.pop()
            if s in seen_shaders:
                continue
            seen_shaders.add(s)

            # a shader may be a script definition, a bare image, or both
            resolved = False
            for img in shader_table.get(s, ()):
                if keep_stem(strip_ext(img)):
                    resolved = True
            if keep_stem(strip_ext(s)):
                resolved = True
            if not resolved and s.endswith('.md3'):
                pending_models.add(s)

        while pending_models:
            m = pending_models.pop()
            if m in seen_models:
                continue
            seen_models.add(m)

            real = lower.get(m)
            if real is None:
                # sounds and models are often named without an extension
                for ext in ('.md3', '.mdc', '.wav', '.ogg'):
                    real = lower.get(m + ext)
                    if real:
                        break
            if real is None:
                continue
            keep.add(real)

            if real.lower().endswith(('.md3', '.mdc')):
                for sh in parse_md3(z.read(real)):
                    if sh.lower() not in seen_shaders:
                        pending_shaders.add(sh.lower())

    # ---- model siblings
    #
    # A model's textures sit next to it, and are reached through paths the
    # walk cannot follow: .skin files map surfaces to shaders, and animMap
    # sequences name frames (command1..command7) that appear nowhere else.
    # Model directories are small and single-purpose, so once a model is kept
    # the cheapest correct thing is to keep everything beside it.
    model_dirs = {posixpath.dirname(n) for n in keep
                  if n.lower().endswith(('.md3', '.mdc', '.skin'))}
    siblings = 0
    for n in names:
        if n.endswith('/') or n in keep:
            continue
        if posixpath.dirname(n) in model_dirs and \
                n.lower().endswith(IMAGE_EXTS + ('.skin',)):
            keep.add(n)
            siblings += 1
    if siblings:
        print(f'  kept {siblings} textures/skins beside {len(model_dirs)} models')

    # ---- unconditional keeps and opt-out drops
    skip_prefixes = []
    keep_prefixes = []
    for k, prefixes in OPTIONAL.items():
        (skip_prefixes if getattr(args, f'drop_{k}') else keep_prefixes).extend(prefixes)

    for n in names:
        if n.endswith('/'):
            continue
        low = n.lower()
        if any(low.startswith(p) for p in skip_prefixes):
            keep.discard(n)
            continue
        if low.startswith(ALWAYS_PREFIXES) or low.endswith(ALWAYS_SUFFIXES):
            keep.add(n)
        if keep_prefixes and low.startswith(tuple(keep_prefixes)):
            keep.add(n)
        if low.startswith('levelshots/'):
            if any(m in low for m in args.maps):
                keep.add(n)

    # never keep another map's data, whatever the walk decided
    for n in dropped_maps:
        keep.discard(n)
    for n in names:
        parts = n.split('/')
        if len(parts) > 2 and parts[0] == 'maps' and parts[1] not in args.maps:
            keep.discard(n)

    # ---- write
    os.makedirs(args.out, exist_ok=True)
    out_pak = os.path.join(args.out, 'pak0.pk3')
    kept_bytes = dropped_bytes = 0
    cat_kept = collections.Counter()
    cat_drop = collections.Counter()

    def category(n):
        p = n.split('/')
        return p[0] if len(p) == 1 else p[0] + '/' + (p[1] if len(p) > 2 else '')

    audio_before = audio_after = 0
    transcoded = {}
    if args.transcode_audio:
        wavs = sorted(n for n in keep if n.lower().endswith('.wav'))
        print(f'\ntranscoding {len(wavs)} wav -> ogg (q{args.audio_quality:g}, '
              f'{args.jobs} jobs)...')
        transcoded = transcode_all(z, wavs, args.audio_quality, args.jobs)
        for n, blob in transcoded.items():
            audio_before += by_name[n].compress_size
            audio_after += len(blob)
        print(f'  audio: {audio_before/2**20:.0f} MB -> {audio_after/2**20:.0f} MB')

    with zipfile.ZipFile(out_pak, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for n in names:
            if n.endswith('/'):
                continue
            info = by_name[n]
            if n in keep:
                if n in transcoded:
                    # .ogg is already compressed; storing it uncompressed keeps
                    # the zip from wasting time re-deflating incompressible data
                    ogg = strip_ext(n) + '.ogg'
                    out.writestr(ogg, transcoded[n], zipfile.ZIP_STORED)
                    kept_bytes += len(transcoded[n])
                    cat_kept[category(n)] += len(transcoded[n])
                    continue
                out.writestr(n, z.read(n))
                kept_bytes += info.compress_size
                cat_kept[category(n)] += info.compress_size
            else:
                dropped_bytes += info.compress_size
                cat_drop[category(n)] += info.compress_size

    # pak1/pak2/mp_bin are small and carry engine-critical data; pass through
    for extra in ('pak1.pk3', 'pak2.pk3', 'mp_bin.pk3'):
        s = os.path.join(args.src, extra)
        if os.path.exists(s):
            d = os.path.join(args.out, extra)
            with open(s, 'rb') as fi, open(d, 'wb') as fo:
                fo.write(fi.read())

    new_size = os.path.getsize(out_pak)
    old_size = os.path.getsize(src_pak)

    print(f'\nkept {len(keep)} / {len(names)} entries')
    print(f'pak0.pk3: {old_size/2**20:.0f} MB -> {new_size/2**20:.0f} MB '
          f'({100*new_size/old_size:.0f}%)')

    print(f'\n{"dropped":<34}{"MB":>8}')
    for k, v in cat_drop.most_common(12):
        if v > 2**19:
            print(f'  {k:<32}{v/2**20:>8.1f}')
    print(f'\n{"kept":<34}{"MB":>8}')
    for k, v in cat_kept.most_common(12):
        if v > 2**19:
            print(f'  {k:<32}{v/2**20:>8.1f}')

    print(f'\nmaps kept: {", ".join(args.maps)}')
    print(f'maps dropped: {", ".join(sorted(x[5:-4] for x in dropped_maps))}')
    skipped = [k for k in OPTIONAL if getattr(args, f'drop_{k}')]
    if skipped:
        print(f'categories dropped wholesale: {", ".join(skipped)}')
    if args.transcode_audio and audio_before:
        print(f'audio transcoded to vorbis: {audio_before/2**20:.0f} MB -> '
              f'{audio_after/2**20:.0f} MB (all sounds retained)')


if __name__ == '__main__':
    main()
