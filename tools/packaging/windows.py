"""Instalador ``.msi`` do Windows — gerado no Linux pelo wixl (msitools).

O wixl compila um subconjunto do WiX 3 sem precisar de Windows. O
``wixl-heat`` transforma a arvore do pacote (runtime/ + app/) em componentes;
o ``PhotoEditor.wxs`` montado aqui poe por cima o produto, os atalhos e o
upgrade:

* instala por maquina em ``C:\\Program Files\\PhotoEditor`` (pede elevacao);
* atalhos no Menu Iniciar e na Area de Trabalho apontando para o
  ``pythonw.exe`` embarcado (sem janela de console) com o app como argumento;
* aparece em "Aplicativos instalados" com icone, versao e link;
* ``MajorUpgrade``: instalar uma versao nova remove a anterior; instalar uma
  mais velha por cima e recusado.

**Nunca troque os GUIDs abaixo.** O ``UpgradeCode`` e o que faz o Windows
reconhecer as versoes como o mesmo produto; os dos componentes dos atalhos
identificam o que desinstalar.
"""
import os
import subprocess
from pathlib import Path
from xml.sax.saxutils import quoteattr

from tools.packaging import art

APP_NAME = 'PhotoEditor'
MANUFACTURER = 'PhotoE'
ABOUT_URL = 'https://photoe.com.br/'
UPGRADE_CODE = 'D375EF80-A583-40B6-AD1E-6BA0918F4C92'
START_MENU_COMPONENT = '9AF67A2C-D8E8-4FF4-AB26-9AE5E6E35601'
DESKTOP_COMPONENT = '4A679994-4CE3-45BD-95EB-F01926C5F78C'

WIXL = os.environ.get('WIXL', 'wixl')
WIXL_HEAT = os.environ.get('WIXL_HEAT', 'wixl-heat')


def msi_version(version: str) -> str:
    """X.Y.Z do projeto -> ProductVersion do MSI.

    O MSI aceita no maximo 255.255.65535, e a versao do projeto (a tag da
    Release, regra 17) vai ate 255.999.999. Os dois numeros de baixo viram um
    contador so
    (Y*1000 + Z, ate 999999) repartido em 16 bits: a ordem se mantem, que e o
    que o MajorUpgrade compara.
    """
    major, minor, patch = (int(p) for p in version.split('.'))
    counter = minor * 1000 + patch
    return f'{major}.{counter // 65536}.{counter % 65536}'


# Sem Platform no <Package>: o wixl nao le esse atributo (reclama e ignora);
# a plataforma x64 vem do ``--arch x64`` na linha de comando.
def _main_wxs(version: str, icon: Path) -> str:
    target = '[INSTALLDIR]runtime\\pythonw.exe'
    arguments = '-E -s "[INSTALLDIR]app\\desktop"'

    def shortcut(component_id, guid, directory, name):
        return f"""
    <DirectoryRef Id="{directory}">
      <Component Id="{component_id}" Guid="{guid}">
        <Shortcut Id="{component_id}Link" Name="{APP_NAME}" Description="{APP_NAME}"
                  Target={quoteattr(target)} Arguments={quoteattr(arguments)}
                  WorkingDirectory="INSTALLDIR" Icon="AppIcon.ico" />
        <RegistryValue Root="HKLM" Key="Software\\{MANUFACTURER}\\{APP_NAME}" Name="{name}"
                       Type="integer" Value="1" KeyPath="yes" />
      </Component>
    </DirectoryRef>"""

    return f"""<?xml version="1.0" encoding="utf-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="*" Name="{APP_NAME}" Language="1033" Version="{msi_version(version)}"
           Manufacturer="{MANUFACTURER}" UpgradeCode="{UPGRADE_CODE}">
    <Package InstallerVersion="500" Compressed="yes" InstallScope="perMachine"
             Description="{APP_NAME} {version}"
             Comments="Event photo editor" Manufacturer="{MANUFACTURER}" />
    <Media Id="1" Cabinet="app.cab" EmbedCab="yes" />
    <MajorUpgrade DowngradeErrorMessage="A newer version of {APP_NAME} is already installed." />

    <Icon Id="AppIcon.ico" SourceFile={quoteattr(str(icon))} />
    <Property Id="ARPPRODUCTICON" Value="AppIcon.ico" />
    <Property Id="ARPURLINFOABOUT" Value="{ABOUT_URL}" />
    <Property Id="ARPNOMODIFY" Value="1" />

    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="ProgramFiles64Folder">
        <Directory Id="INSTALLDIR" Name="{APP_NAME}" />
      </Directory>
      <Directory Id="ProgramMenuFolder" />
      <Directory Id="DesktopFolder" />
    </Directory>
{shortcut('StartMenuShortcut', START_MENU_COMPONENT, 'ProgramMenuFolder', 'StartMenuShortcut')}
{shortcut('DesktopShortcut', DESKTOP_COMPONENT, 'DesktopFolder', 'DesktopShortcut')}

    <Feature Id="Main" Title="{APP_NAME}" Level="1">
      <ComponentGroupRef Id="AppFiles" />
      <ComponentRef Id="StartMenuShortcut" />
      <ComponentRef Id="DesktopShortcut" />
    </Feature>
  </Product>
</Wix>
"""


def build_msi(package_dir: Path, out_msi: Path, version: str, work: Path) -> Path:
    """``out_msi`` a partir da pasta do pacote (runtime/ + app/)."""
    work.mkdir(parents=True, exist_ok=True)
    icon = art.write_ico(work / 'PhotoEditor.ico')

    files = sorted(
        str(p) for p in package_dir.rglob('*')
        if p.is_file() and p.parent != package_dir     # so runtime/ e app/
    )
    heat = subprocess.run(
        [WIXL_HEAT, '--prefix', f'{package_dir}/', '--directory-ref', 'INSTALLDIR',
         '--component-group', 'AppFiles', '--var', 'var.SourceDir', '--win64'],
        input='\n'.join(files) + '\n', capture_output=True, text=True, check=True)
    (work / 'files.wxs').write_text(heat.stdout)
    (work / 'main.wxs').write_text(_main_wxs(version, icon))

    out_msi.parent.mkdir(parents=True, exist_ok=True)
    out_msi.unlink(missing_ok=True)
    subprocess.run([WIXL, '--arch', 'x64', '-D', f'SourceDir={package_dir}', '-D', 'Win64=yes',
                    '-o', str(out_msi), str(work / 'main.wxs'), str(work / 'files.wxs')],
                   check=True)
    return out_msi
