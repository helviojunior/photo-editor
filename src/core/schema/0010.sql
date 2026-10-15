-- Passo 10 do esquema do catálogo do evento = migration Django 0010_photo_copy_of
-- (SQL gerado pelo `manage.py sqlmigrate` da versão em Python). Nunca editar:
-- bancos que já aplicaram este passo não o rodam de novo (regra 13).
ALTER TABLE "photoeditor_photo" ADD COLUMN "copy_of_id" char(32) NULL REFERENCES "photoeditor_photo" ("id") DEFERRABLE INITIALLY DEFERRED;
CREATE INDEX "photoeditor_photo_copy_of_id_a02ad3f1" ON "photoeditor_photo" ("copy_of_id");
