"""Instaladores do app desktop, gerados dentro do builder (tools/Dockerfile).

* ``art``     — icones (.icns/.ico) e o fundo da janela do DMG, desenhados a
                partir do logo do build (nada de arte binaria versionada);
* ``macos``   — ``PhotoEditor.app`` + ``.dmg`` com a janela "arraste para
                Aplicativos", montado no Linux (mkfs.hfsplus + libdmg-hfsplus);
* ``windows`` — ``.msi`` via wixl (msitools).

Dependem de Pillow, ds_store e mac_alias, que so existem no container.
"""
