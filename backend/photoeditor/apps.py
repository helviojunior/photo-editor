from django.apps import AppConfig


class PhotoEditorConfig(AppConfig):
    default_auto_field = 'django.db.models.BigAutoField'
    name = 'photoeditor'
    app_name = 'photoeditor'
    verbose_name = "PhotoEditor"

    def ready(self):
        # Import tardio para evitar import circular
        from .startup import on_startup
        on_startup()
