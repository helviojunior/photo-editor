-- Passo 2 do esquema do catálogo do evento = migration Django 0002_history
-- (SQL gerado pelo `manage.py sqlmigrate` da versão em Python). Nunca editar:
-- bancos que já aplicaram este passo não o rodam de novo (regra 13).
CREATE TABLE "photoeditor_historyentry" ("id" char(32) NOT NULL PRIMARY KEY, "created" datetime NOT NULL, "updated" datetime NOT NULL, "enabled" bool NOT NULL, "kind" varchar(16) NOT NULL, "before" text NOT NULL CHECK ((JSON_VALID("before") OR "before" IS NULL)), "after" text NOT NULL CHECK ((JSON_VALID("after") OR "after" IS NULL)), "undone_at" datetime NULL, "photo_id" char(32) NOT NULL REFERENCES "photoeditor_photo" ("id") DEFERRABLE INITIALLY DEFERRED);
CREATE INDEX "photoeditor_historyentry_undone_at_765251dd" ON "photoeditor_historyentry" ("undone_at");
CREATE INDEX "photoeditor_historyentry_photo_id_4e13af3c" ON "photoeditor_historyentry" ("photo_id");
