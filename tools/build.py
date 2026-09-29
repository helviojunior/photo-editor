#!/usr/bin/env python3
"""Builder do PhotoEditor desktop: runtime embarcado, modelo, frontend e pacote.

So usa a biblioteca padrao (roda com qualquer python3 >= 3.9). Nada e
compilado: o Python e o CPython pronto do python-build-standalone e toda
dependencia e wheel binaria do PyPI. Por isso um unico container Linux monta o
pacote de TODAS as plataformas — ``pip --platform`` baixa as wheels do alvo
(ver tools/Dockerfile e tools/docker-build.sh).

Comandos:

    runtime   [--target T]   Python embarcado + dependencias em .runtime/<T>/
    model                     modelo SAM 2.1 (camadas) em models/
    frontend                  build do React em frontend/build/
    run       [-- args]       roda o app desktop no runtime do host (dev)
    dist      [--target T...] instalador em dist/: .dmg (macOS), .msi (Windows),
                              .tar.gz (Linux); --keep-dirs mantem a pasta aberta

Alvos: macos-arm64, macos-x64, linux-x64, linux-arm64, windows-x64, host, all.
"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# `tools.packaging` (instaladores) importa a partir da raiz do repositorio.
sys.path.insert(0, str(ROOT))
CACHE = ROOT / '.cache'
# Dentro do container (tools/Dockerfile) o "host" e Linux: os runtimes dele
# ficam separados dos do desenvolvedor, que roda o app no proprio SO.
BUILDER = bool(os.environ.get('PHOTOEDITOR_BUILDER'))
RUNTIMES = ROOT / ('.runtime-builder' if BUILDER else '.runtime')
DIST = ROOT / 'dist'
MODELS = ROOT / 'models'

APP_NAME = 'PhotoEditor'

# CPython embarcado (https://github.com/astral-sh/python-build-standalone).
# Fixo: o mesmo build sempre empacota o mesmo interpretador.
PBS_RELEASE = '20260924'
PYTHON_VERSION = '3.12.14'
PYTHON_ABI = 'cp312'

# A tag e o MAXIMO aceito (as versoes anteriores compativeis tambem entram,
# ver pip_platforms) — e define o piso de SO do pacote, ditado pela wheel mais
# exigente:
#   macOS 14+ (onnxruntime) · glibc 2.34+ no Linux x64 (PySide6: Ubuntu 22.04,
#   Debian 12, Fedora 35) · glibc 2.39+ no Linux arm64 (PySide6: Ubuntu 24.04)
#   · Windows 10/11 x64.
TARGETS = {
    'macos-arm64': {'pbs': 'aarch64-apple-darwin', 'platform': 'macosx_14_0_arm64', 'os': 'macos'},
    'macos-x64': {'pbs': 'x86_64-apple-darwin', 'platform': 'macosx_14_0_x86_64', 'os': 'macos'},
    'linux-x64': {'pbs': 'x86_64-unknown-linux-gnu', 'platform': 'manylinux_2_34_x86_64', 'os': 'linux'},
    'linux-arm64': {'pbs': 'aarch64-unknown-linux-gnu', 'platform': 'manylinux_2_39_aarch64', 'os': 'linux'},
    'windows-x64': {'pbs': 'x86_64-pc-windows-msvc', 'platform': 'win_amd64', 'os': 'windows'},
}
# `all` = o que se distribui hoje. macOS Intel: o onnxruntime parou de publicar
# wheel x86_64 para macOS; `--target macos-x64` continua disponivel para testar.
ALL_TARGETS = ['macos-arm64', 'windows-x64', 'linux-x64', 'linux-arm64']

REQUIREMENTS = [ROOT / 'backend' / 'requirements.txt', ROOT / 'desktop' / 'requirements.txt']

# Modelo de segmentacao das camadas: SAM 2.1 tiny (Meta, Apache-2.0) em ONNX,
# ~155 MB. Revisao e hashes fixos: o pacote sempre leva o mesmo modelo.
SEGMENT_MODEL = {
    'dir': 'sam2.1-hiera-tiny',
    'repo': 'onnx-community/sam2.1-hiera-tiny-ONNX',
    'revision': '814a066640debee5a91e70aa401fb8e17e030503',
    'files': {
        'vision_encoder.onnx': '4f30aacd3aaefbca81a0b7fe4c1fc96345570ea0a6f80ced599493d1b3be2e8c',
        'vision_encoder.onnx_data': 'e83df9866a5afe68ea7f0f721f18f65137fc3acbf0da1c74e946d363e09c69cc',
        'prompt_encoder_mask_decoder.onnx': '874414704c5d686db7d206a35f6e15d26563d50c8c4468fccc6739bd7e491dcf',
        'prompt_encoder_mask_decoder.onnx_data': 'e9874d900dd4134ed60eab1e97910327c2419e0b2954485d8fd6e7f1a1470f47',
    },
}


def log(msg):
    print(f'==> {msg}', flush=True)


def run(cmd, **kw):
    print('    $ ' + ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def version() -> str:
    return (ROOT / 'VERSION').read_text().strip()


def host_target() -> str:
    system = platform.system()
    machine = platform.machine().lower()
    arch = 'arm64' if machine in ('arm64', 'aarch64') else 'x64'
    name = {'Darwin': 'macos', 'Linux': 'linux', 'Windows': 'windows'}.get(system)
    if not name:
        sys.exit(f'Unsupported host system: {system}')
    return f'{name}-{arch}'


def resolve_targets(names) -> list:
    out = []
    for name in names or ['host']:
        if name == 'all':
            out.extend(ALL_TARGETS)
        elif name == 'host':
            out.append(host_target())
        elif name in TARGETS:
            out.append(name)
        else:
            sys.exit(f'Unknown target: {name} (choose from {", ".join(TARGETS)}, host, all)')
    return list(dict.fromkeys(out))


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def download(url: str, dest: Path, expected_sha256: str = None):
    """Baixa para ``dest`` (uma vez so; o cache fica em .cache/)."""
    if dest.is_file() and (not expected_sha256 or sha256(dest) == expected_sha256):
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + '.part')
    log(f'Downloading {url}')
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=60) as r, open(tmp, 'wb') as f:
                shutil.copyfileobj(r, f, 1 << 20)
            break
        except OSError:
            if attempt == 2:
                raise
            time.sleep(2 + attempt * 3)
    if expected_sha256 and sha256(tmp) != expected_sha256:
        tmp.unlink()
        sys.exit(f'Checksum mismatch for {url}')
    os.replace(tmp, dest)
    return dest


# ---------------------------------------------------------------------------
# Runtime: CPython embarcado + dependencias
# ---------------------------------------------------------------------------

def pip_platforms(target: str) -> list:
    """Tags de plataforma para o ``pip --platform`` do alvo.

    O pip expande sozinho a tag do macOS para as versoes anteriores, mas NAO
    as ``manylinux_2_N``: sem a lista explicita, wheel publicada so como
    ``manylinux_2_28`` (Pillow) nao serviria para um alvo ``manylinux_2_34``.
    """
    tag = TARGETS[target]['platform']
    if not tag.startswith('manylinux_2_'):
        return [tag]
    _, _, minor, arch = tag.split('_', 3)
    tags = [f'manylinux_2_{m}_{arch}' for m in range(int(minor), 16, -1)]
    return tags + [f'manylinux2014_{arch}']


def pbs_asset(target: str) -> str:
    triple = TARGETS[target]['pbs']
    return f'cpython-{PYTHON_VERSION}+{PBS_RELEASE}-{triple}-install_only_stripped.tar.gz'


def pbs_checksums() -> dict:
    path = download(
        f'https://github.com/astral-sh/python-build-standalone/releases/download/'
        f'{PBS_RELEASE}/SHA256SUMS',
        CACHE / 'pbs' / f'SHA256SUMS-{PBS_RELEASE}')
    sums = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) == 2:
            sums[parts[1]] = parts[0]
    return sums


def python_dir(target: str) -> Path:
    return RUNTIMES / target / 'python'


def python_exe(target: str, gui=False) -> Path:
    base = python_dir(target)
    if TARGETS[target]['os'] == 'windows':
        return base / ('pythonw.exe' if gui else 'python.exe')
    return base / 'bin' / 'python3'


def site_packages(target: str) -> Path:
    base = python_dir(target)
    if TARGETS[target]['os'] == 'windows':
        return base / 'Lib' / 'site-packages'
    return base / 'lib' / f'python{PYTHON_VERSION.rsplit(".", 1)[0]}' / 'site-packages'


def requirements_stamp(target: str) -> str:
    h = hashlib.sha256(f'{PBS_RELEASE}:{PYTHON_VERSION}:{target}'.encode())
    for req in REQUIREMENTS:
        h.update(req.read_bytes())
    return h.hexdigest()


def cmd_runtime(targets):
    for target in targets:
        build_runtime(target)


def build_runtime(target: str):
    stamp_file = RUNTIMES / target / 'deps.stamp'
    stamp = requirements_stamp(target)
    if stamp_file.is_file() and stamp_file.read_text() == stamp and python_dir(target).is_dir():
        log(f'Runtime {target} is up to date')
        return

    asset = pbs_asset(target)
    tarball = download(
        f'https://github.com/astral-sh/python-build-standalone/releases/download/'
        f'{PBS_RELEASE}/{asset.replace("+", "%2B")}',
        CACHE / 'pbs' / asset, pbs_checksums().get(asset))

    log(f'Extracting CPython {PYTHON_VERSION} for {target}')
    shutil.rmtree(RUNTIMES / target, ignore_errors=True)
    (RUNTIMES / target).mkdir(parents=True)
    with tarfile.open(tarball) as tar:
        # Sem share/terminfo (so o curses usa): no Linux ele tem nomes que so
        # diferem por maiuscula, e num disco que ignora a caixa (macOS, onde o
        # repositorio costuma estar montado no builder) viram laco de symlink.
        members = [m for m in tar.getmembers() if '/share/terminfo/' not in m.name]
        if sys.version_info >= (3, 12):
            tar.extractall(RUNTIMES / target, members=members, filter='tar')
        else:
            tar.extractall(RUNTIMES / target, members=members)

    reqs = [a for req in REQUIREMENTS for a in ('-r', req)]
    if target == host_target() and not BUILDER:
        # Dev: o proprio Python embarcado instala (instalacao nativa, normal).
        # No builder sempre cross: o glibc do container nao e o piso do pacote.
        run([python_exe(target), '-m', 'pip', 'install', '--disable-pip-version-check',
             '--no-warn-script-location', '--only-binary=:all:', *reqs])
    else:
        # Outro SO/arquitetura: o pip do builder baixa as wheels DO ALVO e as
        # descompacta direto no site-packages dele. Sem sdist (nada compila).
        log(f'Installing {target} wheels (cross, {TARGETS[target]["platform"]})')
        run([sys.executable, '-m', 'pip', 'install', '--disable-pip-version-check',
             '--no-warn-script-location', '--no-compile', '--upgrade',
             '--target', site_packages(target),
             *[a for tag in pip_platforms(target) for a in ('--platform', tag)],
             '--python-version', PYTHON_VERSION.rsplit('.', 1)[0],
             '--implementation', 'cp', '--abi', PYTHON_ABI,
             '--only-binary=:all:', *reqs])
    stamp_file.write_text(stamp)


# ---------------------------------------------------------------------------
# Modelo e frontend
# ---------------------------------------------------------------------------

def cmd_model():
    dest = MODELS / SEGMENT_MODEL['dir']
    for name, digest in SEGMENT_MODEL['files'].items():
        download(
            f'https://huggingface.co/{SEGMENT_MODEL["repo"]}/resolve/'
            f'{SEGMENT_MODEL["revision"]}/onnx/{name}',
            dest / name, digest)
    log(f'Segmentation model ready in {dest.relative_to(ROOT)}')
    return dest


def cmd_frontend(isolated=False):
    """Build do React. ``isolated`` (builder em Docker) compila numa copia,
    para nao misturar o node_modules do container com o do host."""
    src = ROOT / 'frontend'
    work = src
    if isolated:
        work = CACHE / 'frontend-work'
        if work.exists():
            for item in work.iterdir():
                if item.name != 'node_modules':
                    shutil.rmtree(item) if item.is_dir() else item.unlink()
        shutil.copytree(src, work, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('node_modules', 'build'))

    yarn = shutil.which('yarn')
    if not yarn:
        sys.exit('yarn not found: run the build inside the builder container '
                 '(tools/docker-build.sh) or install Node 20 + yarn.')
    env = dict(os.environ,
               REACT_APP_VERSION=version(),
               REACT_APP_BUILD_TS=os.environ.get('REACT_APP_BUILD_TS')
               or time.strftime('%Y%m%d%H%M%S', time.gmtime()),
               GENERATE_SOURCEMAP='false')
    run([yarn, 'install', '--frozen-lockfile', '--non-interactive'], cwd=work, env=env)
    run([yarn, 'build'], cwd=work, env=env)
    if isolated:
        shutil.rmtree(src / 'build', ignore_errors=True)
        shutil.copytree(work / 'build', src / 'build')
    log('Frontend build ready in frontend/build')


# ---------------------------------------------------------------------------
# Dev: rodar no runtime do host
# ---------------------------------------------------------------------------

def cmd_run(args):
    target = host_target()
    build_runtime(target)
    if not (MODELS / SEGMENT_MODEL['dir']).is_dir():
        cmd_model()
    if not (ROOT / 'frontend' / 'build' / 'index.html').is_file():
        cmd_frontend()
    cmd = [python_exe(target), '-E', '-s', ROOT / 'desktop', *args]
    print('    $ ' + ' '.join(str(c) for c in cmd), flush=True)
    sys.exit(subprocess.call([str(c) for c in cmd], cwd=ROOT))


# ---------------------------------------------------------------------------
# Pacote
# ---------------------------------------------------------------------------

LAUNCHER_UNIX = """#!/bin/sh
# Abre o PhotoEditor com o Python embarcado. -E/-s: nada do Python do sistema
# (PYTHONPATH, site-packages do usuario) vaza para dentro do app.
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/runtime/bin/python3" -E -s "$HERE/app/desktop" "$@"
"""

LAUNCHER_WINDOWS = """@echo off
rem Abre o PhotoEditor com o Python embarcado (pythonw: sem janela de console).
start "" "%~dp0runtime\\pythonw.exe" -E -s "%~dp0app\\desktop" %*
"""

APP_IGNORE = shutil.ignore_patterns(
    '__pycache__', '*.pyc', 'tests.py', 'test_*.py', 'media', 'staticfiles', '.env',
    # saida antiga de collectstatic (DEBUG): o WhiteNoise le das apps
    'static')


def cmd_dist(targets, skip_frontend=False, keep_dirs=False):
    ver = version()
    if not skip_frontend:
        cmd_frontend(isolated=BUILDER)
    frontend_build = ROOT / 'frontend' / 'build'
    if not (frontend_build / 'index.html').is_file():
        sys.exit('frontend/build is missing: run `tools/build.py frontend` first.')
    model = cmd_model()

    for target in targets:
        build_runtime(target)
        name = f'{APP_NAME}-{ver}-{target}'
        out = DIST / name
        log(f'Assembling {out.relative_to(ROOT)}')
        shutil.rmtree(out, ignore_errors=True)
        app = out / 'app'
        shutil.copytree(python_dir(target), out / 'runtime', symlinks=True,
                        ignore=shutil.ignore_patterns('__pycache__'))
        shutil.copytree(ROOT / 'backend', app / 'backend', ignore=APP_IGNORE)
        shutil.copytree(ROOT / 'desktop', app / 'desktop', ignore=APP_IGNORE)
        shutil.copytree(frontend_build, app / 'frontend' / 'build')
        shutil.copytree(model, app / 'models' / model.name)
        shutil.copy2(ROOT / 'VERSION', app / 'VERSION')
        shutil.copy2(ROOT / 'LICENSE', out / 'LICENSE')
        (app / 'build-info.json').write_text(json.dumps({
            'version': ver, 'target': target, 'python': PYTHON_VERSION,
            'pbs_release': PBS_RELEASE,
            'built_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        }, indent=2))

        if TARGETS[target]['os'] == 'windows':
            (out / f'{APP_NAME}.cmd').write_text(LAUNCHER_WINDOWS.replace('\n', '\r\n'))
        else:
            launcher = out / (f'{APP_NAME}.command' if TARGETS[target]['os'] == 'macos' else APP_NAME)
            launcher.write_text(LAUNCHER_UNIX)
            launcher.chmod(0o755)
        precompile(out)

        artifact = package(out, target, ver)
        # A pasta aberta pesa ~2-3 GB por alvo e o instalador tem o mesmo
        # conteudo: fica so com --keep-dirs (para testar o pacote no lugar).
        if not keep_dirs:
            shutil.rmtree(out, ignore_errors=True)
        log(f'Package ready: {artifact.relative_to(ROOT)}')


def precompile(folder: Path):
    """``.pyc`` de tudo, ja no pacote.

    Instalado, o app mora onde nao se escreve (``Program Files``, dentro do
    ``.app``): sem ``.pyc`` pronto o Python recompilaria tudo a cada abertura.
    ``unchecked-hash``: vale sem conferir a data do fonte, que o instalador
    nao preserva. So com o mesmo Python do runtime (3.12, o do builder) — o
    formato do ``.pyc`` muda entre versoes.
    """
    if sys.version_info[:2] != tuple(int(p) for p in PYTHON_VERSION.split('.')[:2]):
        log(f'Skipping .pyc precompilation (builder Python is not {PYTHON_VERSION})')
        return
    import compileall
    import py_compile
    log('Precompiling .pyc')
    for sub in ('runtime', 'app'):
        compileall.compile_dir(
            str(folder / sub), quiet=2, workers=0,
            invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)


def package(folder: Path, target: str, ver: str) -> Path:
    """O instalador do SO: .dmg (macOS), .msi (Windows), .tar.gz (Linux)."""
    os_name = TARGETS[target]['os']
    if os_name == 'macos':
        from tools.packaging import macos
        log('Building PhotoEditor.app and the .dmg')
        app = macos.build_app(folder, CACHE / 'pkg' / target, ver)
        return macos.build_dmg(app, DIST / f'{folder.name}.dmg', CACHE / 'pkg' / f'{target}-dmg')
    if os_name == 'windows':
        from tools.packaging import windows
        log('Building the .msi')
        return windows.build_msi(folder, DIST / f'{folder.name}.msi', ver,
                                 CACHE / 'pkg' / f'{target}-msi')
    return archive_dist(folder, os_name)


def archive_dist(folder: Path, os_name: str) -> Path:
    """zip no Windows; tar.gz no resto (preserva bit de execucao e symlinks)."""
    if os_name == 'windows':
        # Nao with_suffix: ".26-windows-x64" seria tomado por extensao.
        archive = folder.parent / f'{folder.name}.zip'
        archive.unlink(missing_ok=True)
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            for path in sorted(folder.rglob('*')):
                zf.write(path, path.relative_to(folder.parent))
        return archive
    archive = folder.parent / f'{folder.name}.tar.gz'
    archive.unlink(missing_ok=True)
    with tarfile.open(archive, 'w:gz', compresslevel=6) as tar:
        tar.add(folder, arcname=folder.name)
    return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('runtime')
    p.add_argument('--target', action='append')
    sub.add_parser('model')
    p = sub.add_parser('frontend')
    p.add_argument('--isolated', action='store_true')
    p = sub.add_parser('run')
    p.add_argument('args', nargs=argparse.REMAINDER)
    p = sub.add_parser('dist')
    p.add_argument('--target', action='append')
    p.add_argument('--skip-frontend', action='store_true')
    p.add_argument('--keep-dirs', action='store_true',
                   help='keep dist/<package>/ unpacked next to the archive')
    args = parser.parse_args()

    if args.command == 'runtime':
        cmd_runtime(resolve_targets(args.target))
    elif args.command == 'model':
        cmd_model()
    elif args.command == 'frontend':
        cmd_frontend(isolated=args.isolated)
    elif args.command == 'run':
        cmd_run([a for a in args.args if a != '--'])
    elif args.command == 'dist':
        cmd_dist(resolve_targets(args.target), skip_frontend=args.skip_frontend,
                 keep_dirs=args.keep_dirs)


if __name__ == '__main__':
    main()
