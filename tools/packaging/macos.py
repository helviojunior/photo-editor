"""``PhotoEditor.app`` e o ``.dmg`` de instalacao — montados no Linux.

O ``.app`` e so a pasta do pacote reorganizada no formato de bundle:

    PhotoEditor.app/Contents/
        Info.plist
        MacOS/PhotoEditor        lancador (sh) -> exec do Python embarcado
        Resources/PhotoEditor.icns
        Resources/runtime/       CPython + dependencias
        Resources/app/           backend, desktop, frontend, modelo

O lancador faz ``exec``: o processo continua sendo o do bundle (mesmo PID), e
o Dock mostra nome e icone do PhotoEditor, nao "python3".

O ``.dmg`` segue o caminho que a Mozilla usa para o Firefox (sem ``hdiutil``,
que so existe no macOS): ``mkfs.hfsplus`` cria o volume HFS+, o ``hfsplus`` do
libdmg-hfsplus o preenche sem montar nada, e o ``dmg`` comprime (UDZO). A
janela que abre ao montar — fundo, tamanho, posicao dos icones — e o
``.DS_Store`` da raiz, escrito aqui com ``ds_store`` + ``mac_alias``.
"""
import datetime
import os
import plistlib
import shutil
import struct
import subprocess
from pathlib import Path

from tools.packaging import art

APP_NAME = 'PhotoEditor'
BUNDLE_ID = 'br.com.photoeditor.desktop'
# Piso do pacote: o onnxruntime so publica wheel para macOS 14+ (tools/build.py).
MIN_MACOS = '14.0'
VOLUME_NAME = APP_NAME

HFSPLUS = os.environ.get('HFSPLUS', 'hfsplus')
DMG = os.environ.get('DMG_TOOL', 'dmg')
MKFS_HFSPLUS = os.environ.get('MKFS_HFSPLUS', 'mkfs.hfsplus')

# Lancador do bundle: exec (mesmo PID) para o Dock tratar o Python como o app.
# -E/-s: nada do Python do sistema vaza para dentro do app.
#
# ``arch -<arquitetura do pacote>`` no Apple Silicon: o executavel do bundle
# e um SCRIPT, e sem um Mach-O para olhar o LaunchServices escolhe a
# arquitetura as cegas. O Python (de uma arquitetura so) roda na dele mesmo
# assim, mas a preferencia errada fica no processo — e o QtWebEngineProcess,
# que e universal2, sobe na outra e nunca desenha a pagina (janela branca).
# O pacote arm64 forca arm64; o Intel forca x86_64 (Rosetta) de ponta a ponta.
# hw.optional.arm64 responde 1 mesmo de dentro do Rosetta (o ``uname -m`` nao).
LAUNCHER = """#!/bin/sh
RES="$(cd "$(dirname "$0")/../Resources" && pwd)"
PY="$RES/runtime/bin/python3"
if [ "$(/usr/sbin/sysctl -n hw.optional.arm64 2>/dev/null)" = "1" ]; then
    exec /usr/bin/arch -{arch} "$PY" -E -s "$RES/app/desktop" "$@"
fi
exec "$PY" -E -s "$RES/app/desktop" "$@"
"""

# Arquitetura do alvo (tools/build.py) -> nome que o macOS usa.
MACHO_ARCH = {'arm64': 'arm64', 'x64': 'x86_64'}

# HFS+: o cabecalho do volume fica em 1024 bytes do inicio (e uma copia a
# 1024 bytes do fim). Campos usados — ver TN1150 (HFS Plus Volume Format).
_HEADER_OFFSET = 1024
_CREATE_DATE = 16          # UInt32, segundos desde 1904 (hora local)
_NEXT_CATALOG_ID = 64      # UInt32, proximo CNID livre
_FINDER_INFO = 80          # 8 x UInt32; [2] = pasta aberta ao montar
_ROOT_CNID = 2
# O mac_alias so aceita data com fuso. O mkfs.hfsplus grava a hora local do
# container, que e UTC.
_MAC_EPOCH = datetime.datetime(1904, 1, 1, tzinfo=datetime.timezone.utc)


def build_app(package_dir: Path, out_dir: Path, version: str, arch: str = 'arm64') -> Path:
    """``out_dir/PhotoEditor.app`` a partir da pasta do pacote (runtime/ + app/).

    ``arch`` = arquitetura do runtime do pacote (``arm64`` | ``x64``).
    """
    macho = MACHO_ARCH[arch]
    app = out_dir / f'{APP_NAME}.app'
    shutil.rmtree(app, ignore_errors=True)
    contents = app / 'Contents'
    resources = contents / 'Resources'
    (contents / 'MacOS').mkdir(parents=True)
    resources.mkdir()

    shutil.copytree(package_dir / 'runtime', resources / 'runtime', symlinks=True)
    shutil.copytree(package_dir / 'app', resources / 'app', symlinks=True)
    art.write_icns(resources / f'{APP_NAME}.icns')

    launcher = contents / 'MacOS' / APP_NAME
    launcher.write_text(LAUNCHER.replace('{arch}', macho))
    launcher.chmod(0o755)

    with open(contents / 'Info.plist', 'wb') as f:
        plistlib.dump({
            'CFBundleName': APP_NAME,
            'CFBundleDisplayName': APP_NAME,
            'CFBundleIdentifier': BUNDLE_ID,
            'CFBundleExecutable': APP_NAME,
            'CFBundleIconFile': APP_NAME,
            'CFBundlePackageType': 'APPL',
            'CFBundleShortVersionString': version,
            'CFBundleVersion': version,
            'CFBundleInfoDictionaryVersion': '6.0',
            'CFBundleDevelopmentRegion': 'en',
            'CFBundleLocalizations': ['en', 'pt-BR'],
            'LSMinimumSystemVersion': MIN_MACOS,
            # So a arquitetura do runtime (ver LAUNCHER). O pacote arm64 nunca
            # abre sob Rosetta; o Intel, no Apple Silicon, sempre.
            'LSArchitecturePriority': [macho],
            **({'LSRequiresNativeExecution': True} if arch == 'arm64' else {}),
            'LSApplicationCategoryType': 'public.app-category.photography',
            'NSHighResolutionCapable': True,
            'NSSupportsAutomaticGraphicsSwitching': True,
            'NSRequiresAquaSystemAppearance': False,
            'NSHumanReadableCopyright': 'BSD 2-Clause — Helvio Junior / PhotoE',
        }, f)
    (contents / 'PkgInfo').write_text('APPL????')
    return app


def _read_u32(image: Path, offset: int) -> int:
    with open(image, 'rb') as f:
        f.seek(_HEADER_OFFSET + offset)
        return struct.unpack('>I', f.read(4))[0]


def _write_u32(image: Path, offset: int, value: int):
    """Grava no cabecalho principal e na copia do fim do volume."""
    size = image.stat().st_size
    with open(image, 'r+b') as f:
        for base in (_HEADER_OFFSET, size - _HEADER_OFFSET):
            f.seek(base + offset)
            f.write(struct.pack('>I', value))


def _hfs(image: Path, *args):
    subprocess.run([HFSPLUS, '--symlinks', 'clone_link', '--special-modes', 'no',
                    str(image), *[str(a) for a in args]],
                   check=True, stdout=subprocess.DEVNULL)


# Mach-O: magia (64 bits, little-endian) e "fat"; filetype 2 = MH_EXECUTE.
_MACHO_64 = b'\xcf\xfa\xed\xfe'
_MACHO_FAT = b'\xca\xfe\xba\xbe'


def _needs_exec_bit(path: Path) -> bool:
    """Executavel Mach-O ou script — o hfsplus cria tudo como 0644.

    Biblioteca (.dylib/.so) fica de fora: o dyld carrega sem o bit, e marcar
    as milhares do runtime seriam milhares de chamadas ao hfsplus.
    """
    if path.is_symlink() or not os.access(path, os.X_OK):
        return False
    with open(path, 'rb') as f:
        head = f.read(16)
    if head[:2] == b'#!':
        return True
    if head[:4] == _MACHO_64:
        return struct.unpack('<I', head[12:16])[0] == 2
    if head[:4] == _MACHO_FAT:
        return not path.name.endswith(('.dylib', '.so'))
    return False


def _executables(root: Path):
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            path = Path(dirpath) / name
            if _needs_exec_bit(path):
                yield path


def _write_ds_store(target: Path, volume_created: datetime.datetime,
                    background_cnid: int, app_name: str):
    """A janela do volume: sem barras, do tamanho do fundo, icones no lugar."""
    from ds_store import DSStore
    from mac_alias import Alias, TargetInfo, VolumeInfo, ALIAS_FIXED_DISK, ALIAS_KIND_FILE

    width, height = art.DMG_LAYOUT['window']
    # Alias do fundo montado a mao (o Alias.for_file so roda num Mac com o
    # volume montado). O Finder o resolve pelo nome do volume + caminho; os
    # CNIDs e a data batem com o volume de verdade de qualquer forma.
    alias = Alias(
        volume=VolumeInfo(VOLUME_NAME.encode(), volume_created, b'H+', ALIAS_FIXED_DISK, 0,
                          b'\0\0', posix_path=f'/Volumes/{VOLUME_NAME}'.encode()),
        target=TargetInfo(ALIAS_KIND_FILE, b'.background.tiff', _ROOT_CNID, background_cnid,
                          volume_created, b'\0\0\0\0', b'\0\0\0\0',
                          folder_name=VOLUME_NAME.encode(), cnid_path=[],
                          carbon_path=f'{VOLUME_NAME}:.background.tiff'.encode(),
                          posix_path=b'/.background.tiff'),
    )
    with DSStore.open(str(target), 'w+') as d:
        d['.']['vSrn'] = ('long', 1)
        d['.']['bwsp'] = {
            'ShowStatusBar': False,
            'WindowBounds': f'{{{{200, 120}}, {{{width}, {height}}}}}',
            'ContainerShowSidebar': False,
            'PreviewPaneVisibility': False,
            'SidebarWidth': 0,
            'ShowTabView': False,
            'ShowToolbar': False,
            'ShowPathbar': False,
            'ShowSidebar': False,
        }
        d['.']['icvp'] = {
            'viewOptionsVersion': 1,
            'backgroundType': 2,
            'backgroundImageAlias': alias.to_bytes(),
            'backgroundColorRed': 1.0,
            'backgroundColorGreen': 1.0,
            'backgroundColorBlue': 1.0,
            'gridOffsetX': 0.0,
            'gridOffsetY': 0.0,
            'gridSpacing': 100.0,
            'arrangeBy': 'none',
            'showIconPreview': False,
            'showItemInfo': False,
            'labelOnBottom': True,
            'textSize': float(art.DMG_LAYOUT['text_size']),
            'iconSize': float(art.DMG_LAYOUT['icon_size']),
            'scrollPositionX': 0.0,
            'scrollPositionY': 0.0,
        }
        d['.']['icvl'] = (b'type', b'icnv')
        icons = art.DMG_LAYOUT['icons']
        d[app_name]['Iloc'] = icons['app']
        d['Applications']['Iloc'] = icons['applications']


def build_dmg(app: Path, out_dmg: Path, work: Path) -> Path:
    """``out_dmg`` com o ``app`` e o atalho de Aplicativos, na janela desenhada."""
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    stage = work / 'stage'
    stage.mkdir()
    # O .app entra por ultimo, via addall; mover (mesmo disco) evita copiar 1,5 GB.
    os.replace(app, stage / app.name)
    art.write_icns(stage / '.VolumeIcon.icns')
    background = art.write_dmg_background(work / 'background.tiff')

    # Volume com folga de 20% + 64 MB: o catalogo e os extents ocupam espaco.
    payload = sum(p.stat().st_size for p in stage.rglob('*') if p.is_file() and not p.is_symlink())
    size = int(payload * 1.2) + 64 * 1024 * 1024
    size -= size % 4096
    image = work / 'volume.hfs'
    with open(image, 'wb') as f:
        f.truncate(size)
    subprocess.run([MKFS_HFSPLUS, '-v', VOLUME_NAME, str(image)], check=True,
                   stdout=subprocess.DEVNULL)

    created = _MAC_EPOCH + datetime.timedelta(seconds=_read_u32(image, _CREATE_DATE))
    # O fundo entra PRIMEIRO: o CNID dele e o proximo livre do volume recem
    # criado, e o alias no .DS_Store precisa dele.
    background_cnid = _read_u32(image, _NEXT_CATALOG_ID)
    _hfs(image, 'add', background, '/.background.tiff')
    assert _read_u32(image, _NEXT_CATALOG_ID) == background_cnid + 1, 'unexpected CNID allocation'

    ds_store = work / 'DS_Store'
    _write_ds_store(ds_store, created, background_cnid, app.name)
    _hfs(image, 'add', ds_store, '/.DS_Store')
    _hfs(image, 'symlink', '/Applications', '/Applications')

    _hfs(image, 'addall', stage, '/')
    for path in _executables(stage):
        _hfs(image, 'chmod', '755', '/' + path.relative_to(stage).as_posix())
    # C = kHasCustomIcon na raiz: o Finder usa o .VolumeIcon.icns no volume.
    _hfs(image, 'attr', '/', 'C')

    # finderInfo[2] = pasta que o Finder abre ao montar (o "bless --openfolder").
    _write_u32(image, _FINDER_INFO + 8, _ROOT_CNID)

    out_dmg.parent.mkdir(parents=True, exist_ok=True)
    out_dmg.unlink(missing_ok=True)
    subprocess.run([DMG, 'build', str(image), str(out_dmg)], check=True,
                   stdout=subprocess.DEVNULL)
    shutil.rmtree(work)
    return out_dmg
