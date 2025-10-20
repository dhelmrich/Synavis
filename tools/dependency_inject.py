#!/usr/bin/env python3
"""Post-process a wheel to inject FFmpeg runtime libraries into the package.

Usage: tools/package_ffmpeg_into_wheel.py <wheel-path> [--dirs dir1;dir2] [--noprobe]
If --dirs is not provided, the script will probe common system locations.
If --noprobe is provided, the script will NOT expand sibling directories or
perform recursive glob searches; it will only search the directories provided
explicitly via --dirs (or the default candidates if --dirs is omitted and
--noprobe is not set).
"""
import sys
import os
import zipfile
import tempfile
import shutil
import glob
import hashlib
import base64


def compute_sha256_b64(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            h.update(chunk)
    digest = base64.urlsafe_b64encode(h.digest()).rstrip(b'=').decode('ascii')
    return digest, os.path.getsize(path)


def find_ffmpeg_libs(dirs, is_windows=False, noprobe=False):
    patterns = []
    if is_windows:
        patterns = ['**/*avcodec*.dll', '**/*avformat*.dll', '**/*avutil*.dll', '**/*swresample*.dll', '**/*swscale*.dll']
    else:
        patterns = ['**/libavcodec*.so*', '**/libavformat*.so*', '**/libavutil*.so*', '**/libswresample*.so*', '**/libswscale*.so*']
    found = []
    # If noprobe is set, do not expand sibling directories nor do recursive globbing.
    # Only use the exact directories provided in `dirs` and non-recursive globbing.
    expanded_dirs = []
    for d in dirs:
        if not d:
            continue
        d = os.path.abspath(d)
        expanded_dirs.append(d)

    # Remove duplicates and non-existent paths
    expanded_dirs = [os.path.normpath(x) for x in dict.fromkeys(expanded_dirs) if x and os.path.isdir(os.path.normpath(x))]

    for d in expanded_dirs:
        for pat in patterns:
            if noprobe:
                # when not probing, only match the immediate directory (non-recursive)
                pat_to_use = pat.replace('**/', '')
                found.extend(glob.glob(os.path.join(d, pat_to_use), recursive=False))
            else:
                # glob with recursive expansion
                # also expand candidate dirs to sibling/bin and common subfolders
                base = os.path.basename(d).lower()
                parent = os.path.dirname(d)
                probe_dirs = [d]
                if base in ('lib', 'lib64'):
                    probe_dirs.append(os.path.join(parent, 'bin'))
                    probe_dirs.append(os.path.join(parent, '..', 'bin'))
                if base == 'bin':
                    probe_dirs.append(parent)
                probe_dirs.append(os.path.join(d, 'Release'))
                probe_dirs.append(os.path.join(d, 'Debug'))
                # normalize and filter existing
                probe_dirs = [os.path.normpath(x) for x in dict.fromkeys(probe_dirs) if x and os.path.isdir(os.path.normpath(x))]
                for pd in probe_dirs:
                    found.extend(glob.glob(os.path.join(pd, pat), recursive=True))
    # unique
    seen = set()
    out = []
    for p in found:
        if p not in seen:
            seen.add(p)
            out.append(p)
    return out


def inject_libs(wheel_path, libs, package_name='synavis'):
    if not libs:
        print('No libs to inject')
        return
    tmp = tempfile.mkdtemp(prefix='wheel-edit-')
    try:
        print(f'Extracting wheel {wheel_path} to {tmp}')
        with zipfile.ZipFile(wheel_path, 'r') as z:
            z.extractall(tmp)
        pkg_dir = os.path.join(tmp, package_name)
        print('Injecting libraries into package directory:', pkg_dir)
        os.makedirs(os.path.join(pkg_dir, 'lib'), exist_ok=True)
        for lib in libs:
            shutil.copy(lib, os.path.join(pkg_dir, 'lib', os.path.basename(lib)))
        print('Recomputing RECORD file')
        # Recompute RECORD
        dist_info = None
        for e in os.listdir(tmp):
            if e.endswith('.dist-info'):
                dist_info = os.path.join(tmp, e)
                break
        if not dist_info:
            raise RuntimeError('Could not find .dist-info in wheel')
        print('Found dist-info directory:', dist_info)
        record_path = os.path.join(dist_info, 'RECORD')
        records = []
        for root, _, files in os.walk(tmp):
            for fname in files:
                rel = os.path.relpath(os.path.join(root, fname), tmp).replace(os.path.sep, '/')
                if rel == os.path.relpath(record_path, tmp).replace(os.path.sep, '/'):
                    continue
                full = os.path.join(root, fname)
                digest, size = compute_sha256_b64(full)
                records.append((rel, f"sha256={digest}", str(size)))
        print('Writing new RECORD file at:', record_path)
        with open(record_path, 'w', newline='') as rf:
            for rel, hashv, size in records:
                rf.write(f"{rel},{hashv},{size}\n")
            rf.write(f"{os.path.relpath(record_path, tmp).replace(os.path.sep, '/')},,\n")
        print('Repacking wheel')
        new_wheel = wheel_path + '.new'
        with zipfile.ZipFile(new_wheel, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            for root, _, files in os.walk(tmp):
                for fname in files:
                    full = os.path.join(root, fname)
                    rel = os.path.relpath(full, tmp).replace(os.path.sep, '/')
                    z.write(full, rel)
        shutil.move(new_wheel, wheel_path)
    finally:
        shutil.rmtree(tmp)


def main(argv):
    if len(argv) < 2:
        print('Usage: package_ffmpeg_into_wheel.py <wheel-path> [--dirs dir1;dir2]')
        return 2
    wheel = argv[1]
    dirs = []
    if '--dirs' in argv:
        i = argv.index('--dirs')
        if i + 1 < len(argv):
            dirs = argv[i + 1].split(os.pathsep)
    if not dirs:
        if os.name == 'nt':
            candidates = [r'C:\Program Files\ffmpeg\bin', r'C:\ffmpeg\bin']
        else:
            candidates = ['/usr/lib/x86_64-linux-gnu', '/usr/lib', '/usr/local/lib', '/lib64', '/lib']
        dirs = [d for d in candidates if os.path.isdir(d)]

    print('Probing directories for FFmpeg libraries:', dirs)
    libs = find_ffmpeg_libs(dirs, is_windows=(os.name == 'nt'))
    print('Found libs to inject:', libs)
    inject_libs(wheel, libs)
    print('Injection complete.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
