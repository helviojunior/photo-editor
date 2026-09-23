from django.db import models

from photoeditor.dbmodels.base import Base


class HistoryEntry(Base):
    """Uma acao feita numa foto — o que sustenta o CTRL/CMD+Z.

    O historico e COMPLETO e mora no banco do projeto: nada e podado, e
    desfazer so marca ``undone_at``. Fechar e reabrir o projeto mantem tanto o
    que da para desfazer quanto o registro de tudo o que ja foi feito.

    ``before``/``after`` guardam o estado que a acao trocou (ajustes da foto,
    ou o nome do arquivo em deleted/). Desfazer = voltar ao ``before``.
    """

    class Kind(models.TextChoices):
        DELETE = 'delete', 'Delete'
        ADJUST = 'adjust', 'Adjust'
        AUTO = 'auto', 'Auto'
        PRESET = 'preset', 'Preset'
        RESET = 'reset', 'Reset'

    photo = models.ForeignKey('photoeditor.Photo', on_delete=models.CASCADE,
                              related_name='history')
    kind = models.CharField(max_length=16, choices=Kind.choices)
    before = models.JSONField(default=dict)
    after = models.JSONField(default=dict)
    undone_at = models.DateTimeField(null=True, blank=True, db_index=True)

    class Meta:
        ordering = ['-created']

    def __str__(self):
        return f'{self.kind} {self.photo_id}'
