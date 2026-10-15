-- Passo 4 do esquema do catálogo do evento = migration Django 0004_photo_exported_hash
-- (SQL gerado pelo `manage.py sqlmigrate` da versão em Python). Nunca editar:
-- bancos que já aplicaram este passo não o rodam de novo (regra 13).
CREATE TABLE "new__photoeditor_photo" ("exported_hash" varchar(32) NOT NULL, "id" char(32) NOT NULL PRIMARY KEY, "created" datetime NOT NULL, "updated" datetime NOT NULL, "enabled" bool NOT NULL, "file_name" varchar(255) NOT NULL UNIQUE, "deleted_file_name" varchar(255) NOT NULL, "status" varchar(16) NOT NULL, "size_bytes" bigint NOT NULL, "mtime_ns" bigint NOT NULL, "width" integer unsigned NOT NULL CHECK ("width" >= 0), "height" integer unsigned NOT NULL CHECK ("height" >= 0), "orientation" smallint unsigned NOT NULL CHECK ("orientation" >= 0), "captured_at" datetime NULL);
INSERT INTO "new__photoeditor_photo" ("id", "created", "updated", "enabled", "file_name", "deleted_file_name", "status", "size_bytes", "mtime_ns", "width", "height", "orientation", "captured_at", "exported_hash") SELECT "id", "created", "updated", "enabled", "file_name", "deleted_file_name", "status", "size_bytes", "mtime_ns", "width", "height", "orientation", "captured_at", '' FROM "photoeditor_photo";
DROP TABLE "photoeditor_photo";
ALTER TABLE "new__photoeditor_photo" RENAME TO "photoeditor_photo";
CREATE INDEX "photoeditor_photo_status_7e7bd9a0" ON "photoeditor_photo" ("status");
CREATE INDEX "photoeditor_photo_captured_at_b7f57a41" ON "photoeditor_photo" ("captured_at");
