from django.db import models

from photoeditor.dbmodels.base import Base


class Adjustment(Base):
    """Ajustes de uma foto — a edicao inteira mora aqui, nunca no arquivo.

    Um campo por slider do motor (``imaging/develop.py:SLIDERS``), neutro em 0.
    ``preset`` e o "look" somado por cima dos valores (ver ``develop.PRESETS``).
    Foto sem linha aqui = foto sem edicao.
    """

    photo = models.OneToOneField('photoeditor.Photo', on_delete=models.CASCADE,
                                 related_name='adjustment')
    exposure = models.FloatField(default=0)
    contrast = models.FloatField(default=0)
    highlights = models.FloatField(default=0)
    shadows = models.FloatField(default=0)
    whites = models.FloatField(default=0)
    blacks = models.FloatField(default=0)
    temperature = models.FloatField(default=0)
    tint = models.FloatField(default=0)
    vibrance = models.FloatField(default=0)
    saturation = models.FloatField(default=0)
    preset = models.CharField(max_length=32, blank=True, default='')

    # Crop na proporcao da foto (ver develop.normalize_crop): escala do
    # quadro, centro em fracao da largura/altura e giro em graus.
    crop_scale = models.FloatField(default=1)
    crop_cx = models.FloatField(default=0.5)
    crop_cy = models.FloatField(default=0.5)
    crop_angle = models.FloatField(default=0)

    def __str__(self):
        return f'Adjustment {self.photo_id}'
