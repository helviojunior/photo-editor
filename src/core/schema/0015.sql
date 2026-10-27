-- Passo 15: dados editáveis do evento (nome de exibição, campeonato e
-- categoria). Passo próprio do app (não é migration Django): a versão em
-- Python não conhece esta tabela, então ela só é criada ao abrir aqui.
-- É um singleton (uma linha só); o nome vazio significa "use o nome da pasta".
-- Colunas created/updated/enabled e id char(32) para seguir o estilo das
-- demais tabelas photoeditor_*.
CREATE TABLE "photoeditor_eventinfo" ("id" char(32) NOT NULL PRIMARY KEY, "created" datetime NOT NULL, "updated" datetime NOT NULL, "enabled" bool NOT NULL, "name" varchar(255) NOT NULL DEFAULT '', "championship" varchar(255) NOT NULL DEFAULT '', "category" varchar(255) NOT NULL DEFAULT '');
