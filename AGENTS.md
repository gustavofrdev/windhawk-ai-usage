# windhawk-ai-usage

Mod do Windhawk que mostra, numa pílula na borda esquerda da tela, o uso de
sessão (5h) e semanal de cada provedor de IA, lido do `ai-usagebar`.

- Código: `src/`, um arquivo por responsabilidade.
- `ai-usage-pill.wh.cpp` é GERADO por `tools/bundle.sh` (o Windhawk compila um
  arquivo só). Não edite o gerado; edite `src/` e rode o bundle.
- Teste e prévia fora do Windhawk: `tests/build.sh` (bundle + selftest + DLL);
  `tests/build.sh preview 20` mostra a pílula real na tela.
- `src/brand_logo_paths.h` é GERADO por `tools/svg_to_logo.py` a partir de
  `assets/*.svg`. Logo novo: SVG em `assets/`, entrada em `LOGOS` no script e
  em `kBrandLogos` (`src/brand_logo.h`).
- Design: `docs/2026-09-25-ai-usage-pill-design.md`.

## Estilo de código

- Funções: de 4 a 20 linhas. Acima disso, divida.
- Arquivos: abaixo de 500 linhas. Divida por responsabilidade. Vale para
  `src/`; o `.wh.cpp` gerado pelo bundle fica fora da regra.
- Arquivo novo em `src/` entra na lista `SOURCES` de `tools/bundle.sh`, na
  ordem de dependência.
- Uma coisa por função, uma responsabilidade por classe.
- Nomes específicos e únicos. Evite `data`, `handler`, `Manager`.
  Prefira nomes que devolvam menos de 5 resultados no grep do projeto.
- Tipos explícitos. Evite `auto`, exceto quando o tipo já está escrito na
  mesma linha (cast, `make_unique<T>`) ou é um iterador.
- Sem duplicação. Extraia a lógica comum para uma função ou classe.
- Retorno antecipado em vez de if aninhado. No máximo 2 níveis de indentação
  dentro do corpo da função.
- `switch` quando houver várias condições possíveis, em vez de if-else.
- Nomes de função, classe e variável em inglês.
- Código orientado a objetos: estado e comportamento juntos numa classe;
  funções livres só para utilidades puras e para os pontos de entrada do
  Windhawk (`Wh_ModInit`, `Wh_ModUninit`, `Wh_ModSettingsChanged`).
- Mensagem de exceção inclui o valor recusado e o formato esperado. Ela é
  lida por quem abre o log (`Wh_Log`), não por quem olha a pílula.
- DRY: funções precisam ser reaproveitáveis. Extraia antes de duplicar.
- RAII para todo recurso do Windows (HANDLE, HDC, HBITMAP, thread). Nada de
  `CloseHandle` solto em caminho de erro.

## Mensagens para a pessoa

A pílula não mostra texto de erro: falha vira pílula apagada com "!" e o
detalhe vai para o `Wh_Log`. Textos de configuração e README seguem a receita:

1. Comece pelo verbo que a pessoa tem que executar (Informe, Escolha, Rode).
2. Diga onde resolver quando o conserto é em outro lugar.
3. Não exponha valor interno que a pessoa não conhece.
4. No máximo duas frases: o problema e a saída.

## Comentários

- Preserve os comentários existentes. Não os remova ao refatorar.
- Escreva o porquê, não o quê.
- Método público: comentário com a intenção mais um exemplo de uso.
- Cite o número da issue ou o SHA do commit quando uma linha existe por
  causa de um bug específico ou de uma restrição externa.

## Dependências

- Injete pelo construtor ou por parâmetro. Nada de singleton escondido.
- Embrulhe API de terceiro (WinRT JSON, GDI+, API do Windhawk) atrás de uma
  classe do projeto. O resto do código não chama essas APIs direto.

## Validação

- Trate a saída do `ai-usagebar` como entrada não confiável: valide tipo e
  faixa (percentual 0–100) antes de usar.
- Toda chamada externa (processo, JSON) tem timeout ou falha controlada; o
  mod nunca pode travar nem derrubar o `explorer.exe`.

## Antes de publicar

- Procure segredo vazado (o fixture de teste não pode ter token), comando
  montado a partir de entrada não confiável e recurso sem liberar.
- Rode a auditoria com pelo menos dois modelos diferentes antes de subir.
- Rode `tests/build.sh` (testes + compilação com as flags do Windhawk).

## Entrega

- PRs pequenos, um por funcionalidade.
- Você não está autorizado a commitar antes da revisão do usuário: ao
  terminar, peça a revisão e só commite depois da aprovação.
