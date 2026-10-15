-- Passo 9 do esquema do catálogo do evento = migration Django 0009_merge_base_area
-- (SQL gerado pelo `manage.py sqlmigrate` da versão em Python). Nunca editar:
-- bancos que já aplicaram este passo não o rodam de novo (regra 13).
CREATE TABLE "new__photoeditor_merge" ("base_mask" varchar(40) NOT NULL, "id" char(32) NOT NULL PRIMARY KEY, "created" datetime NOT NULL, "updated" datetime NOT NULL, "enabled" bool NOT NULL, "layers" text NOT NULL CHECK ((JSON_VALID("layers") OR "layers" IS NULL)), "base_id" char(32) NOT NULL REFERENCES "photoeditor_photo" ("id") DEFERRABLE INITIALLY DEFERRED);
INSERT INTO "new__photoeditor_merge" ("id", "created", "updated", "enabled", "layers", "base_id", "base_mask") SELECT "id", "created", "updated", "enabled", "layers", "base_id", '' FROM "photoeditor_merge";
DROP TABLE "photoeditor_merge";
ALTER TABLE "new__photoeditor_merge" RENAME TO "photoeditor_merge";
CREATE INDEX "photoeditor_merge_base_id_3c4ae6b8" ON "photoeditor_merge" ("base_id");
CREATE TABLE "new__photoeditor_merge" ("id" char(32) NOT NULL PRIMARY KEY, "created" datetime NOT NULL, "updated" datetime NOT NULL, "enabled" bool NOT NULL, "layers" text NOT NULL CHECK ((JSON_VALID("layers") OR "layers" IS NULL)), "base_id" char(32) NOT NULL REFERENCES "photoeditor_photo" ("id") DEFERRABLE INITIALLY DEFERRED, "base_mask" varchar(40) NOT NULL, "base_opacity" real NOT NULL);
INSERT INTO "new__photoeditor_merge" ("id", "created", "updated", "enabled", "layers", "base_id", "base_mask", "base_opacity") SELECT "id", "created", "updated", "enabled", "layers", "base_id", "base_mask", 1.0 FROM "photoeditor_merge";
DROP TABLE "photoeditor_merge";
ALTER TABLE "new__photoeditor_merge" RENAME TO "photoeditor_merge";
CREATE INDEX "photoeditor_merge_base_id_3c4ae6b8" ON "photoeditor_merge" ("base_id");
