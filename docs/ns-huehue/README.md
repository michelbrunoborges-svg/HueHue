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
- **Sistema → Temas** (design `NXZero-HueHue/design/ns-huehue-switch-v1`, V1.1 aprovado em 19/10):
  HueHue Grafite (padrão, o de sempre), Switch Escuro e Switch Claro, com prévia que não salva,
  aplicação que mantém o tema anterior se não der para salvar, e fundo do início sólido ou Aurora.
  Ids estáveis em LocalSettings (`system_theme_id`: `huehue-graffiti`, `switch-dark`,
  `switch-light`; `system_wallpaper_id`: `solid`, `aurora`), separados dos saves.
  `src/eden_uwp/theme.h` põe as cores do tema em `Palette`/`Ns` no começo de cada quadro da
  biblioteca e devolve as do grafite no fim (o menu do jogo e o teclado do jogo ficam como são).
  Nos temas Switch: cabeçalho com perfil, relógio e controle; rodapé com o nome à esquerda e os
  atalhos à direita; foco ciano; ícones tingidos; o início é a fila de capas (300 px, a focada
  348 px) com o nome embaixo e seis atalhos redondos de 106 px (Biblioteca, Arquivos e USB, Saves,
  Controles, Configurações, Buscar: o "Sobre" do pack saiu, como pedido antes); Controles usa o
  desenho do controle Xbox do pack. Assets do pack em `dist/uwp/Assets/sw`.

## 0.4.0 (19/10): catálogo de temas, firmware com backup e correções

- **Correções dos temas Switch:** a ordem dos cartões em Temas (← → aplicavam o Claro no lugar do
  Escuro), capa reserva do pack Switch e o desenho do controle sem cobrir os textos.
- **Catálogo de temas** (`src/eden_uwp/theme_catalog.*`, issue #3 do NXZero-HueHue): além dos
  três temas, pacotes em `Assets/themes/<id>` e `LocalState/themes/<id>` com `theme.json`
  (schemaVersion 1, id, nome, versão, base `switch-dark`/`switch-light`, fundo, miniatura e
  cores) e a imagem 16:9. Temas → Importar temas (USB) copia e valida pacotes de uma pasta. O
  seletor rola pelo catálogo; prévia, aplicação e volta ao anterior como nos outros temas; o id
  do pacote fica salvo e, se o pacote sumir, volta o Grafite. O leitor aceita também o formato
  dos manifestos de `NXZero-HueHue/design/theme-assets-v1` (`assets.wallpaper`,
  `assets.thumbnail`, `palette`; base clara/escura pela cor de fundo). Incluído no pacote só o
  tema **16 · NS HUEHUE** (provisório, da entrega de 19/10). Os outros 15 têm personagens e
  marcas de terceiros (e o logo do Xbox): ficam fora do appx e do repositório públicos e entram
  pelo Importar temas (USB), a partir da pasta da entrega.
- **Firmware** (issue #5): Arquivos e USB → Importar firmware copia e valida numa pasta
  temporária, mostra a versão lida do arquivo SystemVersion (0100000000000809) e pede
  confirmação (instalar, atualizar, reinstalar ou versão mais antiga) antes de trocar; checa o
  espaço livre; troca por renomeação com recuperação ao abrir o app; confere a versão depois da
  troca e volta a anterior se não bater; guarda o firmware substituído em
  `registered.backup` (Restaurar firmware anterior). A pasta de origem nunca é alterada. Busca
  online: não configurada (não há fonte autorizada para baixar firmware).
- Versão do pacote: **0.4.0.<build>**.
- **Aprovação:** Michel aprovou os 16 fundos de `design/theme-assets-v1` em 19/10/2026 (“estão
  aprovados”). Distribuição: o 16 · NS HUEHUE vai no appx; os outros 15 continuam pelo
  Importar temas (USB), fora do repositório e do release públicos.

## 0.4.1 (19/10, noite): pedidos do teste no Xbox

- **Firmware:** a verificação só avisa. Todos os `.nca` da pasta são copiados. NCA de jogo,
  arquivo que as chaves não abrem ou falta do SystemVersion/Mii aparecem como "Erro na
  verificação: …; os arquivos foram copiados mesmo assim", antes da confirmação (A instala).
- **Desempenho por padrão:** jogo sem ajuste salvo abre no perfil Desempenho (GPU em precisão
  baixa e sem *reactive flushing*, como os builds Android do Eden; imagem bilinear). O antigo
  "Padrão" passou a se chamar **Equilibrado** (GPU precisa). Ajustes já salvos não mudam.
- **Idioma do jogo:** pt-BR → pt-PT → espanhol (latino, depois o da Espanha) → inglês. Os idiomas
  são lidos do NACP da atualização quando existe (atualizações costumam acrescentar idiomas).
- **Menu do jogo:** View + Menu (− e +) segurados por **2 s** abrem o menu (antes 1 s). Novas
  linhas: Limitar a velocidade e Volume (valem para a sessão). "Voltar à biblioteca" virou
  **Fechar o jogo**. A dica do atalho aparece ao abrir um jogo e em Controles.
- **Saves:** ao escolher uma pasta que já tem saves de outra instalação, eles são importados na
  hora (os atuais ficam guardados em `ns_saves_before_restore`). Restaurar continua no menu.
- **Capas:** não são baixadas: vêm do próprio arquivo do jogo. Quando o arquivo não abre, o
  cartão diz o motivo (sem a chave do jogo: importe o title.keys; chaves antigas: atualize o
  prod.keys; arquivo ilegível) e o log registra o status.
- **Fundo do tema** (pacote ou Aurora) também na grade de jogos, configurações e páginas, sob
  um véu de 80% da cor do tema.
- **Motor:** eden-xbox/Jadyx até **0.3.4.0** (PSO preparado em segundo plano e menos esperas
  de pipeline no D3D12, sessões Pulse versionadas e com zstd, Vulkan e ReShade fora do build da
  Store). Os módulos OpenGL/Mesa do NS HUEHUE continuam.
- Versão do pacote: **0.4.1.<build>**.

## 0.4.2 (19/10, noite): issue #6 do NXZero-HueHue, P1-A

- A issue #6 mapeou o código do NXZero-HueHue; o remapeamento para este repositório está no
  comentário da issue (o caminho SPIR-V → DXIL pelo Mesa e o diagnóstico de pipelines já existiam
  aqui; gravação em worker já é a opção "Comandos da GPU em outra thread").
- **Shaders pendentes** (Desempenho e gráficos → avançado, por jogo, padrão "Esperar"): em "Não
  esperar (experimental)" o draw cujo PSO ainda compila é pulado e contado, e o build segue nos
  workers. Passagens de tela cheia (até 6 vértices), draws indiretos e todo compute continuam
  esperando. `pso_defer=1` no boot.cfg liga o mesmo para testes. O log do pipeline traz
  `deferral on (N draws skipped while building, M full-screen waits)`.
- Versão do pacote: **0.4.2.<build>**.

## 0.4.3 (19/10, noite): firmware que não instalava e jogos que só rodavam sem firmware

- **Causa do erro ao instalar o firmware (0.4.0 a 0.4.2):** logo depois de copiar e validar, a
  biblioteca relia as configurações e a limpeza de pastas de importação antigas apagava a cópia
  que esperava a confirmação; o A então falhava ("Não foi possível importar"). A cópia agora fica
  protegida até o A ou o B.
- A importação tolera mais: arquivo que não copia (duas tentativas) é contado e o resto segue;
  pastas de NCA em partes (`xxx.nca/00`) são copiadas; se a pasta escolhida não tem `.nca`, o
  firmware é procurado nas subpastas (até dois níveis). Pasta com dois SystemVersion: vale o mais
  alto. As mensagens de erro trazem o código do Windows.
- **Applets do sistema sempre do emulador (HLE):** com firmware instalado o Eden rodava os applets
  do firmware (seleção de usuário, erro, Mii, amiibo, fotos, web) como um segundo processo do
  console; jogos que abriam sem firmware travavam com ele (Super Mario Odyssey). `applets=lle` no
  boot.cfg volta ao padrão do Eden para testes.
- **Firmware do console** (Desempenho e gráficos, por jogo): Automático (usa; se o jogo fechar
  com o firmware no primeiro minuto ou não carregar, passa a abrir sem ele e a biblioteca avisa),
  Usar sempre, Não usar (o firmware fica de lado só durante aquele jogo e volta ao fim; se o app
  fechar no meio, volta na próxima abertura).
- Versão do pacote: **0.4.3.<build>**.

## 0.4.4 (20/10): lista de correções do teste da 0.4.3.40

- **Início (tema padrão):** lista única Jogos, Favoritos, Recentes, Configurações, Sair do NS.
  Sem o item Controles (está em Configurações e no menu do jogo), B não sai do app, L/R não fazem
  nada no início; A entra e B volta em cada item.
- **Sem cabeçalho de abas** (Meus jogos/Configurações/Controles) em todas as telas: só a marca e o
  usuário. "Meus jogos" passou a "Jogos"; ao entrar, o cursor fica no filtro **Todos**; L/R trocam
  Todos/Favoritos/Recentes/Traduções. Favoritos e Recentes do início abrem já na aba certa.
- **Configurações:** sem a linha "Abrir" nas prévias (ficam as duas linhas de informação);
  "Controles e teclado" → "Controles"; sem "Buscar firmware online".
- **Saves (USB) — causa do backup que não gravava nada:** a lista dos saves do console usava
  `std::filesystem::relative`, que no Xbox falha no caminho do LocalState e devolvia vazio; agora
  é `lexically_relative`. O backup automático saiu: ao escolher **Sair do NS** com pasta de saves,
  o app pergunta (A backup e sair, Y sair sem backup, B cancelar). O backup manual continua.
- **Backup e restauração das configurações** (Saves (USB)): grava em `NS-HUEHUE-config` as
  configurações do app (LocalSettings), os perfis por jogo (`ns_profiles.tsv`), favoritos,
  recentes e o online; restaurar pede confirmação e recarrega tudo.
- **Sistema:** legendas sem sobreposição (layout novo) e **Volume** geral de 0 a 200% (passo 10).
- **Controles:** imagem do controle nova em todos os temas, legendas com todas as funções
  (navegar, d-pad, menu, buscar, opções, confirmar, voltar), "Por letra/Por posição" →
  **Modo Xbox / Modo Switch** e **Mapear botões** (cada botão do Switch recebe qualquer controle
  do Xbox, ou nenhum). Também por jogo, nas opções do X ("Controles do jogo": Próprio/Padrão) e no
  menu do jogo (Start+Select).
- **Tema Switch:** capas dos jogos do início não somem mais (os ícones dos cartões ficam
  guardados e são pedidos primeiro); L/R andam entre os jogos; X abre as opções do jogo e Y a
  busca; atalho novo **Sair do NS**; B não sai.
- **Velocidade do jogo** (por jogo e no menu do jogo): 10% a 200% em passos de 10, padrão 100%,
  abaixo de 10% = Desligado (sem limite). **Volume** do menu do jogo até 200%.
- **Mods:** linha "Pasta de downloads" na aba Arquivos: cada download terminado é copiado para a
  pasta escolhida (USB) com o nome do arquivo do site, e a pasta vira uma fonte para reinstalar.
  O mod instalado continua no armazenamento interno (`load/<id do jogo>`), que é onde o
  carregador do Eden lê. A instalação foi testada no PC com zips reais nos formatos do GameBanana
  (pasta com romfs, atmosphere/contents/<id>, pastas soltas do jogo, exefs com .ips) e com os
  arquivos 7z/RAR/RAR5 de teste do libarchive.
- Versão do pacote: **0.4.4.<build>**.

## 0.4.5 (23/10): o que o Eden 0.3.9 tem e o NS HUEHUE não tinha

Mapa das telas "Definições da consola" e "Definições rápidas" do Eden 0.3.9 (MNKE) contra o
nosso, item a item. Só entrou o que tem efeito real no nosso core; nada foi duplicado.

| Eden 0.3.9 | NS HUEHUE |
|---|---|
| Tema, Idioma, Região, Volume, Velocidade, Contador de fotogramas | já existiam (Sistema; Velocidade por jogo e no menu) |
| Modo da consola (TV/portátil) | já existia por jogo: Resolução TV / Portátil |
| **Modo de avião** | **novo** em Sistema (`airplane_mode`) |
| Filtro de ampliação | já existia: Ampliação da imagem |
| Texturas ASTC guardadas como / Descodificar ASTC na GPU | já existiam: Texturas ASTC e ASTC sem transparência |
| Resolução 1x/2x, Suavização (SMAA/FXAA), MSAA, Maior textura, Resolução de saída | não entram: o nosso D3D12 não tem escala de resolução nem esses filtros (são do renderizador próprio deles); seriam botões sem efeito |
| Shaders assíncronos / Modo assíncrono | já existiam: Shaders pendentes (Esperar / Não esperar) |
| **Passagens que esperam (vértices)** | **novo** por jogo e no menu do jogo (`SetPipelineDeferral`) |
| Threads de compilação de shaders | já existia: Shaders 4 / 6 núcleos |
| Reutilizar shaders traduzidos | o nosso já faz sempre (cache de shaders ligados) |
| Emular tesselação / grelha | o nosso usa a tesselação da própria GPU |
| **Emulação assíncrona da GPU** | **novo** por jogo |
| Gravar comandos num thread | já existia: Comandos da GPU em outra thread |
| Apresentação sem bloqueio | equivalente: Apresentar quadros em outra thread |
| Tabelas de descritores no thread, Emulação na própria fila, os 10 itens de Correções para a consola, e 7 de Memória | não entram: correções e caminhos internos do renderizador deles, sem código equivalente no nosso |
| Esperar pelos dados da GPU nas barreiras | **novo**: Leitura da GPU pela CPU (`use_reactive_flushing`) e **Precisão da GPU** |
| Memória inativa do jogo para o SSD | parecido com o nosso Memória do jogo no SSD (fastmem) |
| — | **novos** sem par no 0.3.9: Precisão da CPU (com Insegura), Filtro anisotrópico, Proporção da imagem, Vídeos do jogo (NVDEC) |
| Silenciar (menu rápido) | **novo** no menu do jogo |
| Painel de FPS | **refeito** com as mesmas linhas: FPS / ms, RAM usada / limite, GPU, JIT, EMU, Outro, carga da GPU, carga da CPU, núcleos (e o pico de quadro), no canto superior esquerdo |

- **Cache de shaders no HD externo** (`shader_backup.*`, Configurações > Saves e shaders (USB)):
  pasta própria, Copiar shaders para o HD, Trazer shaders do HD e Sincronizar ao jogar (ligado:
  antes de abrir o jogo traz o cache dele do HD se for maior; ao fechar leva o que cresceu). Em
  `<pasta>/NS-HUEHUE-shaders/<id do jogo>/` (d3d12.bin, d3d12_hot.bin, opengl.bin). O maior vence
  nos dois sentidos. O nosso cache guarda os shaders do jogo e refaz os PSOs ao abrir (o Series
  não aceita blobs de PSO prontos: `GetCachedBlob` derruba o device).
- **Temas:** os 15 temas com personagens e marcas de terceiros (e o logo do Xbox) continuam fora
  do appx e do repositório públicos; vão como pacote para Importar temas (USB).
- Versão do pacote: **0.4.5.<build>**.
