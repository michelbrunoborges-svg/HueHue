# NS HUEHUE

Emulador de Nintendo Switch para **Xbox Series X|S em Dev Mode**, com a cara do HueHue,
interface em português do Brasil e temas próprios.

> **Experimental.** Muitos jogos rodam, alguns ainda não abrem, e a primeira passagem por uma
> área nova engasga enquanto os shaders são montados. O histórico de cada versão está em
> [`docs/ns-huehue/README.md`](docs/ns-huehue/README.md).

## O que tem

- **Biblioteca:** Jogos, Favoritos, Recentes e Traduções; capas; busca; opções por jogo (mods,
  cheats, atualizações, DLC, Pulse, controles e desempenho).
- **Desempenho e gráficos por jogo:** perfil, renderizador D3D12 ou OpenGL, resolução, ampliação
  (FSR 1), filtro anisotrópico, proporção, limite de FPS, pular quadros, shaders pendentes,
  precisão da GPU e da CPU, velocidade de 10% a 200% e opções avançadas de memória e texturas.
- **Menu do jogo** (View + Menu por 2 s): controles, mapeamento de botões, velocidade, volume até
  200%, silenciar, shaders pendentes e fechar o jogo.
- **Painel de desempenho:** FPS / ms, RAM usada / limite, GPU, JIT, EMU, outro, carga da GPU e da CPU,
  núcleos.
- **HD externo:** backup de saves, das configurações e dos caches de shaders (sincronizados ao abrir
  e fechar cada jogo).
- **Temas:** HueHue Grafite, Switch Claro, Switch Escuro, o pacote NS HUEHUE e outros pelo
  Importar temas (USB).

## Instalar no Xbox Series X|S

1. **Pré-requisitos:** console em **Dev Mode**, um PC na mesma rede e **suas próprias** chaves
   (`prod.keys`, `title.keys`), firmware e jogos, extraídos do **seu** Switch. Nada disso vem no
   pacote nem neste repositório, e nunca deve ser colocado aqui.
2. **Baixar:** na aba **Releases**, a versão mais recente: `NSHueHue-<versão>.appx` (o app),
   `Microsoft.VCLibs.x64.14.00.appx` (dependência obrigatória) e `NSHueHue-<versão>.cer` (certificado).
3. **Instalar:** no PC, abra o Device Portal do Xbox (`https://<ip-do-xbox>:11443`) → **Add** →
   `NSHueHue-<versão>.appx` → adicione o `Microsoft.VCLibs.x64.14.00.appx` como dependência → Start.
4. **Modo Game (obrigatório):** no Dev Home, selecione **NS HUEHUE** → View → View details →
   **App type: Game**.
5. **Chaves, firmware e jogos:** Configurações → Arquivos e USB, a partir de uma pasta do HD externo
   ou pendrive.

**Atualizar:** instale o `.appx` novo por cima, **sem desinstalar**: desinstalar apaga saves,
chaves e firmware do armazenamento interno.

**Se algo der errado:** pelo Device Portal, em `LocalState`, os arquivos `eden_uwp_diag.txt` e
`eden\log\eden_log.txt` dizem o que aconteceu.

## Build

Todo build roda no GitHub Actions (`.github/workflows/build-xbox.yml`, runner Windows com
Visual Studio 2022): compila, empacota, assina e guarda o `.appx`; o release sai pelo
`release-from-run.yml`.

## Créditos

Só os projetos cujo código ou dados estão no NS HUEHUE:

| Projeto | Responsável por |
|---|---|
| [Eden](https://git.eden-emu.dev/eden-emu/eden) | Núcleo do emulador: CPU, GPU e sistema do Switch |
| [Jadyx (eden-xbox)](https://github.com/JulianDr14/eden-xbox) | Port para Xbox, renderizador Direct3D 12, mods, cheats e Pulse |
| [eden-xbox](https://github.com/juanresendiz813/eden-xbox) | Base UWP original para o Xbox |
| [NXbox](https://github.com/NspxMiguel/NXbox) | Renderizador OpenGL pelo Mesa |
| [Mesa](https://gitlab.freedesktop.org/mesa/mesa) | Driver OpenGL d3d12 e tradução de shaders (`spirv_to_dxil`) |
| [mesa-uwp](https://github.com/aerisarn/mesa-uwp) | Mesa compilado para UWP (Xbox) |
| [FFmpeg](https://ffmpeg.org) | Vídeos dos jogos |
| [libarchive](https://github.com/libarchive/libarchive) | Arquivos `.zip`, `.7z` e `.rar` dos mods |
| [AMD FidelityFX FSR](https://github.com/GPUOpen-Effects/FidelityFX-FSR) | Ampliação da imagem FSR 1 |
| [nx-cheats-db](https://github.com/sthetix/nx-cheats-db) | Banco de cheats |
| [nx-links (CNX)](https://github.com/gamemoddesignbr/nx-links) | Índice de traduções e mods de fãs |
| [GameBanana](https://gamebanana.com) | Loja de mods |

## Licença

GPL-3.0-or-later (`LICENSE.txt`); cada componente mantém a sua licença. Se distribuir o app,
distribua também o código-fonte.

O NS HUEHUE é independente: não é afiliado à Nintendo nem à Microsoft. Não reporte problemas
deste projeto aos projetos acima.
