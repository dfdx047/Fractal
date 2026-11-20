# Plano para a v0.3 (ponto de partida)

Estado em 20/11/2026, dia do lançamento da v0.2-beta. Este arquivo junta o que ficou aberto, o que já foi descoberto
sobre cada problema e os ramos de teste que existem, para planejar o próximo update sem ter de reconstruir a história.
O que entrou na v0.2 está no [CHANGELOG](../CHANGELOG.md) e nas [notas da versão](release-notes/v0.2-beta.md).

## 1. Problemas abertos, do mais visível ao menos

### Watch Dogs: sala inicial clara demais (superexposição) no Android

- O certo é a sala escura (como no console). No Odin 3 (Turnip v39patched) sai escura em cerca de 40% das partidas,
  com a mesma build. A divergência acontece entre 450 e 480 s da cena `wd_m900`.
- Descartado: corrida entre núcleos (single-core também varia), NaN, compilação assíncrona, readbacks (a contagem não se
  correlaciona), a cópia de autodependência (`selfdep_copy=0` deu 5 escuras seguidas, mas desligar a cópia nos 3 shaders
  que a usam deu 2 claras: foi acaso).
- Pista nova: os resultados vêm em **sequências** (6 escuras seguidas de madrugada, depois 3 claras, 1 escura, 4 claras...).
  Algo persiste entre uma partida e outra.
- **Temperatura descartada** (20/11, build final 3c1ce92c, save apagado a cada partida): 3 claras e 1 escura com bateria
  a 33-34 °C, pele a 46-50 °C, GPU 660 MHz e consumo iguais nas 4.
- **Pista forte: o cache de shaders** (20/11, 15 partidas alternadas): com os 3 arquivos do WD apagados antes de cada
  partida (`transferable/*_shaders.bin`, `*_vkpipeline.bin`, `precompiled/*_spirv.bin`) foram 7 escuras em 7; com o
  cache mantido, 5 escuras e 2 claras. Próximo passo: separar qual dos 3 arquivos decide (apagar um de cada vez) e
  comparar o SPIR-V do cache com o recompilado para os shaders da passada de luz/exposição.
- **Issue #8** (água branca no BotW, Odin 2 Portal, "às vezes corrige ao reabrir") parece o mesmo problema: pedimos ao
  usuário para testar sem o cache do BotW.
- Ramo de teste `wd-selfdep` (92ad50ef, tunables `selfdep_copy_skip1..3`): pode ser apagado se a pista acima vingar.

### Mario Kart 8 no Mi A3 (Adreno 610)

- Driver do sistema: GPU PAGE FAULT no endereço 0x80 (bloco UCHE/HLSQ, leitura) cerca de 75 ms depois da primeira
  amostragem de sombra (shader ba31fd8f161b3d7e, unidade 1: D16 1536x1536, compare, clamp to border, borda branca),
  depois SIGSEGV no blob ao carregar a pista. Pré-existente (também na build do YouTuber 46d21ff0).
- Turnip T19: reinicia o celular (violação de XPU/TrustZone). **Não rodar sem avisar o usuário.**
- Descartados: ASTC, streamout adiado, lazy_zero, `robust_device`, os 7 padrões próprios do Fractal,
  `unfilterable_nearest`, `border_to_edge` (ramo `a3-border` 3df3d09e), pular os shaders de sombra. Bissecção por pular
  draws não é limpa (outros crashes e lmkd).
- Próximo: logar descritores com VK_NULL_HANDLE ou tamanho 0 no A3; comparar com o RP5 (Adreno 650 roda normal).

### Outros jogos

| Jogo | Sintoma | O que se sabe |
|---|---|---|
| AC III | chão azul no multi-core | o jogo não envia o draw do terreno; single-core corrige. Diagnóstico local fora do git (`diffs/2026-11-18_ac3_diag_*`) |
| Splinter Cell Blacklist | cidade invisível | não investigado a fundo |
| FIFA 13 | gramado preto (núcleo, não driver) | Cemu upstream #1907; olhar o readback 1x8 R32F e os formatos 124/126 sem filtro linear |
| Binding of Isaac Rebirth (US) | imagem preta | erros de command buffer zerados, falta ver o que o jogo apresenta |
| Batman Arkham Origins (Odin 3) | SIGSEGV raro (1 de 3) em CreateView | view sobrevive à textura (use-after-free na ligação de texturas) |
| Cars 3 (RP5 sistema) | crash raro em r_renderMprimDrawableBatch | suspeita de occlusion queries no blob Qualcomm |
| NFS Most Wanted U (POCO PanVK) | crash raro aos ~170 s (bctrl para 0) | só no PanVK |
| Teslagrad (Mi A3) | ~11 fps | um intervalo contíguo de 955 KB de PPC é recompilado inteiro a cada entrada nova |
| Watch Dogs (Odin 3, driver do sistema) | crash no boot às vezes | pipeline só de vértice com streamout estoura a memória do compilador Qualcomm; Turnip OK |

## 2. Ideias medidas e adiadas

- **Economia de energia no Snapdragon 8 Elite:** emulador sem os 2 núcleos prime = AC IV 60 fps de 12,2 para 7,9 W,
  WD de 8,4 para 5,2 W sem perder fps quando o jogo está no limite. Fazer modo automático (prime só quando o fps cai
  abaixo do alvo) ou opção "economia".
- **ADPF** (PerformanceHintManager): pronto e desligado por padrão (`adpf=0`); custou fps no Bayonetta 2 do POCO.
- **ETC2/EAC** e **decodificação de texturas em paralelo**: prontos e desligados (sem ganho; ETC custa CPU na Mali).
- **Decodificação de BCn por compute na Mali**, BC4/BC5 em R8/RG8, orçamento de memória pela extensão memory budget.
- **Cache de pipelines por família de GPU:** pipelines de cache gerado em Adreno 6xx derrubam o compilador da Adreno 8xx
  (Turnip Gen8 e blob). Marcar a família no `vkpipeline.bin` ou pular entradas de outra família.
- **Correção do FileCache no Cemu upstream** (9b3dde81): o bug também existe lá; considerar PR.
- **fast-udiv** (ramo 211df809) e GMEM no Turnip: medidos, neutros. Não retomar.

## 3. Ramos locais (sem push)

| Ramo | Conteúdo | Destino |
|---|---|---|
| `wd-selfdep` | pular a cópia de autodependência por shader | apagar se não servir |
| `a3-border` | `border_to_edge` | apagar (não resolveu o MK8) |
| `v02-lazyoff` | lazy_zero desligado por padrão | descartado (a causa do FF era outra) |
| `fast-udiv`, `teste-*`, `so-defer`, `lazy-zero`, `integ-next` | experimentos já integrados ou descartados | limpar |

## 4. Ferramentas e lições do ciclo da v0.2

- `scripts/regressao.py` agora marca **FREEZE** (evento "no frame for 30 s"). Antes só contava crashes, e o travamento
  do FF Showdown passou por três regressões.
- `ng3_var.sh` passa variantes de `perf/tuning.txt` (várias chaves separadas por `\n`); valores negativos funcionam.
- Medir jogos com o save do usuário, não só a cena de abertura (BotW caiu para 11 fps no mundo aberto numa build de
  teste e a cena do santuário não mostrava).
- O hash do app só muda quando o CMake reconfigura: `touch CMakeLists.txt` antes de toda build publicada.
- Mesmo motor, mesmo bug: Hot Wheels e FF Showdown (Firebrand) tinham as mesmas corridas em `wuGraphicsCore`. Ao corrigir
  um jogo por title fix, procurar o mesmo código nos outros jogos do mesmo estúdio.
- Aparelhos: RP5 fica no perfil travado; Odin 2 Mini é da sessão do Forza; o celular pessoal do usuário (192.168.1.218)
  nunca entra nos testes.
