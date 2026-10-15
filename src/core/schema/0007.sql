-- Passo 7 do esquema do catálogo do evento = migration Django 0007_merge
-- (SQL gerado pelo `manage.py sqlmigrate` da versão em Python). Nunca editar:
-- bancos que já aplicaram este passo não o rodam de novo (regra 13).
CREATE TABLE "photoeditor_merge" ("id" char(32) NOT NULL PRIMARY KEY, "created" datetime NOT NULL, "updated" datetime NOT NULL, "enabled" bool NOT NULL, "layers" text NOT NULL CHECK ((JSON_VALID("layers") OR "layers" IS NULL)), "base_id" char(32) NOT NULL REFERENCES "photoeditor_photo" ("id") DEFERRABLE INITIALLY DEFERRED, "output_id" char(32) NULL UNIQUE REFERENCES "photoeditor_photo" ("id") DEFERRABLE INITIALLY DEFERRED);
CREATE INDEX "photoeditor_merge_base_id_3c4ae6b8" ON "photoeditor_merge" ("base_id");
