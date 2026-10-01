from django.db import models

from photoeditor.dbmodels.base import Base


class InstagramPost(Base):
    """Uma publicacao feita pelo botao "Publicar no Instagram".

    Registro do que saiu do evento: em qual conta, com que legenda e quais
    versoes Instagram (na ordem do carrossel). ``code`` e o da URL do post
    (``https://www.instagram.com/p/<code>/``).
    """

    username = models.CharField(max_length=64)
    media_id = models.CharField(max_length=64, blank=True, default='')
    code = models.CharField(max_length=64, blank=True, default='')
    caption = models.TextField(blank=True, default='')
    photos = models.JSONField(default=list, blank=True)

    class Meta:
        ordering = ['-created']

    def __str__(self):
        return f'@{self.username} {self.code}'
