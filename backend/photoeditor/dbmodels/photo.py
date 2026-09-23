from django.db import models

from photoeditor.dbmodels.base import Base


class Photo(Base):
    """Uma foto original de ``<project>/raw`` (somente JPEG).

    O arquivo original nunca e alterado: o catalogo so guarda o que foi lido
    dele. A edicao vive em ``Adjustment``; excluir move o arquivo para
    ``deleted/`` e muda o ``status``.
    """

    class Status(models.TextChoices):
        ACTIVE = 'active', 'Active'
        DELETED = 'deleted', 'Deleted'
        # Estava catalogada e sumiu de raw/ e de deleted/ sem passar pelo editor.
        MISSING = 'missing', 'Missing'

    file_name = models.CharField(max_length=255, unique=True)
    # Nome do arquivo dentro de deleted/ — pode diferir de file_name quando ja
    # existia outro arquivo com o mesmo nome la.
    deleted_file_name = models.CharField(max_length=255, blank=True, default='')
    status = models.CharField(max_length=16, choices=Status.choices,
                              default=Status.ACTIVE, db_index=True)

    size_bytes = models.BigIntegerField(default=0)
    # mtime em nanossegundos: muda quando o arquivo e trocado, e invalida os
    # derivados em cache (thumbnail/preview).
    mtime_ns = models.BigIntegerField(default=0)
    # Dimensoes JA na orientacao de exibicao (EXIF Orientation aplicado).
    width = models.PositiveIntegerField(default=0)
    height = models.PositiveIntegerField(default=0)
    orientation = models.PositiveSmallIntegerField(default=1)

    # DateTimeOriginal + SubsecTimeOriginal (+ OffsetTimeOriginal): ordena as
    # fotos na sequencia em que o evento aconteceu.
    captured_at = models.DateTimeField(null=True, blank=True, db_index=True)

    class Meta:
        ordering = [models.F('captured_at').asc(nulls_last=True), 'file_name']

    def __str__(self):
        return self.file_name
