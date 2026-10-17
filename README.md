# HueHue Switch

Emulador de Nintendo Switch para **Xbox Series X|S em Dev Mode**, com o visual HueHue:
fundo preto com "HUE RA" na diagonal, logo HUEHUE nas cores do Brasil, destaque verde-limão
e interface em português.

É um fork do [eden-xbox](https://github.com/JulianDr14/eden-xbox) (port UWP do emulador
[Eden](https://git.eden-emu.dev/eden-emu/eden) com renderizador Direct3D 12). O motor do
emulador é o deles. Aqui mudam o tema, o idioma, o nome do app e o build automático.

> **Experimental.** No Series X, o projeto original reporta Pokémon: Let's Go a 60 FPS,
> Mario Strikers: Battle League a 60 FPS e Super Mario Bros. Wonder entre 55 e 60 FPS.
> Zelda: Tears of the Kingdom ainda estoura a memória. Outros jogos podem não abrir.
> A primeira passagem por uma área nova engasga enquanto os shaders compilam.


## Visual NX Zero (layout grafite)

Desde esta versão o app aparece como **NX Zero HueHue Switch** e abre num menu inicial
(INICIAR, JOGOS, CONFIGURAÇÕES, CONTROLES, SOBRE) com o letreiro "NX ZERO / BR HUEHUE EDITION",
a parede de grafite verde-neon e magenta e a cara do HueHue como mascote. B na biblioteca volta ao
menu; B no menu sai. As imagens ficam em `dist/uwp/Assets/ui` e saem de
`tools/huehue/make_ui_assets.py` (fontes livres em `tools/huehue/fonts`). A identidade do pacote não
mudou: instalar por cima mantém jogos, chaves e saves.

## O que mudou em relação ao eden-xbox

| Área | Mudança |
|---|---|
| Visual | Paleta preta + verde-limão `#C8F31D`, fundo "HUE RA HUEHUE" na diagonal, logo HUEHUE colorido com etiqueta SWITCH, faixa verde/amarelo/azul |
| Idioma | Português do Brasil (`src/eden_uwp/locales/pt.cpp`), escolhido automaticamente se o Xbox estiver em português |
| App | Nome "HueHue Switch", pacote `HueHue.HueHueSwitch` (instala ao lado do Eden original, sem conflito) |
| Ícones | Selo HueHue, tiles e splash (`dist/uwp/Assets`) |
| Build | GitHub Actions compila, empacota, assina e publica o `.appx` a cada push |

Arquivos do tema: `src/eden_uwp/ui_painter.h` (paleta), `src/eden_uwp/uwp_library_canvas.cpp`
(fundo, logo, biblioteca), `src/eden_uwp/game_menu_surface.cpp` (menu durante o jogo).

## Instalar no Xbox Series X

### 1. Pré-requisitos
- Xbox Series X|S em **Dev Mode** (app *Xbox Dev Mode Activation* + conta de desenvolvedor no Partner Center).
- Um PC na mesma rede, para abrir o Device Portal.
- **Suas próprias** chaves (`prod.keys`, `title.keys`), firmware e jogos, extraídos do **seu** Switch.
  Nada disso vem neste repositório nem no pacote, e nunca deve ser colocado aqui.

### 2. Baixar o pacote
Na aba **Releases** deste repositório, pegue a versão mais recente. São 3 arquivos:
- `huehue-switch.appx`: o app
- `Microsoft.VCLibs.x64.14.00.appx`: dependência obrigatória (sem ela o app não abre)
- `huehue-switch.cer`: certificado (use se o Device Portal pedir)

### 3. Instalar pelo Device Portal
1. No Xbox, em Dev Home, anote o endereço do **Remote Access** (algo como `https://192.168.0.20:11443`).
2. No PC, abra esse endereço e aceite o aviso de certificado do navegador.
3. **Add** → escolha `huehue-switch.appx` → Next.
4. Na tela de dependências, adicione `Microsoft.VCLibs.x64.14.00.appx`. Se pedir certificado, use o `.cer`.
5. Start e aguarde terminar.

### 4. Colocar em modo Game (obrigatório)
No Dev Home do Xbox, selecione **HueHue Switch** → botão **View** → **View details** →
**App type: Game**. Em modo App o limite de memória é de cerca de 1 GB e os jogos não rodam.

### 5. Chaves, firmware e jogos
1. Abra o HueHue Switch.
2. **Configurações (X)** → gerenciador de arquivos → importe chaves e firmware de uma pasta (pendrive USB).
3. Jogos: copie `.nsp`/`.xci` pelo Device Portal em **File explorer** → LocalAppData →
   `HueHue.HueHueSwitch_…` → `LocalState` → `games`. Ou adicione uma pasta do pendrive pela biblioteca.
4. Na biblioteca, aperte **Menu** para atualizar a lista.

### Atualizar
Cada build publica um Release com versão maior. Instale o novo `.appx` por cima pelo Device Portal;
chaves, jogos e configurações (em `LocalState`) são mantidos.

### Se algo der errado
Pelo Device Portal, em `LocalState`: `eden_uwp_diag.txt` e `eden\log\eden_log.txt` descrevem o que aconteceu.
Problemas gráficos ficam em `eden\log\eden_graphics_bugs.log`.

## Build

Automático: todo push na `main` (ou **Actions → Build Xbox (appx) → Run workflow**) roda
`.github/workflows/build-xbox.yml` num runner Windows com Visual Studio 2022 e publica o Release.
O primeiro build é o mais demorado; o Mesa (`spirv_to_dxil`) fica em cache depois disso.

Manual no Windows: veja `docs/README-eden-xbox.md` e `docs/xbox/uwp_build.md`.

## Licença e créditos

GPL-3.0-or-later, igual ao Eden (`LICENSE.txt`). Se distribuir o app, distribua também o código-fonte.

- [Eden](https://git.eden-emu.dev/eden-emu/eden): o emulador.
- [JulianDr14/eden-xbox](https://github.com/JulianDr14/eden-xbox): o port para Xbox, renderizador D3D12 e front-end.
- [juanresendiz813/eden-xbox](https://github.com/juanresendiz813/eden-xbox): a base UWP original.
- Mesa (`spirv_to_dxil`); yuzu, Ryujinx, Sudachi, Citron, Torzu, Suyu e Ryubing.

O HueHue Switch é independente: não é afiliado ao Eden, à Nintendo nem à Microsoft.
Não reporte problemas deste fork ao projeto Eden.
