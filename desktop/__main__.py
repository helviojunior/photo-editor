"""Ponto de entrada: ``python desktop`` (pacote) ou ``python -m desktop``.

O launcher do pacote roda ``runtime/python -E -s app/desktop``: executar a
PASTA poe ela mesma no sys.path, e os imports sao ``desktop.*`` — por isso a
pasta de cima entra aqui.
"""
import sys
from pathlib import Path

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from desktop.app import main  # noqa: E402

main()
