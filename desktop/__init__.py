"""PhotoEditor desktop: janela nativa (Qt WebEngine) + backend Django local.

Dois processos, o mesmo Python embarcado:

* **shell** (``desktop/app.py``): a janela, os menus, a escolha da pasta do
  projeto e o navegador endurecido que mostra o frontend React;
* **servidor** (``desktop/server.py``): o Django servido pelo waitress em
  ``127.0.0.1``, um por projeto aberto (ou em modo Home, sem projeto).

Trocar de projeto reinicia o servidor: settings, banco e o estado em memoria
da exportacao sao do projeto, e processo novo e a unica troca sem resto.
"""
