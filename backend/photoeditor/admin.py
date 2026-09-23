from django.conf import settings
from django.contrib import admin

# Admin publico: o acesso e liberado pelo PublicAdminMiddleware, que entra
# como o usuario ``admin`` padrao (sem senha). Modelos do editor sao
# registrados aqui com @admin.register.

admin.site.site_header = getattr(settings, 'BRAND_NAME', 'PhotoEditor')
admin.site.site_title = getattr(settings, 'BRAND_NAME', 'PhotoEditor')
admin.site.index_title = 'Admin'
