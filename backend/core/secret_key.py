"""Segredos gerados no primeiro uso (SECRET_KEY), na pasta de dados do usuario.

Fica fora de ``photoeditor/`` porque as settings precisam dele antes de o
Django existir.
"""
import secrets
import string
from pathlib import Path

# Sem pontuacao "viva": o valor vai para um arquivo .env lido por dotenv, onde
# aspas, crase, '#', '$' e chaves quebram o parse. Letras/digitos + simbolos
# sempre inertes ja dao entropia de sobra.
_SECRET_ALPHABET = string.ascii_letters + string.digits + "-_.~"


def generate_secret(min_len: int, max_len: int) -> str:
    """Segredo aleatorio com CSPRNG — nunca ``random``, que e previsivel."""
    length = secrets.randbelow(max_len - min_len + 1) + min_len
    return ''.join(secrets.choice(_SECRET_ALPHABET) for _ in range(length))


def ensure_secrets_file(path: Path) -> Path:
    """Cria ``path`` com uma SECRET_KEY nova se ele ainda nao existir."""
    if path.exists():
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'w', encoding='UTF-8') as f:
        f.write(f"SECRET_KEY={generate_secret(60, 80)}\n")
    try:
        # Em sistemas POSIX, restringe a leitura dos segredos ao usuario.
        path.chmod(0o600)
    except OSError:
        pass
    return path
