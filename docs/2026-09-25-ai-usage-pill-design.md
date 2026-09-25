# AI Usage Pill — design

Aprovado em 25/09/2026.

## Objetivo

Olhar para o canto esquerdo da tela e ver, sem clicar, quanto do limite de
sessão (5h) e semanal já foi usado no Claude e no Codex.

## Decisões

| Tema | Decisão |
|---|---|
| Plataforma | Mod do Windhawk injetado no `explorer.exe`; uma instância por sessão (mutex nomeado). |
| Fonte dos dados | `wsl.exe -e sh -lc "ai-usagebar usage --json"` (shell de login, para o PATH incluir `~/.local/bin`), escondido (sem console), a cada 5 min (abaixo disso os endpoints respondem 429). Reaproveita o login do WSL. Comando configurável. |
| Formato | Faixa horizontal dentro da taskbar, no canto esquerdo (12 px da borda, configurável), centralizada na altura dela. Revisado em 25/09: a primeira versão ficava no meio da borda esquerda da tela. |
| Conteúdo | Por provedor: barra da sessão 5h + %, e abaixo barra fina da semanal + % "sem". Métricas identificadas por `window_secs` (18000 e 604800). Depois do %, o tempo até o reset (`↻ 2h10`, `↻ 35m`, `↻ 5d`), calculado do `reset_at` e redesenhado a cada minuto. |
| Cores e logos | Logo do provedor antes das barras, na cor da marca: Claude `#D97757`, Codex `#10A37F` (configurável). Logos vêm dos SVG do ai-usagebar (MIT), convertidos em vetor por `tools/svg_to_logo.py`; provedor sem logo ganha um ponto na cor dele. Sem fundo próprio. |
| Comportamento | Janela "possuída" pela taskbar (fica acima dela), clique atravessa, fora da barra de tarefas e do Alt+Tab, não pega foco. Some quando há app em tela cheia; posição conferida a cada 2 s. |
| Falha | Mantém os últimos valores, pílula apagada (metade da opacidade) com ponto âmbar. Detalhe no `Wh_Log`. |

## Componentes (`src/`, unidos em `ai-usage-pill.wh.cpp` por `tools/bundle.sh`)

- `PillSettings` / `PillSettingsLoader` — lê as configurações do Windhawk, com padrão (`common.h`, `mod_entry.cpp`).
- `HiddenProcessRunner` — roda o comando sem janela, com timeout, devolve stdout.
- `UsageReportParser` — JSON do `ai-usagebar` → `std::vector<VendorUsage>` (WinRT JSON embrulhado aqui).
- `UsagePoller` — thread que busca a cada intervalo e publica no `UsageSnapshotStore`.
- `BrandLogoShape` / `LogoPathBuilder` — logo gerado (`brand_logo_paths.h`) → caminho GDI+.
- `PillPainter` — desenha logo, barras e números num bitmap ARGB com GDI+.
- `PillWindow` — janela em camadas presa à taskbar, posição, DPI, tela cheia e repintura.
- `PillApp` — liga tudo; criado em `Wh_ModInit`, destruído em `Wh_ModUninit`.

## Teste

- `tests/pill_test.cpp` inclui o mod com `WH_EDITING` (as APIs do Windhawk viram stubs).
- `selftest`: parser contra `tests/fixture-usage.json` e casos inválidos.
- `preview` / `preview-stale`: abre a pílula real (normal ou com comando falhando) para captura de tela.
- `tests/build.sh` também compila o mod com as mesmas flags do Windhawk.
