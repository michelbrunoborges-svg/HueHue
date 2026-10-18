# NS HUEHUE — jogar online entre consoles

## Como funciona

O Eden já traz o multiplayer por **salas** do yuzu: ele leva a comunicação **local sem fio** do
Switch (LDN) pela internet. Dois consoles com o NS HUEHUE entram na mesma sala de um servidor
de salas do Eden e o jogo enxerga o outro como se estivesse do lado.

- Funciona nos jogos que têm modo **local sem fio** (Mario Kart 8 Deluxe, Smash Bros., Monster
  Hunter, Pokémon, Splatoon em modo local etc.). O online oficial da Nintendo **não** funciona.
- Os dois consoles precisam da **mesma versão** do jogo (mesmo update).
- A latência depende da distância de cada console até o servidor.

## No emulador

Configurações › **Online**:

| Linha | O que é |
|---|---|
| Jogar online | Liga/desliga. Ligado, o jogo entra na sala antes de começar. |
| Servidor de salas | Nome ou IP do servidor (sem `http`). |
| Porta | Padrão `24872`. |
| Senha da sala | Vazio para sala aberta. |

O apelido na sala é o **Nome de usuário** de Configurações › Sistema. Fica salvo em
`LocalState/ns_online.cfg`.

Depois é só abrir o jogo nos dois consoles e entrar no modo **local sem fio** do próprio jogo
(um cria a partida, o outro procura).

## O servidor de salas

Precisa de um servidor ligado 24h com a porta **UDP 24872** aberta. Duas opções:

1. **VPS própria (recomendado):** uma VPS Linux pequena (1 vCPU, 1 GB) dá conta de várias
   salas. Custo típico de R$25–40/mês. Com Docker:

   ```bash
   git clone <este repositório> ns-huehue && cd ns-huehue
   docker build -f tools/room-server/Dockerfile -t ns-huehue-room .
   docker run -d --name ns-huehue-room --restart unless-stopped -p 24872:24872/udp \
     ns-huehue-room --room-name "NS HUEHUE" --port 24872 --max-members 8 --password "troque-esta-senha"
   ```

   Libere a porta UDP 24872 no firewall da VPS. No emulador, use o IP da VPS e a senha.

2. **Servidor público da comunidade:** qualquer servidor de salas do Eden/yuzu compatível
   serve; basta o endereço e a porta. Sem garantia de disponibilidade.

## Estado

- Cliente (tela, configuração, entrada na sala no boot): implementado.
- Servidor: Dockerfile pronto; **não testado** neste ambiente (sem Docker aqui).
- Teste real: **pendente** — precisa de dois consoles e um servidor no ar.
