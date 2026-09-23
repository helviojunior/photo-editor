"""Catalogo das fotos originais de ``<project>/raw``.

A varredura e idempotente: roda no boot e pelo endpoint de reescanear, e o
resultado so depende do que esta no disco.
"""
import logging
import threading

from django.conf import settings
from django.db import transaction

from photoeditor.imaging.io import is_jpeg, read_info
from photoeditor.models import Photo

log = logging.getLogger(__name__)

# Duas varreduras simultaneas (boot + clique em "reescanear") disputariam a
# criacao das mesmas linhas.
_scan_lock = threading.Lock()


def scan():
    """Sincroniza o catalogo com raw/ e deleted/. Devolve um resumo."""
    with _scan_lock:
        return _scan()


def _scan():
    summary = {'added': 0, 'updated': 0, 'restored': 0, 'missing': 0,
               'errors': 0, 'total': 0}
    raw_dir = settings.RAW_DIR
    if not raw_dir.is_dir():
        log.error("Scan skipped: %s does not exist.", raw_dir)
        return summary

    on_disk = {p.name: p for p in raw_dir.iterdir() if is_jpeg(p)}
    known = {p.file_name: p for p in Photo.objects.all()}

    with transaction.atomic():
        for name, path in sorted(on_disk.items()):
            stat = path.stat()
            photo = known.get(name)
            if photo and photo.mtime_ns == stat.st_mtime_ns \
                    and photo.size_bytes == stat.st_size:
                if photo.status != Photo.Status.ACTIVE:
                    # Voltou para raw/ por fora do editor (movida a mao).
                    photo.status = Photo.Status.ACTIVE
                    photo.deleted_file_name = ''
                    photo.save(update_fields=['status', 'deleted_file_name', 'updated'])
                    summary['restored'] += 1
                continue

            try:
                info = read_info(path)
            except Exception:
                log.exception("Could not read %s; skipping.", path)
                summary['errors'] += 1
                continue

            fields = dict(size_bytes=stat.st_size, mtime_ns=stat.st_mtime_ns,
                          width=info.width, height=info.height,
                          orientation=info.orientation, captured_at=info.captured_at,
                          status=Photo.Status.ACTIVE, deleted_file_name='')
            if photo is None:
                Photo.objects.create(file_name=name, **fields)
                summary['added'] += 1
            else:
                for k, v in fields.items():
                    setattr(photo, k, v)
                photo.save()
                summary['updated'] += 1

        # Catalogadas que nao estao em raw/: excluidas pelo editor continuam
        # "deleted" enquanto o arquivo estiver em deleted/; o resto sumiu.
        for name, photo in known.items():
            if name in on_disk:
                continue
            if photo.status == Photo.Status.DELETED \
                    and (settings.DELETED_DIR / (photo.deleted_file_name or name)).is_file():
                continue
            if photo.status != Photo.Status.MISSING:
                photo.status = Photo.Status.MISSING
                photo.save(update_fields=['status', 'updated'])
                summary['missing'] += 1

    summary['total'] = Photo.objects.filter(status=Photo.Status.ACTIVE).count()
    log.info("Catalog scan: %s", summary)
    return summary
