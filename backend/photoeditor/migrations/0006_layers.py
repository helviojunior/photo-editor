from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('photoeditor', '0005_crop'),
    ]

    operations = [
        migrations.AddField(
            model_name='adjustment',
            name='layers',
            field=models.JSONField(blank=True, default=list),
        ),
        migrations.AlterField(
            model_name='historyentry',
            name='kind',
            field=models.CharField(choices=[('delete', 'Delete'), ('adjust', 'Adjust'), ('auto', 'Auto'), ('preset', 'Preset'), ('reset', 'Reset'), ('crop', 'Crop'), ('layer', 'Layer')], max_length=16),
        ),
    ]
