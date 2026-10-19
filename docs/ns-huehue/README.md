# NS HUEHUE — visual aprovado e mapa de implementação

## Aprovação

- Pacote: `design/ns-huehue-v1/` do PR #2 em `michelbrunoborges-svg/NXZero-HueHue`
  (branch `codex/ns-huehue-design-v1`, 52 telas, HANDOFF.md, styles.css, assets).
- Aprovado por Michel em 17/10/2026: **"Aprovo como está"**.
- Base do emulador: **Eden (HueHue Switch)**, com o motor atualizado do eden-xbox
  (JulianDr14, hoje Jadyx) até **0.2.93.3**: o motor (core, video_core/D3D12, áudio, dynarmic e o
  patch do Mesa) vem inteiro do upstream; o frontend continua em `src/eden_uwp` com o visual do NS
  HUEHUE. A pasta de dados segue `LocalState\eden` (o upstream passou a usar `jadyx`), para manter
  chaves, firmware e saves.
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
| 07 Opções do jogo | `DrawNsGameOptions` | Iniciar jogo · Mods · Cheats · Atualizações · DLC · Pulse · Perfil de desempenho · Favorito |
| 09 Biblioteca vazia / 47 Carregando | `DrawNsEmpty` / `DrawNsLibrary` | "Bora começar?": A abre Configurações › Arquivos e USB |
| 11–16 Configurações | `DrawNsSettings` + páginas do Eden | Sistema, Controles, Arquivos e USB, Online, Créditos e licenças |
| 03b Traduções | aba da biblioteca | traduções PT-BR do índice CNX; A baixa e instala; selo PT-BR nos cards |
| 40–41 Multijogador | Configurações › Online | salas do Eden pela internet (ver ONLINE.md) |
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
de saves, 39 Nextendo, 44 Diagnóstico, 45 confirmar exclusão, 46 Sem
conexão, 49 primeira configuração, 50 atualização do app.

Telas internas do Eden que mantêm o próprio layout, sobre o fundo do pacote: página Sistema,
editor de nome, teclado do PC, mods, cheats, updates/DLC, conteúdo ausente.

## Teste no Xbox

**Pendente**: só vale depois de instalar no console (Device Portal → app em modo Game).
Os prints do CI rodam num Windows sem GPU, com o mesmo código Direct2D.

## Melhorias de 18/10

- **Traduções em lote:** aba Traduções na biblioteca, cruzando os jogos com o índice CNX (nx-links).
- **Update/DLC automáticos:** arquivos de update/DLC do jogador colocados na pasta de jogos são
  registrados sozinhos na varredura e somem da lista de jogos (`LocalState/ns_content_scan.tsv`).
  Download de update/DLC da internet **não** é feito (conteúdo pago da Nintendo).
- **Perfil de desempenho por jogo:** Padrão, Desempenho, Compatibilidade, Qualidade
  (`LocalState/ns_profiles.tsv`). Drivers customizados não existem no Xbox (só o D3D12 do console).
- **Online por salas:** ver [ONLINE.md](ONLINE.md).

## Melhorias de 18/10 (tarde)

- **Ícone e tiles do app:** Square150/310/71, Wide310x150, Square44 e StoreLogo compostos com o
  logo, o mascote e o fundo aprovados (`tools/huehue/make_tiles.py`), com `DefaultTile` no manifesto.
- **Multiplayer local 2–4:** os outros controles conectados viram jogadores 2, 3 e 4.
- **Desempenho e gráficos (por jogo):** perfil; resolução TV (até 1080p) ou portátil (720p, modo
  portátil do console); limite de FPS (automático, 60, 30, manual 10–60); pular quadros (0–3);
  shaders (4 ou 6 núcleos). 480p **não**: o renderer D3D12 ainda não tem escala de resolução.
- **Conteúdo mais fácil:** as Opções do jogo mostram o update ativo e quantos DLCs e mods estão ligados.

## Melhorias de 18/10 (noite)

- **Motor 0.2.93.3** do eden-xbox/Jadyx (core, D3D12, áudio, dynarmic e patch do Mesa), com a pasta
  de dados ainda em `LocalState\eden`.
- **Renderizador OpenGL do NX Zero** (Mesa d3d12, `src/eden_uwp/gl_window.*`), escolhido por jogo em
  Desempenho e gráficos → Renderizador. Shaders do OpenGL em GLSL (padrão) ou SPIR-V (menos
  memória). Limite de FPS, pulo de quadros e o menu do jogo (sobre o último quadro) também no
  OpenGL. Sem Pulse no OpenGL. Se o Mesa não subir, o jogo roda no D3D12.
- **Compatibilidade automática** (`src/eden_uwp/game_compat.h`): títulos da lista (hoje: Super
  Mario Odyssey) abrem no OpenGL na primeira vez; um jogo que derruba o app no D3D12 no primeiro
  minuto, ou que para a GPU, passa a abrir no OpenGL e a biblioteca avisa. Marcador da sessão em
  `LocalState\ns_session.txt`.
- **Online**: continua precisando de um servidor de salas (ver ONLINE.md).

## Melhorias de 19/10 (testes do Michel na 0.3.24)

- **Busca (Y e campo de busca) fechando o app:** a página de busca desenhava um título vazio com
  ponteiro nulo, que o Direct2D do console recusa; agora textos vazios não são desenhados e uma
  página que falhar ao desenhar só perde aquele quadro.
- **Desempenho e gráficos** (por jogo, salvo): Resolução TV / Portátil / 540p / 360p (as duas
  últimas pela escala de resolução do Eden, só no OpenGL; o Eden reduz em quartos, então 480p não
  existe); Ampliação da imagem (Automático, Nenhum, Bilinear, FSR 1 nítido/equilibrado/suave),
  saiu do menu do jogo; FPS manual livre de 0 a 60 (← → de 1 em 1, LB/RB de 10 em 10, 0 = sem
  limite); Memória Normal / Econômica.
- **Memória Econômica:** os caches do D3D12 guardam 512 MiB a mais de folga, cache do JIT de 64 MiB
  por núcleo e sem pré-compilação na abertura. Um jogo que fecha com pouca memória livre passa a
  abrir em Econômica (marcador de sessão com a memória livre).
- **Catálogo:** arquivos do mesmo jogo (outro dump, update ou DLC que não foi importado) viram um
  card só, o do jogo base.
- **Assets** do pacote de imagens corrigido pelo Michel (tiles redimensionados para 150/310 px).

## Saves no HD externo (19/10)

- **Configurações → Saves (USB)** (`src/eden_uwp/save_backup.*`): o jogador escolhe uma pasta no HD
  externo; o app guarda nela `NS-HUEHUE-saves` com os saves (`nand/user/save`, sem `temp`) e as
  contas do console (`nand/system/save/8000000000000010`, onde ficam o `profiles.dat` e os avatares:
  os saves ficam guardados no id da conta).
- **Backup automático** (ligado por padrão): ao abrir o app e sempre que um jogo volta para a
  biblioteca, copia só o que mudou (manifesto `manifest.tsv` com tamanho e data de cada arquivo).
  Cada arquivo é gravado como `.part` e renomeado, então um HD tirado no meio não estraga a cópia
  anterior.
- **Restaurar saves da pasta**: traz a cópia de volta (depois de reinstalar, ou em outro Xbox),
  com confirmação; os saves que estavam no Xbox ficam antes em `LocalState\ns_saves_before_restore`.
- Uma pasta com a cópia de **outra instalação** não é sobrescrita pelo automático: o app avisa e
  oferece Restaurar; Fazer backup agora só a substitui depois de confirmar de novo.
- Os jogos continuam salvando em `LocalState`: no Xbox o app só escreve fora dela pelo seletor de
  pastas (WinRT), que o código de arquivos do Eden não usa.

## Sem a página Créditos (19/10)

- A seção **Créditos e licenças** saiu de Configurações. Fica só uma linha discreta sob a lista de
  seções com a versão, o aviso da GPL v3 (software livre, sem garantia, texto da licença no `LICENSE.txt` que agora vai
  no pacote), sem link do repositório, e "não afiliado à Nintendo ou à Microsoft": a GPL v3 (seção 5d) pede esse aviso numa
  interface interativa. Os créditos continuam no código-fonte e nos arquivos de licença do pacote.

## Rodada de 19/10 (tarde): instalação, avançado por jogo e layout Switch

- **Certificado fixo:** o CI guarda o certificado de assinatura (cache `signing-cert-v1`) e assina
  todas as versões com ele. Antes cada build criava um certificado novo, e o console que confiava
  no anterior recusava a atualização. O console precisa confiar no `.cer` novo uma vez.
- **Manifesto:** `rescap:expandedResources` (os recursos estendidos que o console dá a jogos).
- **Desempenho e gráficos → avançado (⚙, por jogo, salvo):** limite de velocidade; comandos D3D12
  em outra thread (`async_record`); apresentação em outra thread; texturas ASTC na GPU/CPU/BC3;
  ASTC opaco BC1/BC3; fastmem desligado/híbrido/completo; liberar pipelines ociosos; espera dos
  núcleos (adaptativa/bloquear/girar); cache do JIT 64/128/256 MiB. São opções que o motor já
  tinha só pelo `boot.cfg`; a página rola (10 linhas visíveis de 19).
- **Sistema → Layout do menu:** NS HUEHUE - Padrão ou NS HUEHUE - Switch. O Switch troca o início
  por uma fila de capas grandes (recentes primeiro, até 12, e Todos os jogos), o nome do jogo
  selecionado em cima, uma fila de botões redondos (Todos os jogos, Favoritos, Recentes, Buscar,
  Saves, Controles, Configurações, Sair), relógio e jogador no topo, e o fundo liso nas outras
  telas. A inicia o jogo, X abre as opções, Y busca.
