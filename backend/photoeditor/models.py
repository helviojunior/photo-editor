# Modelos do editor de fotos.
#
# O sistema e publico e nao autenticado: nao ha modelo de usuario proprio nem
# Company. O unico usuario existente e o ``admin`` padrao do Django admin (ver
# ``photoeditor/middleware.py``), no ``auth.User`` do proprio Django.
#
# Novos modelos herdam de ``photoeditor.dbmodels.base.Base`` (UUID, created,
# updated, enabled) e sao importados aqui para o Django descobri-los.
from photoeditor.dbmodels.photo import Photo  # noqa: F401
