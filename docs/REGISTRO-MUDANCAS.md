# Registro de mudanças com risco de regressão

Diário das mudanças grandes ou arriscadas, em ordem: quando, o quê, como foi testado e o que aconteceu. Quando aparecer
uma regressão, comece por aqui: a data e o commit dizem o que entrou entre a última build boa e a ruim. Correções de jogos
com o problema de cada uma estão no [roadmap](ROADMAP.pt-BR.md) e no [changelog](../CHANGELOG.md); aqui ficam as mudanças
que podem afetar todos os jogos.

Cada entrada: data · commits · o que muda · como foi testado (aparelhos, drivers, jogos) · resultado · como voltar atrás.

Aparelhos: RP5 = Retroid Pocket 5 (SD 865, Adreno 650), POCO = POCO X6 Pro (Dimensity 8300 Ultra, Mali-G615), A3 = Xiaomi
Mi A3 (SD 665, Adreno 610), Odin 3 = AYN Odin 3 (8 Elite, Adreno 830).

## 2026-11-18

### Extensões ARMv8 e NEON

- **Commits:** `b1078675` (detecção em tempo de execução), `852801bb` (AES), `64efc3e5` (CRC32), `bd6af3dc` (máximo de
  índices), `c4e6b0aa` (BCn com paleta por bloco).
- **O que muda:** a detecção via `getauxval(AT_HWCAP)` escolhe o caminho em tempo de execução, e a build continua
  ARMv8.0-A. O AES entra nas leituras de disco criptografadas (WUD/WUX, NUS). O CRC32 entra na carga de RPX/RPL. O máximo
  de índices com NEON vale para todo draw indexado. A decodificação BC1-BC5 na CPU vale para as GPUs sem BCn (Mali).
- **Testado:** `tools/neon_tests` compara cada caminho NEON com o código escalar bit a bit. Regressão nos 3 aparelhos
  (system, Turnip T19, PanVK).
- **Resultado:** nenhuma diferença de imagem nem crash novo. No POCO: AES de 32 MB/s para 4,1 GB/s, BC1 1024x1024 de 4,0
  para 1,5 ms. O resto dos números está no CHANGELOG.
- **Voltar atrás:** `git revert` do commit da área. Cada um é independente, menos `b1078675`, que os outros usam.

### Limpeza: só Android

- **Commits:** merge `d16332d4`, com `9aa94b36` (interface wx), `ee6e63b1` (OpenGL/Metal), `d07f65ea` (JIT x86-64),
  `54b44032` (Windows/macOS/Linux desktop), `4d4403c3` (entrada/áudio desktop, Discord), `4b0f2868` (build só Android),
  `56d83cd7` e `054ddf0f` (funções sem uso), `bc69967a` (símbolos de terceiros fora dos exports), `10bd5abd` (corte de
  recursos, só arm64), `5e0236a6` (graphic packs sem Ktor), `9cb6d054` (entrada: contagem de assinantes das teclas).
- **O que muda:** 572 arquivos e cerca de 103 mil linhas a menos. A biblioteca nativa caiu de 22,7 para 18,4 MB. Recursos
  sem uso saem no build (`isShrinkResources`).
- **Testado:**
  - Interface no RP5: biblioteca, temas, drivers online, download real de graphic packs (180 packs, v991) e shaders da
    comunidade.
  - Regressão: RP5 com 13 jogos (system e T19), POCO com 12 jogos (system, PanVK) e mais 5 jogos novos, A3 com 4 jogos.
- **Resultado:** fps igual ou dentro da variação normal, sem crash novo.
- **Risco que sobra:** recurso carregado só pelo nome (o código não usa `getIdentifier`). Se algum tema, som ou imagem
  sumir, olhe `src/android/app/src/main/res/raw/keep.xml`.
- **Voltar atrás:** reverter o merge `d16332d4` com `-m 1`.

### Correções gerais

- `226c1868`: o workaround do Binding of Isaac passa a valer também para o RPX da versão USA. Muda só esse jogo.
- `a0b305c5`: sem logic op em drivers que não têm o recurso `logicOp` (os drivers da Adreno e da Mali). Muda os pipelines
  com raster op diferente de COPY nesses drivers. Turnip e PanVK não mudam.

### Afinidade de núcleos (ajustes de teste)

- **Commit:** `2c2431f1`. Ajustes `cpu_mask_ppc` / `cpu_mask_ppc<N>`, `cpu_mask_latte` e `cpu_mask_jit` em
  `perf/tuning.txt` (máscara decimal de CPUs). Desligados por padrão: sem o arquivo nada muda.
- **POCO, AC III, driver da Mali, 150-290 s:**

  | Variação | fps | CPU |
  |---|---|---|
  | padrão | 34,1 / 34,3 | 273% |
  | recompilador nos núcleos de 2,2 GHz (3 variações) | 20,4-22,1 | ~400% |
  | CPU emulada e GPU nos núcleos 4-7, recompilador livre | 34,3 | 270% |
  | GPU no núcleo 7 | 34,4 | 273% |

  Prender o recompilador nos núcleos pequenos faz mais código cair no interpretador. Fixar o resto não muda nada,
  porque o Android já põe essas threads nos núcleos grandes. Conclusão: no POCO fica o padrão.
- **RP5, Ninja Gaiden 3 (limitado a 60 fps):** CPU emulada e GPU nos núcleos 4-7 gastam 194% de CPU contra 223% no
  padrão, no mesmo fps. CPU emulada em 3 núcleos (4-6) com a GPU no 7 cai para 23 fps.
- **RP5, Spider-Man 2 (limitado pela CPU, 450-590 s):** padrão 34,8 fps / 263% de CPU; CPU emulada e GPU nos núcleos
  4-7: 38,2 / 192%; só a GPU no 7: 34,7 / 249%.

### Núcleos grandes por padrão

- **Commit:** `683c6772`. As threads dos núcleos emulados e a da GPU (LatteThread) evitam o cluster pequeno: núcleos com
  menos de 75% do maior `cpu_capacity` (regra do ARMSX3). Sem cluster pequeno (8 Elite) nada é fixado. `big_cores=0` em
  `perf/tuning.txt` desliga. O recompilador, o áudio e o resto continuam livres.
- **Testado (build `2026-11-18_fractal-bigcores.apk`, A/B no mesmo run):**
  - RP5 Spider-Man 2: 40,3 fps / 193% contra 34,9 / 262% com `big_cores=0`.
  - Mi A3 Super Mario 3D World (T19): 22,2 nos dois.
  - POCO: neutro (rodada 2 da afinidade). Odin 3: máscara 0, nada muda.
  - Regressão RP5 13 jogos system + T19 com esta build (v02bc_rp5_*).
- **Risco:** aparelho com só 2 núcleos grandes (ex.: alguns Snapdragon 6xx/7xx 2+6) põe 3 núcleos emulados e a GPU em 2
  CPUs. Se aparecer queda nesses, o limite deve exigir pelo menos 4 núcleos grandes.
- **Voltar atrás:** `big_cores=0`, ou reverter o commit.

### Ajustes de diagnóstico de memória (desligados por padrão)

- **Commit:** `f4d8191f`. `buffer_cache_mb` (cache de buffers da GPU, padrão 164), `tex_chunk_mb` (blocos do heap de
  texturas, padrão 128) e `log_tex_mem=1` (registra cada bloco do heap de texturas, cada imagem de 8 MB ou mais e o total a
  cada 64 MB). Sem `perf/tuning.txt` nada muda; o único custo fixo é um campo de 8 bytes por textura.
- **Achado com eles (Mi A3, MK8, driver do sistema):** o blob da Qualcomm não anuncia BC1-BC5, então cada textura BC vira
  RGBA8 (4-8x maior). Na largada são 833 MB de texturas, e o lmkd mata o jogo. `buffer_cache_mb=96` não muda nada.

### Cache de buffers da GPU de 256 MB (6 GB de RAM ou mais)

- **Commit:** `f3f03341`. O cache de buffers passa de 164 para 256 MB quando o aparelho tem 5 GB ou mais de RAM
  reportada (os de "6 GB" mostram ~5,5). Abaixo disso fica em 164. `buffer_cache_mb` em `perf/tuning.txt` continua valendo.
- **Por quê:** Cars 3 no RP5 às vezes não achava espaço para um intervalo de streamout de 34 MB ("Failed to free enough
  memory in GPU buffer") e parava de desenhar: 4 de 14 runs (system e T19).
- **Testado:** RP5 com `2026-11-18_fractal-bufcache256.apk`: Cars 3 0 de 4 runs com o erro; regressão de 13 jogos no
  system e no T19 (`v02b_rp5_*`) sem crash e com fps iguais aos da candidata.
- **Risco:** +92 MB de memória da GPU nos aparelhos de 6 GB ou mais. Mi A3 (4 GB) e outros abaixo de 5 GB não mudam.
- **Voltar atrás:** `buffer_cache_mb=164`, ou reverter o commit.

## 2026-11-19

### BotW: cópia de autodependência só para shaders raros

- **Commit:** `4edbc727`.
- **Problema:** BotW no Odin 3 (v39patched, pack 1080p, sombras 200%, FPS++) caiu de 39-43 fps (v0.1, e44b3323) para
  11-19 fps desde `70eb6796` (16/11). Aquele commit copia o anexo de cor inteiro antes de cada draw que o amostra, também
  quando há `VK_EXT_attachment_feedback_loop_layout` (pontos do Paper Mario no Turnip/PanVK). O BotW faz ~330 desses por
  quadro no mundo aberto: 543 passes/quadro contra 216, GPU 84 ms/quadro a 1016 MHz contra 20 ms a 661 MHz.
- **Correção:** com feedback loop disponível, um pixel shader visto mais de `selfdep_copy_max_per_frame` (padrão 8) vezes
  num quadro passa a usar o feedback loop pelo resto da sessão (evento `selfdep_feedback` no events.txt). Paper Mario
  (16/11, PanVK): 1 cópia por quadro, continua copiando.
- **Testado (Odin 3, save do usuário no Platô, 300-370 s):**

  | Build | fps | passes/quadro | GPU/quadro |
  |---|---|---|---|
  | v0.1 e44b3323 | 39,4 | 227 | 20,1 ms |
  | 4719a412 | 10,8-10,9 | 543 | 84 ms |
  | 4719a412 + `selfdep_copy=0` | 40,5 | 216 | 19,6 ms |
  | correção (2 runs) | 35,3 / 39,1 (média 39,6 / 42,4) | 224 | 20-23 ms |

  Mi A3 (system e T19): SM3DW 35,8 / 28,7, Captain Toad 27,1, Shovel Knight 60,0, iguais à candidata.

### PGO e ADPF

- **Commit:** `0893c8b5`.
- **PGO:** `FRACTAL_PGO` no CMake (`generate`, caminho de um `.profdata`, ou vazio) e no Gradle (variável de ambiente;
  sem ela usa `src/android/pgo/fractal.profdata` se existir). A build instrumentada grava `files/pgo/*.profraw` a cada
  30 s. Perfil atual: Odin 3 (8 jogos, v39patched), POCO (8 jogos, driver da Mali), Mi A3 (3 jogos, system e T19),
  merge com o `llvm-profdata` do NDK 29. Para refazer: build com `FRACTAL_PGO=generate`, rodar os jogos, puxar os
  `.profraw`, `llvm-profdata merge -o src/android/pgo/fractal.profdata *.profraw`.
- **ADPF (`util/helpers/PerformanceHint.*`):** sessão de dica com os 3 núcleos emulados e a LatteThread; o trabalho
  de cada quadro é o intervalo menos o tempo ocioso da thread mais ocupada (laço ocioso do núcleo, espera de comandos e
  de fence da GPU). Desligado por padrão (`adpf=1` liga); com ele desligado só sobra a contagem de tempo ocioso.
- **Testado (A/B, mesma cena, 19/11 madrugada):**

  | Aparelho, jogo | normal, ADPF ligado | normal, ADPF desligado | PGO |
  |---|---|---|---|
  | Odin 3, WD (150-900 s) | 53,6 fps / 9,5 W | 51,4-51,8 fps / 10,1 W | 56,1 fps |
  | Odin 3, AC IV (1% low) | 46-50 | 57 | - |
  | POCO, Bayonetta 2 | 26,3 | 33,2 | 33,5 |
  | POCO, AC III | 32,3 | 33,2 | 33,5 |
  | RP5, Spider-Man 2 / NG3 | 39,2-39,7 / 34,5 | 39,7-39,8 / 34,2-35,0 | 39,8-39,9 / 34,2-34,3 |
  | Mi A3, SM3DW / Captain Toad | 32,5-33,0 / 26,6-27,2 | 32,5-33,1 / 27,2 | 32,6 / 26,9 |

  Sem crash em nenhuma das builds (coleta e A/B). Coluna PGO do RP5 e do A3: perfil só do Odin 3.
- **Risco:** o perfil envelhece conforme o código muda (funções novas ficam sem perfil, sem erro). Refazer antes de
  cada versão grande.
- **Voltar atrás:** `FRACTAL_PGO=off` na build, ou apagar `src/android/pgo/fractal.profdata`.

## 2026-11-18 (cont.)

### Tentativas descartadas

- **Espera ociosa do núcleo 1 sem travas**
  (`Fractal-trabalho/diffs/2026-11-18_idle_spin_sem_travas.diff`): o perfil do POCO mostrou cerca de 55% do tempo do
  laço ocioso em `clock_gettime` e nas travas do timer e do áudio. A versão sem travas não deu fps no RP5, no POCO nem no
  A3. Ficou de fora.
- **FIFA 13, mistura de áudio com 0 canais de saída** (correção de título em `rpl.cpp`, não commitada): tirou o crash
  aos 12 s no RP5, mas o jogo ainda crasha por volta de 100 s na mesma thread de áudio (4 de 6 runs). A build sem a correção
  crasha aos 12-16 s em 4 de 6 runs (system 1 de 2 válidos, T19 em sysmem 3 de 3). Ou seja, a correção adia o crash e não o
  causa. Guardada em `Fractal-trabalho/diffs/2026-11-18_fifa13_audio_0canais.diff`. Single-core não serve: o jogo
  fica na tela preta do boot (sem crash só porque não chega ao áudio).

### Como os testes rodam

- Os scripts de regressão gravam um perfil próprio do jogo (`gameProfiles/<tid>.ini`) com o driver. Esse perfil
  substitui o perfil padrão de `bin/gameProfiles/default`. Por isso, configurações do padrão, como `turnipSysmem = false`
  no FIFA 13, não valem no teste. Quando a configuração do padrão importar, passe-a em `EXTRA_PROFILE`.
- Dois aparelhos não podem usar o mesmo nome de teste no mesmo dia: a pasta é a mesma e um sobrescreve o outro.
- Um run "sem crash" não basta: confira os prints. O FIFA em single-core deu 60 fps sem crash com a tela toda preta.
- Depois de copiar um `.wua`, confira o `md5sum`. Uma cópia feita com pouco espaço saiu corrompida sem erro do adb.
