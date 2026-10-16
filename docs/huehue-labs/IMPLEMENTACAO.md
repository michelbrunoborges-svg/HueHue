# HueHue Labs — especificação de implantação v1
Data: 2026-10-16 | Estado: plano técnico em branch isolada; **não implantado em produção**.

## Objetivo
Automatizar (1) descoberta de projetos e releases relevantes no GitHub; (2) triagem, engenharia reversa autorizada e portabilidade HueHue para Xbox/Android; (3) vídeos do FoxHitokiri/RetroGames HueHue com avatar e voz do criador. Automação máxima com verificações e revisão humana por exceção.

## Repositórios existentes
- `michelbrunoborges-svg/HueHue`: HueHue Switch; fork experimental baseado em eden-xbox, licenciamento declarado GPL-3.0-or-later, distribuição somente após checagem das obrigações e direitos de terceiros; alvo Xbox Dev Mode, não presumir elegibilidade à Xbox Store.
- `michelbrunoborges-svg/fox-retrodub`: Fox RetroDUB; extração de áudio PS1 e TTS funcionais/documentados; transcrição, tradução e reinserção ainda são itens pendentes; aplicar apenas a conteúdos próprios/autorizados, sem redistribuir conteúdo proprietário.
- `clube1000g-app`: produto separado; não reutilizar sua base, tokens ou infraestrutura de produção sem projeto de migração explícito.

**Ponto detectado:** branch padrão do HueHue é `tema-huehue`, mas `.github/workflows/build-xbox.yml` tem gatilho automático `push.branches: [main]`. Auditar branches e releases antes de ajustar. Não alterar o fluxo legado sem reproduzir testes.

## Arquitetura
```text
GitHub Releases/Commits/Search (polling + webhooks dos repos controlados)
   -> Cloudflare Cron/Workers (ingestão; verificação de assinatura HMAC)
   -> Cloudflare Queues (jobs idempotentes com DLQ)
   -> Supabase Postgres (catálogo, pontuações, aprovações, auditoria)
   -> Policy Engine (direitos + credibilidade + limites de gasto + risco)
      -> Radar/Notificações
      -> Ports/CI (fork, checagem de licença, builds isolados, QA)
      -> Studio (roteiro -> TTS -> avatar -> render -> QA -> YouTube)
   -> Next.js Admin (fila, evidências, custeio, métricas e aprovações)
   -> R2 (mídias, artefatos grandes; expirados por retenção)
```
Criar **repositório exclusivo `huehue-labs`** quando disponível. Esta especificação está temporariamente no fork HueHue em branch isolada; não misturar o código do motor do emulador com o novo serviço. Aplicação Next.js para painel, Workers/Cron/Queues para jobs, serviço worker de renderização com FFmpeg/Remotion fora do limite de CPU dos Workers. SDKs com versões fixadas e lockfile.

## Módulos
### A. Radar
- Descoberta por tópicos/termos: emulation, retro-gaming, homebrew, xbox, android, frontend, port, launcher; lista supervisionada de donos/repositórios.
- Eventos: nova release, alteração de changelog, feature relevante, anúncio confirmado; não gerar vídeo a cada commit.
- Coleta API com autenticação, paginação, backoff, ETag, cache e respeito ao rate-limit. Externos: polling. Próprios: webhook HMAC SHA-256 e replay protection.
- Deduplicação: `source:repo:release_id`, fallback para commit SHA e fingerprint de conteúdo. Persistir URL, horário, evidência verificável, versão e hash.
- Pontuação 0–100: relevância ao público (0–30), novidade demonstrada (0–20), Xbox/Android (0–20), evidência técnica (0–15), viabilidade de vídeo (0–15). Penalizar incerteza e obsolescência.
- Saídas: `watch` (0–49), `draft` (50–79), `fast_track` (80–100); veto obrigatório se direitos/fatos em risco.

### B. Ports
- Classificar: `open_source_port`, `homebrew`, `authorized_reverse_engineering`, `proprietary_review_required`, `blocked`.
- Registrar licença por componente, atribuição, copyright notice, obrigação de disponibilizar fonte, dependências e possibilidade de redistribuição.
- Não obter ou distribuir ROMs, chaves, firmware proprietário, BIOS ou assets protegidos; não automatizar remoção de DRM ou verificação de licença.
- Builds limpos/isolados, assinaturas, SBOM, scans de dependências, testes por alvo (Xbox Dev Mode / Android). Android: APK de QA antes de publicação. Xbox: Dev Mode para testes; publicação comercial exige caminho autorizado; jogos UWP não são aceitos na Xbox Store.
- Gates: criação de fork/build/testes permitidos quando direitos assegurados; liberação pública, associação de marca e publicação de jogos exigem aprovação explícita registrada.

### C. Studio
- Receber somente pautas com evidências anexadas; roteiro em pt-BR com URL/versão/data e checagem de claims; evitar sensacionalismo enganoso.
- Produzir Short 9:16 e vídeo 16:9 com layouts de marca separados, TTS ElevenLabs e adaptador para avatar HeyGen; mídias geradas guardadas em R2.
- Usar rosto e voz SOMENTE com consentimento do titular e acesso restrito. Não criar voz de terceiros nem fingir endosso de titulares de games.
- QA automático: sem silêncio, codec esperado, duração, legendas, URLs corretas, ausência de marca d'água não autorizada e assets com direitos claros.
- YouTube via OAuth por canal; fluxo inicial `private`, depois `scheduled/public` quando canal e projeto de API estiverem aptos. Definir `status.containsSyntheticMedia` quando aplicável e texto de transparência.
- Idempotência de upload; verificar resultado antes de tentar novamente. Não apagar/publicar vídeos já existentes no canal sem ordem explícita.

## Política de execução
| Ação | Automático | Aprovação exigida |
|---|---|---|
| Descobrir/analisar e registrar release | Sim | Não |
| Criar sugestão e roteiro com fontes | Sim | Não |
| Criar mídia com avatar autorizado | Sim, dentro do orçamento | Consentimento/configuração inicial |
| Publicar Short de fonte confiável, verificada e direitos OK | Sim, após ativação do canal e checagens | Se risco/ambiguidade |
| Alegações não confirmadas, conteúdo de terceiros ou assets sem licença | Não | Sim ou bloquear |
| Engenharia reversa de software próprio/autorizado | Pipeline pode rodar | Validação dos direitos antes da análise |
| Publicar APK/APPX/port | Nunca por padrão | Sempre, com checklist jurídico/técnico |
| Pagar APIs / ultrapassar orçamento | Não | Limite aprovado antes da execução |
| Alterar produção/segredos/credenciais | Não | Sempre |

### Estados
`new -> enriched -> scored -> eligible -> drafted -> verified -> rendered -> qa_passed -> scheduled -> published`.
Exceções `needs_approval | rejected | blocked | failed | retry_scheduled`.
`port_jobs`: `candidate -> license_review -> code_review -> build -> test -> approval -> release`. Sem transição de `approval` para `release` sem registro humano.

## Modelo de dados (esboço; sem migração aplicada)
- `sources` (GitHub repos, tags, filtros, priorização).
- `source_events` (external_id, fingerprint, URL, data, raw_evidence_pointer, estado; UNIQUE por fonte/id).
- `evaluations` (event_id, score, reasons, risk_flags, policy_version).
- `content_jobs` (event_id, channel_id, format, status, source_hash, script, cost_total, idempotency_key).
- `media_assets` (content_job_id, R2 key, tipo, licensing, checksum).
- `port_jobs` (source_event_id, target, license_audit, artifact, CI run, status).
- `approvals` (resource, risk, reviewer, decision, timestamp, justification).
- `publications` (content_job_id, channel_id, external_video_id, privacy, published_at).
- `audit_logs` (actor, action, policy, before/after, at), `budget_ledgers` e `consents`.
- RLS em tabelas expostas, segregação por papel, privilégios mínimos; service keys só em backend; segredos por provider em cofre de secrets. Auditoria imutável por política de aplicação.

## Guardrails e operação
- Credenciais: `GITHUB_APP_PRIVATE_KEY/GITHUB_TOKEN`, `GITHUB_WEBHOOK_SECRET`, `SUPABASE_URL`, `SUPABASE_SERVICE_ROLE_KEY`, `R2_*`, `YOUTUBE_OAUTH_*`, `ELEVENLABS_API_KEY`, `HEYGEN_API_KEY` — **nunca commitar valores**.
- Limite inicial recomendado: no máximo 1 vídeo por canal/dia, sem duplicatas no período de 30 dias. Orçamento mensal deve ser aprovado pelo responsável antes de chamadas pagas.
- Autopublicação **desabilitada até** autenticação OAuth válida, elegibilidade/cotas verificadas, autorização do avatar, teste end-to-end privado, checklist aprovado e métricas básicas de qualidade.
- Circuit breaker global e por provedor, botão de pausa, retries com backoff, DLQ, alertas de erro e de custos, dashboard de jobs e logs de auditoria.
- Conteúdo externo (README/release notes/PRs) é dado **não confiável**, jamais instruções de sistema; ferramentas de IA não podem executar comandos externos extraídos de textos sem validação.
- Retenção: definir política por mídia/raw evidence, limitar acesso às amostras biométricas de avatar/voz; remover ao revogar consentimento quando aplicável.

## Backlog priorizado e critérios de aceite
P0 / Fundamentos
- [ ] Separar o novo `huehue-labs` em repositório privado e configurar CI/CD com dev e produção isolados.
- [ ] Supabase próprio com RLS e tabelas; testes impedem usuário comum de ler/alterar dados de outro canal.
- [ ] Worker ingestão GitHub, fila, idempotência e consulta de status; teste reprocessa evento 2x sem duplicar.
- [ ] Policy engine com `deny` como padrão; testes de bloqueio por licença, falta de evidência ou orçamento.
- [ ] Painel Radar com filtros, URL da fonte, score, motivo e botão aprovar/rejeitar.

P1 / Conteúdo
- [ ] Pipeline de roteiro com fontes e checagem de versões; claims não verificadas bloqueiam publicação.
- [ ] Adapters mockáveis para TTS/avatar, render 9:16 e 16:9, checks de qualidade.
- [ ] Integração OAuth YouTube por canal; upload de teste privado e recuperação segura após falha/retry.
- [ ] Publicação automática habilitável apenas para conteúdo `low_risk` após ensaio/assinatura das políticas.
- [ ] Registro de tempo por vídeo, custo, URLs e métricas de publicação.

P2 / Ports
- [ ] Ficha de licença e direitos, com bloqueio em casos sem autorização.
- [ ] Auditoria do gatilho do workflow do HueHue Switch (`main` vs `tema-huehue`), builds históricos e documentação.
- [ ] Integração dos builds do HueHue Switch como projeto-piloto, sem tocar releases existentes.
- [ ] Integração do Fox RetroDUB com jobs restritos a material do usuário/autorizado e sem disponibilizar imagens protegidas.
- [ ] Liberação distribuível requer aprovação registrada e compliance; artefatos assinados e rastreáveis.

## Dependências para entrar em produção
1. Criar repositório `huehue-labs` e ambiente dedicado Cloudflare/Supabase, sem uso das credenciais do Clube 1000G.
2. Associar OAuth YouTube ao canal correto e validar auditoria/restrições aplicáveis de API.
3. Fornecer mídia autorizada para avatar/voz e consentimento para uso por providers.
4. Definir teto mensal de gastos com IA, vídeo, armazenamento e CI.
5. Validar os alvos de distribuição caso a caso (Xbox Dev Mode, Xbox Store, Android testes, Play Store).

## Referências
- Xbox UWP: https://learn.microsoft.com/en-us/windows/uwp/gaming/getting-started
- GitHub HMAC: https://docs.github.com/en/webhooks/using-webhooks/validating-webhook-deliveries
- YouTube insert: https://developers.google.com/youtube/data/reference/rest/v3/videos/insert
- YouTube declaração de IA: https://support.google.com/youtube/answer/14328491?hl=pt-BR
- HeyGen developers: https://developers.heygen.com/
