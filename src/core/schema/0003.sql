-- Passo 3 do esquema do catálogo do evento = migration Django 0003_adjustment
-- (SQL gerado pelo `manage.py sqlmigrate` da versão em Python). Nunca editar:
-- bancos que já aplicaram este passo não o rodam de novo (regra 13).
CREATE TABLE "photoeditor_adjustment" ("id" char(32) NOT NULL PRIMARY KEY, "created" datetime NOT NULL, "updated" datetime NOT NULL, "enabled" bool NOT NULL, "exposure" real NOT NULL, "contrast" real NOT NULL, "highlights" real NOT NULL, "shadows" real NOT NULL, "whites" real NOT NULL, "blacks" real NOT NULL, "temperature" real NOT NULL, "tint" real NOT NULL, "vibrance" real NOT NULL, "saturation" real NOT NULL, "preset" varchar(32) NOT NULL, "photo_id" char(32) NOT NULL UNIQUE REFERENCES "photoeditor_photo" ("id") DEFERRABLE INITIALLY DEFERRED);
