-- Passo 14: níveis, curvas e efeitos do restante da foto (os das camadas vão
-- dentro do JSON de "layers"). Os padrões são a identidade: as fotos já
-- editadas continuam idênticas (e com o mesmo settings_hash).
ALTER TABLE "photoeditor_adjustment" ADD COLUMN "levels" text NOT NULL DEFAULT '{}';
ALTER TABLE "photoeditor_adjustment" ADD COLUMN "curves" text NOT NULL DEFAULT '{}';
ALTER TABLE "photoeditor_adjustment" ADD COLUMN "effects" text NOT NULL DEFAULT '[]';
