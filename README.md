# AI Usage Pill

Mod do [Windhawk](https://windhawk.net) que mostra, direto na taskbar do
Windows 11, quanto você já usou dos limites do **Claude** e do **Codex**
(e de outros provedores suportados pelo [ai-usagebar](https://github.com/akitaonrails/ai-usagebar)).

![AI Usage Pill na taskbar](docs/img/taskbar.png)

- **Barra grossa**: sessão de 5 horas.
- **Barra fina (`sem`)**: limite semanal.
- **Logo e cor da marca** antes de cada provedor.
- Percentual fica vermelho a partir de 90% da sessão.

Você bate o olho no canto esquerdo da taskbar e sabe se ainda dá para
continuar ou se está perto de travar, sem abrir nada.

## Como funciona

```
explorer.exe (mod do Windhawk)
  ├─ thread de coleta ── a cada 5 min ──> wsl.exe -e sh -lc "ai-usagebar usage --json"
  │                                        (processo escondido, sem janela de console)
  │        └─ JSON → sessão 5h + semanal por provedor
  └─ thread de interface ── janela em camadas sobre a taskbar, desenhada com GDI+
```

1. O Windhawk injeta o mod no `explorer.exe`. Uma trava (mutex) garante que só
   uma pílula exista, mesmo com vários processos do Explorer.
2. Uma thread roda o `ai-usagebar usage --json` a cada intervalo, sem abrir
   console, com tempo limite de 30 s. Só os handles do pipe são herdados pelo
   processo filho.
3. O JSON é lido com a API `Windows.Data.Json`. As métricas são identificadas
   pela janela de tempo (`window_secs` 18000 = 5h, 604800 = 7 dias), não pelo
   texto, e percentuais fora de 0–100 são descartados ou limitados.
4. A janela é "possuída" pela taskbar (fica sempre acima dela), deixa o clique
   passar, não aparece no Alt+Tab e some quando um app entra em tela cheia.
5. Se a coleta falhar (WSL fora do ar, login expirado), a pílula continua com os
   últimos números, fica apagada e mostra um ponto âmbar. O motivo vai para o log
   do mod no Windhawk.

Os logos são os SVG do próprio ai-usagebar, convertidos em vetor por
`tools/svg_to_logo.py` e desenhados como caminho GDI+: ficam nítidos em
qualquer escala de tela.

## Requisitos

- Windows 11 com [Windhawk](https://windhawk.net).
- [ai-usagebar](https://github.com/akitaonrails/ai-usagebar) com login feito
  nos provedores. O padrão usa o ai-usagebar instalado **dentro do WSL**
  (`~/.local/bin`), aproveitando o login do Claude Code e do Codex de lá.
  Rode `ai-usagebar usage` no WSL uma vez para conferir que ele responde.

## Instalação

1. Baixe o [`ai-usage-pill.wh.cpp`](ai-usage-pill.wh.cpp).
2. No Windhawk, clique em **Create a New Mod**, apague o exemplo e cole o
   conteúdo do arquivo.
3. Clique em **Compile Mod** e depois em **Exit Editing Mode**.

A pílula aparece no canto esquerdo da taskbar em alguns segundos.

## Configurações

Em **Installed Mods → AI Usage Pill → Details → Settings**:

| Configuração | Padrão | Para que serve |
|---|---|---|
| Comando | `wsl.exe -e sh -lc "ai-usagebar usage --json"` | Qualquer comando que imprima o JSON do `ai-usagebar usage --json`. Com a versão Windows do ai-usagebar, use o caminho do `.exe`. |
| Intervalo de atualização | 5 min | De 1 a 60. Os endpoints da Anthropic e da OpenAI bloqueiam (HTTP 429) consultas com menos de ~5 min de intervalo; aí o ai-usagebar fica 5 min sem consultar e os números atrasam. |
| Distância da esquerda | 12 px | Aumente se a pílula cobrir algo da taskbar. |
| Opacidade | 100% | De 20 a 100. |
| Provedores | `anthropic` laranja, `openai` verde | Ordem e cor de cada provedor; use o id do ai-usagebar (`cursor`, `copilot`, ...). Provedor sem logo embutido aparece com um ponto na cor dele. |

**Observação:** o comando padrão chama o WSL a cada intervalo, então o WSL não
desliga sozinho por inatividade enquanto o mod estiver ativo.

## Desenvolvimento

O Windhawk compila um arquivo só, mas o código fica dividido por
responsabilidade em `src/`. Não edite o `ai-usage-pill.wh.cpp` à mão: ele é
gerado pelo bundle.

```
src/
  mod_metadata.cpp        metadados, README e configurações do Windhawk
  common.h                tipos, cores, utilitários
  hidden_process_runner.h processo escondido com timeout e cancelamento
  usage_report_parser.h   JSON do ai-usagebar → uso por provedor
  usage_poller.h          thread de coleta + estado compartilhado
  brand_logo_paths.h      logos em vetor (gerado por tools/svg_to_logo.py)
  brand_logo.h            logo → caminho GDI+
  pill_painter.h          desenho (layout, barras, texto)
  pill_window.h           janela sobre a taskbar, posição, tela cheia
  pill_app.h              liga coleta e interface; instância única
  mod_entry.cpp           configurações e pontos de entrada Wh_*
tools/bundle.sh           junta src/ em ai-usage-pill.wh.cpp
tools/svg_to_logo.py      assets/*.svg → src/brand_logo_paths.h
tests/build.sh            bundle + testes + compilação do mod
```

Com o Windhawk instalado, dentro do WSL:

```bash
tests/build.sh              # gera o bundle, roda os testes e compila o DLL do mod
tests/build.sh preview 20   # mostra a pílula real na tela por 20 s
```

O `build.sh` usa o clang que vem com o Windhawk e as mesmas flags do editor
dele. Os testes compilam o mod com `WH_EDITING`, que transforma as chamadas
da API do Windhawk em stubs.

Regras de estilo do projeto: [`AGENTS.md`](AGENTS.md).

## Créditos

- [ai-usagebar](https://github.com/akitaonrails/ai-usagebar), de Fabio Akita,
  que faz a coleta de verdade. Os ícones dos provedores vêm dele (MIT).
- Claude e Anthropic são marcas da Anthropic; OpenAI e Codex são marcas da
  OpenAI. Este projeto não tem vínculo com nenhuma das duas.

## Licença

[MIT](LICENSE)
