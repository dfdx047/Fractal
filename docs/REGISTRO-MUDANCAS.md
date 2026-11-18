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
- **Em teste:** Spider-Man 2 no RP5 (limitado pela CPU) e AC IV no Odin 3, com consumo.

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
