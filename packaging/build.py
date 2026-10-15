#!/usr/bin/env python3
"""Utilitarios do build do PhotoEditor (C++/Qt): versao, modelo, runtime do
helper do Instagram e arte dos instaladores.

So usa a biblioteca padrao (roda com qualquer python3 >= 3.9). Quem compila e
empacota sao os builders em Docker de ``packaging/<plataforma>/`` (ver o
Makefile da raiz, `make help`); este script so faz o que e comum a todos:

    version [--refresh]          versao do build (Releases do GitHub) -> VERSION
    package-name --target T      nome do pacote: PhotoEditor-1.2.3-<T> /
                                 PhotoEditor-dev-v1.2.3-<T>
    model                        modelo SAM 2.1 (camadas) em .cache/models/
    instagram-runtime --target T --out DIR
                                 CPython embarcado + wheels do helper do
                                 Instagram (src/instagram/requirements.txt) em DIR
    art [--out DIR]              icones (.icns/.ico/.png) desenhados do logo
    vcruntime --target T --out DIR
                                 DLLs do runtime do Visual C++ (msvcp140...) que
                                 o ONNX Runtime pede, ao lado do PhotoEditor.exe
    build-info --target T --out F
                                 build-info.json que vai dentro do pacote
    checksums FILE...            FILE.sha256 ao lado de cada arquivo

Alvos: macos-arm64, macos-x64, windows-x64, windows-arm64, linux-x64, linux-arm64.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / '.cache'
MODELS = CACHE / 'models'
INSTAGRAM = ROOT / 'src' / 'instagram'

APP_NAME = 'PhotoEditor'

# CPython do helper do Instagram (https://github.com/astral-sh/python-build-standalone).
# Fixo: o mesmo build sempre empacota o mesmo interpretador (regra 18).
PBS_RELEASE = '20260924'
PYTHON_VERSION = '3.12.14'
PYTHON_ABI = 'cp312'

# Piso de SO das wheels do helper. A tag e o MAXIMO aceito (as anteriores
# compativeis tambem entram, ver pip_platforms):
#   macOS 13 (o MACOS_MIN do builder de macOS) · glibc 2.39 no Linux (o
#   Ubuntu 24.04 do builder Linux, piso do app inteiro) · Windows 10/11.
TARGETS = {
    'macos-arm64': {'pbs': 'aarch64-apple-darwin', 'platform': 'macosx_13_0_arm64', 'os': 'macos'},
    'macos-x64': {'pbs': 'x86_64-apple-darwin', 'platform': 'macosx_13_0_x86_64', 'os': 'macos'},
    'linux-x64': {'pbs': 'x86_64-unknown-linux-gnu', 'platform': 'manylinux_2_39_x86_64', 'os': 'linux'},
    'linux-arm64': {'pbs': 'aarch64-unknown-linux-gnu', 'platform': 'manylinux_2_39_aarch64', 'os': 'linux'},
    'windows-x64': {'pbs': 'x86_64-pc-windows-msvc', 'platform': 'win_amd64', 'os': 'windows'},
    'windows-arm64': {'pbs': 'aarch64-pc-windows-msvc', 'platform': 'win_arm64', 'os': 'windows'},
}

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


# ---------------------------------------------------------------------------
# Versao: quem manda e o GitHub (regra 17)
# ---------------------------------------------------------------------------
# A versao NAO e versionada no repositorio. Ela sai das Releases do GitHub:
#
#   canal    de onde vem o numero                  exibido / nome do pacote
#   release  a tag da Release (CI)                 1.2.3 / PhotoEditor-1.2.3-<alvo>
#   dev      a ultima Release (build local)        1.2.3-dev+<commit>  / PhotoEditor-dev-v1.2.3-<alvo>
#
# PHOTOEDITOR_VERSION (X.Y.Z) e PHOTOEDITOR_CHANNEL sobrescrevem (a CI usa).
# Sem Release publicada ainda, a versao e 0.0.0. O .app e o .msi recebem o
# numero puro X.Y.Z (o MSI so aceita numeros); o sufixo e so para exibicao.
# O arquivo VERSION da raiz e GERADO aqui (ignorado pelo git): e de onde o
# CMakeLists.txt le a versao (rotulo para a interface, numero para o projeto).

GITHUB_REPO = 'helviojunior/photo-editor'
CHANNELS = ('release', 'dev')
LATEST_RELEASE_CACHE = CACHE / 'latest-release'
# Versao encontrada vale 24 h. "Nenhuma Release" (0.0.0) so 1 h: e o estado
# que muda quando se publica a primeira, e 24 h presas em 0.0.0 atrapalham.
LATEST_RELEASE_TTL = 24 * 3600
NO_RELEASE_TTL = 3600
_VERSION_RE = re.compile(r'^v?(\d{1,3})\.(\d{1,3})\.(\d{1,3})$')


def _parse_version(raw: str):
    """``v1.2.3``/``1.2.3`` -> ``1.2.3``, ou None fora do formato do MSI."""
    m = _VERSION_RE.match((raw or '').strip())
    if not m or int(m.group(1)) > 255:
        return None
    return '.'.join(str(int(g)) for g in m.groups())


def latest_release(refresh: bool = False) -> str:
    """Numero da ultima Release publicada no GitHub.

    A consulta a API fica em cache (``.cache/latest-release``) por 24 h — 1 h
    se ainda nao ha Release. ``refresh`` (ou PHOTOEDITOR_REFRESH_VERSION=1)
    ignora o cache. Sem rede, usa o ultimo valor em cache; sem Release, 0.0.0.
    """
    refresh = refresh or os.environ.get('PHOTOEDITOR_REFRESH_VERSION') == '1'
    cached = None
    if LATEST_RELEASE_CACHE.is_file():
        cached = _parse_version(LATEST_RELEASE_CACHE.read_text())
        ttl = NO_RELEASE_TTL if cached == '0.0.0' else LATEST_RELEASE_TTL
        age = time.time() - LATEST_RELEASE_CACHE.stat().st_mtime
        if cached and not refresh and age < ttl:
            return cached
    headers = {'Accept': 'application/vnd.github+json'}
    if os.environ.get('GITHUB_TOKEN'):
        headers['Authorization'] = f'Bearer {os.environ["GITHUB_TOKEN"]}'
    req = urllib.request.Request(
        f'https://api.github.com/repos/{GITHUB_REPO}/releases/latest', headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            found = _parse_version(json.load(r).get('tag_name', '')) or '0.0.0'
    except urllib.error.HTTPError as exc:
        if exc.code != 404:            # 404 = nenhuma Release publicada ainda
            return cached or '0.0.0'
        found = '0.0.0'
    except OSError:
        return cached or '0.0.0'
    LATEST_RELEASE_CACHE.parent.mkdir(parents=True, exist_ok=True)
    LATEST_RELEASE_CACHE.write_text(found)
    return found


def version() -> str:
    """Numero X.Y.Z do build (ver a tabela acima)."""
    forced = os.environ.get('PHOTOEDITOR_VERSION', '').strip()
    if forced:
        parsed = _parse_version(forced)
        if not parsed:
            sys.exit(f'PHOTOEDITOR_VERSION={forced!r} is not X.Y.Z (X <= 255, Y and Z <= 999).')
        return parsed
    return latest_release()


def channel() -> str:
    value = os.environ.get('PHOTOEDITOR_CHANNEL', '').strip() or 'dev'
    if value not in CHANNELS:
        sys.exit(f'PHOTOEDITOR_CHANNEL={value!r}: choose from {", ".join(CHANNELS)}.')
    return value


def git_hash() -> str:
    """Commit atual (7 caracteres), lido de .git sem precisar do git — os
    containers dos builders nao o tem. PHOTOEDITOR_GIT_HASH sobrescreve."""
    forced = os.environ.get('PHOTOEDITOR_GIT_HASH', '').strip()
    if forced:
        return forced[:7]
    git = ROOT / '.git'
    try:
        head = (git / 'HEAD').read_text().strip()
        if not head.startswith('ref: '):
            return head[:7]
        ref = head[5:]
        if (git / ref).is_file():
            return (git / ref).read_text().strip()[:7]
        for line in (git / 'packed-refs').read_text().splitlines():
            if line.endswith(' ' + ref):
                return line.split()[0][:7]
    except OSError:
        pass
    return ''


def version_label() -> str:
    """O que o app exibe: 1.2.3 na Release; 1.2.3-dev+abc1234 fora dela."""
    ver, chan = version(), channel()
    if chan == 'release':
        return ver
    commit = git_hash()
    return f'{ver}-{chan}' + (f'+{commit}' if commit else '')


def package_prefix() -> str:
    """Inicio do nome dos pacotes: PhotoEditor-1.2.3 / PhotoEditor-dev-v1.2.3."""
    ver, chan = version(), channel()
    return f'{APP_NAME}-{ver}' if chan == 'release' else f'{APP_NAME}-{chan}-v{ver}'


def msi_version(ver: str) -> str:
    """X.Y.Z do projeto -> ProductVersion do MSI.

    O MSI aceita no maximo 255.255.65535, e a versao do projeto (a tag da
    Release, regra 17) vai ate 255.999.999. Os dois numeros de baixo viram um
    contador so (Y*1000 + Z, ate 999999) repartido em 16 bits: a ordem se
    mantem, que e o que o MajorUpgrade compara.
    """
    major, minor, patch = (int(p) for p in ver.split('.'))
    counter = minor * 1000 + patch
    return f'{major}.{counter // 65536}.{counter % 65536}'


def write_version_file() -> str:
    """Grava o VERSION da raiz (gerado, fora do git) e devolve o rotulo.

    So reescreve quando muda: o CMake reconfigura quando o VERSION muda de
    data, e regravar o mesmo texto recompilaria tudo a cada `make`.
    """
    label = version_label()
    path = ROOT / 'VERSION'
    if not path.is_file() or path.read_text().strip() != label:
        path.write_text(label + '\n')
    return label


def host_target() -> str:
    system = platform.system()
    machine = platform.machine().lower()
    arch = 'arm64' if machine in ('arm64', 'aarch64') else 'x64'
    name = {'Darwin': 'macos', 'Linux': 'linux', 'Windows': 'windows'}.get(system)
    if not name:
        sys.exit(f'Unsupported host system: {system}')
    return f'{name}-{arch}'


def check_target(name: str) -> str:
    if name == 'host':
        return host_target()
    if name not in TARGETS:
        sys.exit(f'Unknown target: {name} (choose from {", ".join(TARGETS)}, host)')
    return name


# ---------------------------------------------------------------------------
# Downloads
# ---------------------------------------------------------------------------

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
# Modelo
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


# ---------------------------------------------------------------------------
# Runtime do helper do Instagram: CPython embarcado + wheels do alvo
# ---------------------------------------------------------------------------
# O app (C++) chama ``instagram/python/<python> -E -s instagram/photoe_instagram.py``.
# O runtime e montado SEMPRE por cross-install: o CPython pronto do alvo e
# extraido e o pip DO BUILDER baixa as wheels do alvo (``--platform``) direto
# no site-packages dele. Nada compila; um container Linux monta o runtime de
# qualquer SO/arquitetura.

def pip_platforms(target: str) -> list:
    """Tags de plataforma para o ``pip --platform`` do alvo.

    O pip expande sozinho a tag do macOS para as versoes anteriores, mas NAO
    as ``manylinux_2_N``: sem a lista explicita, wheel publicada so como
    ``manylinux_2_28`` nao serviria para um alvo ``manylinux_2_39``.
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


def site_packages(target: str, python_dir: Path) -> Path:
    if TARGETS[target]['os'] == 'windows':
        return python_dir / 'Lib' / 'site-packages'
    return python_dir / 'lib' / f'python{PYTHON_VERSION.rsplit(".", 1)[0]}' / 'site-packages'


def builder_pip() -> list:
    """O pip do builder: o do proprio Python, ou um venv em .cache/ (o Python
    do sistema do Ubuntu e "externally managed" e as vezes vem sem pip)."""
    if subprocess.run([sys.executable, '-m', 'pip', '--version'],
                      capture_output=True).returncode == 0 and sys.prefix != sys.base_prefix:
        return [sys.executable, '-m', 'pip']
    venv = CACHE / 'pip-venv'
    py = venv / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python3')
    if not py.is_file():
        run([sys.executable, '-m', 'venv', venv])
        run([py, '-m', 'pip', 'install', '--disable-pip-version-check', '-q', '--upgrade', 'pip'])
    return [py, '-m', 'pip']


def instagram_stamp(target: str) -> str:
    h = hashlib.sha256(f'{PBS_RELEASE}:{PYTHON_VERSION}:{target}:{TARGETS[target]["platform"]}'.encode())
    h.update((INSTAGRAM / 'requirements.txt').read_bytes())
    h.update(json.dumps(RUNTIME_PRUNE, sort_keys=True).encode())
    return h.hexdigest()


def cmd_instagram_runtime(target: str, out: Path):
    """``out/python``: CPython do alvo com as dependencias do helper.

    O ``photoe_instagram.py`` em si e instalado pelo CMake (Packaging.cmake);
    aqui so o interpretador, que pesa e depende do alvo.
    """
    requirements = INSTAGRAM / 'requirements.txt'
    if not requirements.is_file():
        sys.exit(f'{requirements.relative_to(ROOT)} not found')
    out = out.resolve()
    python_dir = out / 'python'
    stamp_file = out / '.stamp'
    stamp = instagram_stamp(target)
    if stamp_file.is_file() and stamp_file.read_text() == stamp and python_dir.is_dir():
        log(f'Instagram runtime for {target} is up to date ({out})')
        return out

    asset = pbs_asset(target)
    tarball = download(
        f'https://github.com/astral-sh/python-build-standalone/releases/download/'
        f'{PBS_RELEASE}/{asset.replace("+", "%2B")}',
        CACHE / 'pbs' / asset, pbs_checksums().get(asset))

    log(f'Extracting CPython {PYTHON_VERSION} for {target}')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    with tarfile.open(tarball) as tar:
        # Sem share/terminfo (so o curses usa): no Linux ele tem nomes que so
        # diferem por maiuscula, e num disco que ignora a caixa (macOS, onde o
        # repositorio costuma estar montado no builder) viram laco de symlink.
        members = [m for m in tar.getmembers() if '/share/terminfo/' not in m.name]
        if sys.version_info >= (3, 12):
            tar.extractall(out, members=members, filter='tar')
        else:
            tar.extractall(out, members=members)
    prune_runtime(target, python_dir)

    log(f'Installing the helper wheels for {target} ({TARGETS[target]["platform"]})')
    run([*builder_pip(), 'install', '--disable-pip-version-check',
         '--no-warn-script-location', '--no-compile', '--upgrade',
         '--target', site_packages(target, python_dir),
         *[a for tag in pip_platforms(target) for a in ('--platform', tag)],
         '--python-version', PYTHON_VERSION.rsplit('.', 1)[0],
         '--implementation', 'cp', '--abi', PYTHON_ABI,
         '--only-binary=:all:', '-r', requirements],
        env=dict(os.environ, PIP_CACHE_DIR=str(CACHE / 'pip')))
    precompile(python_dir)
    stamp_file.write_text(stamp)
    log(f'Instagram runtime ready in {out}')
    return out


# O que o helper nao usa: Tcl/Tk (tkinter, IDLE), pip/ensurepip, headers e
# o necessario para compilar extensoes. Caminhos relativos a python/ (glob).
RUNTIME_PRUNE = {
    'unix': ['include', 'share', 'lib/pkgconfig', 'lib/itcl*', 'lib/tcl*', 'lib/tk*', 'lib/thread*',
             'lib/libtcl*', 'lib/libtk*', 'bin/idle*', 'bin/pip*', 'bin/2to3*', 'bin/pydoc*',
             'bin/python*-config',
             'lib/python3.*/config-3.*', 'lib/python3.*/lib-dynload/_tkinter*'],
    'windows': ['include', 'libs', 'tcl', 'Scripts', 'DLLs/_tkinter.pyd', 'DLLs/tcl*.dll',
                'DLLs/tk*.dll'],
    'stdlib': ['tkinter', 'idlelib', 'turtledemo', 'ensurepip', 'test', 'lib2to3', 'pydoc_data',
               'site-packages/pip', 'site-packages/pip-*'],
}


def prune_runtime(target: str, python_dir: Path):
    windows = TARGETS[target]['os'] == 'windows'
    stdlib = python_dir / ('Lib' if windows else f'lib/python{PYTHON_VERSION.rsplit(".", 1)[0]}')
    paths = [p for pattern in RUNTIME_PRUNE['windows' if windows else 'unix'] for p in python_dir.glob(pattern)]
    paths += [p for pattern in RUNTIME_PRUNE['stdlib'] for p in stdlib.glob(pattern)]
    for path in sorted(set(paths)):
        if not path.exists() and not path.is_symlink():
            continue                    # ja saiu junto com a pasta de cima
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        else:
            path.unlink()


def precompile(folder: Path):
    """``.pyc`` de tudo, ja no runtime.

    Instalado, o app mora onde nao se escreve (``Program Files``, dentro do
    ``.app``): sem ``.pyc`` pronto o Python recompilaria tudo a cada chamada.
    ``unchecked-hash``: vale sem conferir a data do fonte, que o instalador
    nao preserva. So com o mesmo Python do runtime (3.12, o dos builders) — o
    formato do ``.pyc`` muda entre versoes.
    """
    if sys.version_info[:2] != tuple(int(p) for p in PYTHON_VERSION.split('.')[:2]):
        log(f'Skipping .pyc precompilation (builder Python is not {PYTHON_VERSION})')
        return
    import compileall
    import py_compile
    log('Precompiling .pyc')
    compileall.compile_dir(
        str(folder), quiet=2, workers=0,
        invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)


# ---------------------------------------------------------------------------
# Runtime do Visual C++ (Windows)
# ---------------------------------------------------------------------------
# O onnxruntime.dll oficial e compilado com MSVC e importa msvcp140.dll,
# msvcp140_1.dll, vcruntime140*.dll — que nem todo Windows tem. Os arquivos
# redistribuiveis da Microsoft vem do pacote ``msvc-runtime`` do PyPI (wheel
# por arquitetura, versao fixa), baixado e conferido pelo sha256 do PyPI.
VCRUNTIME_VERSION = '14.44.35112'


def cmd_vcruntime(target: str, out: Path):
    import zipfile
    if TARGETS[target]['os'] != 'windows':
        sys.exit(f'vcruntime: {target} is not a Windows target')
    platform_tag = TARGETS[target]['platform']
    with urllib.request.urlopen(f'https://pypi.org/pypi/msvc-runtime/{VCRUNTIME_VERSION}/json',
                                timeout=30) as r:
        files = json.load(r)['urls']
    wheel = next(f for f in files if f['filename'].endswith(f'-{PYTHON_ABI}-{PYTHON_ABI}-{platform_tag}.whl'))
    path = download(wheel['url'], CACHE / 'vcruntime' / wheel['filename'], wheel['digests']['sha256'])
    out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path) as zf:
        for name in zf.namelist():
            if name.lower().endswith('.dll'):
                (out / Path(name).name).write_bytes(zf.read(name))
                log(f'VC++ runtime {Path(name).name}')


# ---------------------------------------------------------------------------
# Arte, build-info, checksums
# ---------------------------------------------------------------------------

def cmd_art(out: Path):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    try:
        import art  # noqa: E402  (packaging/art.py, precisa do Pillow)
    except ImportError as exc:
        sys.exit(f'packaging/art.py needs Pillow ({exc}); run it inside a builder (make art).')
    for path in art.write_icons(out):
        log(f'Icon {path}')


def cmd_build_info(target: str, out: Path):
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({
        'version': version(), 'label': version_label(), 'channel': channel(),
        'commit': git_hash(), 'target': target,
        'python': PYTHON_VERSION, 'pbs_release': PBS_RELEASE,
        'model': f'{SEGMENT_MODEL["repo"]}@{SEGMENT_MODEL["revision"]}',
        'built_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
    }, indent=2) + '\n')


def cmd_checksums(files):
    for name in files:
        path = Path(name)
        line = f'{sha256(path)}  {path.name}\n'
        Path(f'{path}.sha256').write_text(line)
        print(line, end='')


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('version', help='print (and write to VERSION) the version label')
    p.add_argument('--refresh', action='store_true',
                   help='ask the GitHub API now, ignoring the 24 h cache')
    p.add_argument('--number', action='store_true', help='print only X.Y.Z')
    p.add_argument('--msi', action='store_true', help='print the MSI ProductVersion')
    p = sub.add_parser('package-name')
    p.add_argument('--target', required=True)
    sub.add_parser('model')
    p = sub.add_parser('instagram-runtime')
    p.add_argument('--target', required=True)
    p.add_argument('--out', required=True, type=Path)
    p = sub.add_parser('art')
    p.add_argument('--out', type=Path, default=CACHE / 'art')
    p = sub.add_parser('vcruntime')
    p.add_argument('--target', required=True)
    p.add_argument('--out', required=True, type=Path)
    p = sub.add_parser('build-info')
    p.add_argument('--target', required=True)
    p.add_argument('--out', required=True, type=Path)
    p = sub.add_parser('checksums')
    p.add_argument('files', nargs='+')
    args = parser.parse_args()

    if args.command == 'version':
        if args.refresh:
            latest_release(refresh=True)
        label = write_version_file()
        print(msi_version(version()) if args.msi else version() if args.number else label)
    elif args.command == 'package-name':
        print(f'{package_prefix()}-{check_target(args.target)}')
    elif args.command == 'model':
        cmd_model()
    elif args.command == 'instagram-runtime':
        cmd_instagram_runtime(check_target(args.target), args.out)
    elif args.command == 'art':
        cmd_art(args.out)
    elif args.command == 'vcruntime':
        cmd_vcruntime(check_target(args.target), args.out)
    elif args.command == 'build-info':
        cmd_build_info(check_target(args.target), args.out)
    elif args.command == 'checksums':
        cmd_checksums(args.files)


if __name__ == '__main__':
    main()
