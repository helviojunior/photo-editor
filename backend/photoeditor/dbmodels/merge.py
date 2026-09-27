from django.db import models

from photoeditor.dbmodels.base import Base


class Merge(Base):
    """Composicao de areas de varias fotos sobre uma foto base (ver
    ``services/merges.py``).

    ``layers`` guarda, na ordem de empilhamento (a de baixo primeiro), uma
    entrada por foto de origem::

        {photo, mask, opacity, visible,
         align: {matrix, inliers, error, ok, gain}}

    ``mask`` e a chave da mascara da area (em project_data/masks, sobre o
    preview da foto de origem, como as camadas do editor); ``align`` e a
    analise feita ao criar — a matriz leva pixels da foto de origem aos da
    base, os dois na resolucao cheia.

    ``base_mask``/``base_opacity``: uma area da propria base (a bola dela) e a
    opacidade com que ela fica; abaixo de 1, o que aparece por tras vem de
    uma das fotos das camadas, onde o objeto ja nao esta ali.

    Nada vai para raw/: a base passa a ser a foto do merge (a edicao parte
    dele) e o Exportar grava ``<base>_merge.jpg`` em publicar/.
    """

    base = models.ForeignKey('photoeditor.Photo', on_delete=models.CASCADE,
                             related_name='merges')
    layers = models.JSONField(default=list, blank=True)
    base_mask = models.CharField(max_length=40, blank=True, default='')
    base_opacity = models.FloatField(default=1.0)

    class Meta:
        ordering = ['-created']

    def __str__(self):
        return f'Merge {self.base_id}'
