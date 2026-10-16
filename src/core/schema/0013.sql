-- Passo 13: categoria "arquivada". A foto arquivada (Backspace no editor) sai de
-- raw/ para archived/, como a excluída vai para deleted/; o nome que ela tem lá
-- (diferente do original se houve colisão) fica nesta coluna.
ALTER TABLE "photoeditor_photo" ADD COLUMN "archived_file_name" varchar(255) NOT NULL DEFAULT '';
