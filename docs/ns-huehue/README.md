# NS HUEHUE — visual aprovado e mapa de implementação

## Aprovação

- Pacote: `design/ns-huehue-v1/` do PR #2 em `michelbrunoborges-svg/NXZero-HueHue`
  (branch `codex/ns-huehue-design-v1`, 52 telas, HANDOFF.md, styles.css, assets).
- Aprovado por Michel em 17/10/2026: **"Aprovo como está"**.
- Base do emulador: **Eden (HueHue Switch)**, com o motor atualizado do eden-xbox
  (JulianDr14) até 0.2.92.1, o último antes do rename `src/eden_uwp` → `src/jadyx`.
- Identidade do pacote mantida (`HueHue.HueHueSwitch`, `CN=HueHueSwitch`): instala por
  cima do HueHue Switch e preserva saves, chaves, firmware e jogos importados.

## Regras do HANDOFF seguidas

- Assets do pacote usados como estão (`dist/uwp/Assets/ns/`): logo, mascote, fundo grafite,
  padrão, capa genérica e ícones. Nada foi redesenhado nem gerado de novo.
- Telas desenhadas em Direct2D a partir dos tokens (cores, tamanhos, margens, foco de 3 px),
  não com prints estáticos. Canvas de 1920×1080, escalado para a janela.
- Rodapé mostra só os atalhos que a tela realmente atende. Nenhum botão sem ação.
- Todas as opções do backend do Eden continuam acessíveis (Sistema, Controles, Arquivos,
  Opções do jogo: mods, cheats, updates, DLC, sessão Pulse).
- Créditos e licenças continuam no app (Configurações → Créditos e licenças).
- Sem chaves, firmware ou jogos no pacote. Sem logos da Nintendo/Xbox e sem capas protegidas.

## Telas implementadas (número do pacote → onde está)

| Tela | Implementação | Observação |
|---|---|---|
| 01 Início | `DrawNsHome` | menu: Meus jogos, Configurações, Controles (sem botão Início e sem Sobre, a pedido do Michel em 17/10) |
| 02 Meus jogos | `DrawNsLibrary` | grade 5 colunas, 10 por página |
| 03 Favoritos / 04 Recentes | abas da biblioteca | `LocalState/ns_favorites.txt`, `ns_recent.txt` (30 últimos) |
| 05 Buscar jogos / 51 Nenhum resultado | `DrawNsSearch` (Y) | página inteira: caixa, contagem, 1º resultado e teclado; filtra enquanto digita, sem acento e sem maiúscula |
| 07 Opções do jogo | `DrawNsGameOptions` | Iniciar jogo · Mods · Cheats · Atualizações · DLC · Pulse · Favorito |
| 09 Biblioteca vazia / 47 Carregando | `DrawNsEmpty` / `DrawNsLibrary` | "Bora começar?": A abre Configurações › Arquivos e USB |
| 11–16 Configurações | `DrawNsSettings` + páginas do Eden | Sistema, Controles, Arquivos e USB, Créditos e licenças |
| 18 Controles | `DrawNsControls` | linhas reais do Eden (tipo, A/B/X/Y, zona morta, teclado) |
| 42 Sobre / 43 Créditos | Configurações › Créditos e licenças | Sobre retirado do menu; créditos, versão e licenças continuam (GPL) |
| 48 Splash | `SplashScreen.png` do pacote | composto com o fundo, mascote e logo do pacote |

## Navegação

A confirmar · B voltar · X opções do jogo · Y buscar · LB/RB trocam de seção
(Meus jogos, Configurações, Controles; B volta ao menu inicial) · ↑ na primeira linha da grade vai para
as abas e a busca. Menu na grade relê a biblioteca; View abre Controles.
Teclado/PC: Enter, Esc, X, Y, Q/E, R (reler), F1, F2.

## Telas do pacote sem backend no Eden (não implementadas de propósito)

Não existem no Eden hoje, então não ganharam botão (regra do HANDOFF: nada sem ação):
06 Detalhes do jogo, botões "Ver instruções" (09), "Diagnóstico" (42) e "Ver licenças" (43), 08 Iniciando jogo (o Eden usa a própria tela de carregamento), 10 erro
dedicado (o erro aparece no aviso do rodapé), 13 Online, 14/30–34 Contas e avatares,
17 Renderizador (o Eden só tem D3D12), 19–22 importação por USB (a importação é pelo
seletor de pastas em Arquivos), 23–26 fontes de conteúdo e downloads, 35–38 sincronização
de saves, 39 Nextendo, 40–41 multijogador, 44 Diagnóstico, 45 confirmar exclusão, 46 Sem
conexão, 49 primeira configuração, 50 atualização do app.

Telas internas do Eden que mantêm o próprio layout, sobre o fundo do pacote: página Sistema,
editor de nome, teclado do PC, mods, cheats, updates/DLC, conteúdo ausente.

## Teste no Xbox

**Pendente**: só vale depois de instalar no console (Device Portal → app em modo Game).
Os prints do CI rodam num Windows sem GPU, com o mesmo código Direct2D.
