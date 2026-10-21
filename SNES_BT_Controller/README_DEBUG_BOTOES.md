# Debug dos botoes fisicos

O firmware imprime no Monitor Serial (115200 baud) apenas quando um botao muda de estado, evitando mensagens repetidas a cada ciclo.

Exemplos:

```text
[BUTTON] A PRESSIONADO
[BUTTON] A SOLTO
[BUTTON] SELECT PRESSIONADO
[BUTTON] SELECT SOLTO
```

Botoes monitorados: A, B, X, Y, SELECT e START. O estado eletrico esperado e ativo em LOW (`INPUT_PULLUP`), com excecao de GPIO34 e GPIO35, que exigem pull-ups externos. O debug roda em `InputManager::readState()`.

Abra o Monitor Serial a 115200 baud. Como o firmware tambem usa `Serial` para logs, nao use a mesma UART USB/Serial para conectar a UART do Raspberry; a comunicacao UART do projeto usa `Serial2` nos GPIO17/16.
