<p align="center">
  <a href="ROADMAP.md">English</a> · <b>Português (Brasil)</b> · <a href="ROADMAP.es.md">Español</a>
</p>

# Roadmap do Fractal

Situação: **v0.2-beta (em preparação)**, novembro de 2026; o [changelog](../CHANGELOG.md) (em inglês) lista as mudanças
de cada versão. Cada correção em *Feito* aponta para o commit que a fez, e os números vêm das medições escritas nesses
commits.

## De onde o Fractal vem

- **Base:** o port de Android do Cemu feito pelo SSimco (`android-port`, ponto do fork `8759323e`, 2026-04-27).
- **Cemu oficial incorporado:** os 220 commits do `main` do `cemu-project/Cemu` até `3b103161` (2026-11-04), no `7c02ce75`.
- **Trabalho do Fractal:** cerca de 240 commits desde 2026-11-08: interface, drivers, entrada, renderizador,
  recompilador, código ARM/NEON, correções de jogos e ferramentas de depuração.

Aparelhos de teste: **Retroid Pocket 5** (Snapdragon 865, Adreno 650), **POCO X6 Pro** (Dimensity 8300 Ultra,
Mali-G615 MC6) e **Xiaomi Mi A3** (Snapdragon 665, Adreno 610). Resultados anteriores também vêm do **Anbernic RG Cube**
(Unisoc T820, Mali-G57 MC4), que segue como referência para aparelhos Mali mais fracos.

---

## Feito

### Correções de jogos no núcleo compartilhado do emulador

Esses bugs estão em código que o Fractal divide com a versão de PC do Cemu, então não são exclusivos do Android.

| Jogo | O que estava errado | Correção | Commit |
|---|---|---|---|
| The Amazing Spider-Man | Nenhuma imagem em momento algum ("no display" na wiki de compatibilidade do Cemu). O jogo copia o quadro pronto para o scan buffer com `GX2CopySurface` em vez de `GX2CopyColorBufferToScanBuffer` | Cópias para as faixas do scan buffer da TV/GamePad são apresentadas. Jogável na build de PC do Fractal (abertura, título, capítulo 1) | [`79f2f691`](https://github.com/dfdx047/Fractal/commit/79f2f691) |
| Watch Dogs | Fechamentos aleatórios na abertura: o alocador do jogo precisa de uma área alinhada a 128 KiB e recebia alinhamento de 16 KiB. O Cemu oficial tem o mesmo problema (64 KiB no Windows) | Memória do console reservada num endereço do host alinhado a 4 GB | [`cb5321e6`](https://github.com/dfdx047/Fractal/commit/cb5321e6) |
| Watch Dogs | Fechamento na abertura: uma thread consulta um gerenciador e usa outro que a thread principal cria microssegundos depois | Patch no jogo: a thread espera o gerenciador certo | [`d316661a`](https://github.com/dfdx047/Fractal/commit/d316661a) |
| Watch Dogs | Fechamento durante o jogo: o jogo roda tarefas de visibilidade na GPU com transform feedback e lê os resultados na CPU; o Cemu só os guardava na GPU | Resultados de streamout copiados de volta para a memória do console antes que a CPU possa vê-los; origens do offset de streamout seguem o pacote | [`8b69778e`](https://github.com/dfdx047/Fractal/commit/8b69778e) |
| Watch Dogs | Fechamento durante o jogo: draws indexados com streamout gravavam as saídas na posição do valor do índice, não na ordem do index buffer | Draws indexados com streamout desenhados a partir dos vértices reunidos na ordem do índice | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Watch Dogs | Imagem congelada a partir do prólogo (tabuleiro, legendas empilhadas, tutorial "Shoot Maurice" parado): os rects do upscale final e do FXAA eram descartados | Sem descarte de faces em primitivas rect | [`6d80adae`](https://github.com/dfdx047/Fractal/commit/6d80adae) |
| Watch Dogs | Congelamento de 38-68 s ao entrar no jogo; shaders de streamout compilados de novo para cada pixel shader ligado | Vertex shaders com rasterização descartada não dependem do pixel shader; shaders grandes em cache pelo código-fonte. Agora 9 s | [`9b6ee09b`](https://github.com/dfdx047/Fractal/commit/9b6ee09b), [`69ac8320`](https://github.com/dfdx047/Fractal/commit/69ac8320) |
| Watch Dogs | Congelamento depois de sessões longas (por volta de 1000 s no RP5, 1250 s no PC): o jogo confere que a GPU está parada numa espera por uns 10 µs e então preenche o próximo trecho da sua lista de tarefas de GPU. O processador de comandos já tinha passado da espera e rodado o trecho vazio, então uma tarefa de visibilidade nunca terminava. O Cemu oficial tem a mesma corrida | O processador de comandos confere a espera de novo logo antes do primeiro efeito; barreiras release/acquire nas display lists. 3000 s sem congelar no PC e no RP5 | [`210b613e`](https://github.com/dfdx047/Fractal/commit/210b613e) |
| Injustice: Gods Among Us | Travamento da GPU: laços de shader sem break viravam laços infinitos | Constantes de laço emuladas (`IT_SET_LOOP_CONST`, variáveis de laço nos uniforms) | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Injustice: Gods Among Us | Lutadores esticados (skinning na GPU via streamout) | Offsets de streamout tirados do pacote, contados em DWORDs, sem o vértice base do draw | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Need for Speed: Most Wanted U | Estalos e chiados no som do motor ao acelerar | `AXDecodeAdpcmData` implementado (faltava), histórico do biquad em precisão total, contexto de laço do ADPCM, estouro no filtro passa-baixa | [`44cdd26b`](https://github.com/dfdx047/Fractal/commit/44cdd26b) |
| Todos os jogos | Pipelines pré-compilados na abertura eram montados com o estado da GPU daquele momento, então o cache do driver nunca batia e eles eram compilados de novo durante o jogo. O Cemu oficial tem o mesmo bug | Estado do pipeline montado a partir dos registradores passados ao compilador | [`69ac8320`](https://github.com/dfdx047/Fractal/commit/69ac8320) |
| Todos os jogos | Cache de buffers: um uniform buffer com registrador de tamanho `0xFFFFFFFF` virava um nó de tamanho zero que corrompia a árvore de faixas | Tamanho verificado | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Super Smash Bros. for Wii U | Congelava ao carregar um estágio no RP5: uma thread de prioridade baixa segurava o mutex que o carregador esperava enquanto uma thread de prioridade maior ocupava o núcleo, então a dona nunca mais rodava. O Cemu não tinha herança de prioridade no `OSMutex` | O `OSLockMutex` empresta a prioridade de quem espera para a dona (e para a cadeia de donas), o `OSUnlockMutex` e o `OSWaitCond` devolvem, como no Cafe OS | [`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685) |
| Splinter Cell: Blacklist | Fechava aos 27 s de jogo: o destino de uma cópia para uma superfície 1D (512x1 R32F) era procurado por uma view 2D, que o cache de texturas recusa | Destinos de cópia 1D e 1D-array criados como 2D e 2D-array, como os de cubemap | [`dd40e27e`](https://github.com/dfdx047/Fractal/commit/dd40e27e) |
| Splinter Cell: Blacklist | Preso em 20 fps em todos os aparelhos e, no RP5, parado na tela de carregamento da missão: o resultado das occlusion queries só chegava à memória do jogo no vsync virtual (ou a cada 8 queries), e o jogo espera 3 queries por quadro | O laço ocioso do processador de comandos finaliza as queries pendentes assim que o renderizador as tem. 30 fps (o limite do jogo) no POCO e no RP5, e o RP5 entra no jogo | [`1557a268`](https://github.com/dfdx047/Fractal/commit/1557a268) |
| Splinter Cell: Blacklist | `OSGetMapVirtAddrRange` e `OSGetDataPhysAddrRange` não existiam, então o jogo lia o que estivesse nas variáveis de saída | Devolvem a faixa do `OSAllocVirtAddr` e a MEM2 | [`ad5d1dc1`](https://github.com/dfdx047/Fractal/commit/ad5d1dc1) |
| Batman: Arkham City | Congelava de vez depois do New Game ("Saving...") em todos os aparelhos: occlusion queries do GX2 que terminavam sem nenhuma query do host rodando nunca finalizavam | Sem nada em andamento e sem query do host ativa, todo evento de query emitido até ali conta como finalizado; query encerrada duas vezes ou reiniciada cedo é tratada | [`e5dc06a1`](https://github.com/dfdx047/Fractal/commit/e5dc06a1) |
| The Legend of Zelda: Breath of the Wild | Fechava ao carregar um cache de shaders da comunidade (issue #3 no GitHub): fetch shaders com cláusula de 5-8 ou 13-16 fetches iam para o parser antigo. O Cemu upstream tem a mesma verificação | Máscara corrigida; os 5981 vertex shaders desse cache seguem pelo caminho de CF | [`42a2fbd7`](https://github.com/dfdx047/Fractal/commit/42a2fbd7) |
| LEGO Batman 2: DC Super Heroes | Piscava preto a cada dois quadros em todas as GPUs: o jogo faz dois swaps por quadro e copia para a TV uma vez só, e o segundo swap apresentava uma imagem preta | Depois que uma imagem do jogo foi apresentada, um swap sem cópia não apresenta nada, como o scan buffer do console | [`de5d2869`](https://github.com/dfdx047/Fractal/commit/de5d2869) |
| Teslagrad | Parava (SIGTRAP) ao salvar: o `SetEndOfFile` no Unix/Android era um esboço vazio, também no upstream | O arquivo é cortado na posição atual | [`8daaafec`](https://github.com/dfdx047/Fractal/commit/8daaafec) |
| Disney Infinity 3.0 | Parava no `getsockname` com um socket que o emulador não conhece | Devolve -1 com `EINVAL`, ou o erro do host | [`6b27c9ae`](https://github.com/dfdx047/Fractal/commit/6b27c9ae) |
| Disney Infinity 3.0, Cars 3 | Parava quando o heap de 164 MB do cache de buffers enchia de faixas de streamout | Como último recurso, faixas de streamout sem uso há 2 quadros são descartadas (POCO 3 de 3 e RP5 sem crash); a alocação só é repetida depois de uma falha | [`146fdb6e`](https://github.com/dfdx047/Fractal/commit/146fdb6e), [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396) |
| ZombiU (EUA) | Tela preta depois de abrir com a atualização v32, também no Cemu de PC. A wiki do Cemu manda tirar a atualização, o que um .wua não permite | O jogo abre sem essa atualização (fica no log); abertura e jogo no POCO | [`b6cf7c8b`](https://github.com/dfdx047/Fractal/commit/b6cf7c8b) |

Também em código compartilhado: o recompilador agora trata `psq_lx`/`psq_stx` e as formas com update, que o Cemu
oficial manda para o interpretador ([`9107127b`](https://github.com/dfdx047/Fractal/commit/9107127b); no The Amazing
Spider-Man 2 são os conversores de amostras de áudio que deixavam um núcleo perto de 100 % durante o carregamento).

**Assassin's Creed IV: Black Flag:** nos nossos testes a versão oficial 2.6 do Cemu (AppImage) parou em "loading..." sem
uso de CPU, enquanto a build de PC do Fractal e o Retroid Pocket 5 rodam a cutscene de abertura e o convés do navio.
Ainda não identificamos qual mudança faz a diferença.

### Correções de jogos para Android, ARM64 e drivers de GPU de celular

| Jogo | O que estava errado | Correção | Commit |
|---|---|---|---|
| Ninja Gaiden 3: Razor's Edge | Tela preta depois de alguns minutos no Android: uma corrida no código de event flags do próprio jogo deixava duas threads esperando para sempre | O "set" de event flag do jogo não reseta mais um evento que a outra thread acabou de sinalizar | [`8eaae97a`](https://github.com/dfdx047/Fractal/commit/8eaae97a) |
| Ninja Gaiden 3: Razor's Edge | Fechamento: funções com mais de 1 MB de código ARM64 deixavam desvios fora de alcance | Código frio emitido entre segmentos; um erro de codificação deixa a função interpretada em vez de abortar | [`a26051bf`](https://github.com/dfdx047/Fractal/commit/a26051bf) |
| Ninja Gaiden 3: Razor's Edge | Turnip: imagem toda borrada pela profundidade de campo lendo um depth buffer desatualizado | Barreira no fim de cada render pass no Turnip | [`64c4b75c`](https://github.com/dfdx047/Fractal/commit/64c4b75c) |
| Ninja Gaiden 3: Razor's Edge | Fechamento no próprio alocador com o zlib-ng como zlib do host | Estado do zlib alocado no heap padrão | [`8eaae97a`](https://github.com/dfdx047/Fractal/commit/8eaae97a) |
| Assassin's Creed IV: Black Flag | Mundo preto (só o HUD) na selva, perto da ponte: o `fsel` do recompilador copiava só a primeira metade do registrador e o jogo lia lixo na segunda numa rotina de tempo de quadro | `fsel` copia o registrador inteiro, como o interpretador. Os apagões ficaram bem mais raros mas não sumiram (ver Em andamento) | [`b8d27fdf`](https://github.com/dfdx047/Fractal/commit/b8d27fdf) |
| Tekken Tag Tournament 2 | O driver do sistema da Qualcomm fechava ao compilar um shader de gather 4x4 na primeira luta | Escritas indexadas em GPR geradas como switch em vez de um array de GPRs com índice dinâmico | [`1293cd7d`](https://github.com/dfdx047/Fractal/commit/1293cd7d) |
| Assassin's Creed IV: Black Flag | Driver do sistema da Qualcomm: blocos verdes/magenta/azuis sobre partículas, telas de carregamento e cutscenes. O bloom amostra a textura em que desenha, o que é indefinido sem `VK_EXT_attachment_feedback_loop_layout` | Esses draws amostram uma cópia da textura | [`64c4b75c`](https://github.com/dfdx047/Fractal/commit/64c4b75c) |
| Watch Dogs | Estádio e bloom brancos na Adreno: a Adreno devolve NaN em min/max/clamp quando um operando é NaN | min/max/clamp seguros contra NaN ligados por padrão nas GPUs Qualcomm | [`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223) |
| Xenoblade Chronicles X | Turnip: travamento da GPU uns 36 s depois de começar um jogo novo, sempre | Imagens `R8G8B8A8_UNORM` continuam mutáveis (sem UBWC) no Turnip | [`2a72a16b`](https://github.com/dfdx047/Fractal/commit/2a72a16b) |
| Xenoblade Chronicles X | Adreno 650: o envio de command buffers falhava depois de ~160 s (cópias entre imagens comprimidas de formatos diferentes) | Essas cópias passam por um buffer intermediário | [`017be0ef`](https://github.com/dfdx047/Fractal/commit/017be0ef) |
| Bayonetta 2 | Fechava 3 s depois de abrir (RG Cube), travava na tela de abertura (RP5): o backend ARM64 nunca carregava o índice do núcleo (`mfspr UPIR`) | SPRs acima de 998 carregados pelo nome | [`613dc67b`](https://github.com/dfdx047/Fractal/commit/613dc67b) |
| Pokkén Tournament | Fechava sempre ao sair de um menu: armazenamento local de thread lido numa thread do host e usado em outra (TLS dinâmico no Android) | TLS do escalonador e das fibras lido na thread que roda o código. Chega às batalhas a 60 fps no RP5 | [`8ea62197`](https://github.com/dfdx047/Fractal/commit/8ea62197) |
| Need for Speed: Most Wanted U | Fechava ao entrar na cidade: descriptor sets esgotados (mais de 160 mil combinações de textura/sampler) | Pools de descritores crescem conforme a necessidade, sets sem uso há 600 quadros são liberados | [`1feba3d6`](https://github.com/dfdx047/Fractal/commit/1feba3d6) |
| Need for Speed: Most Wanted U | Folhagem e luzes brancas, estouradas | O graphic pack "Bloom Fix" da comunidade vem ligado por padrão (causa raiz ainda em aberto) | [`0386ec86`](https://github.com/dfdx047/Fractal/commit/0386ec86) |
| The Legend of Zelda: Breath of the Wild | Turnip: travamento da GPU na abertura com renderização em blocos | O Turnip renderiza na memória do sistema por padrão (opção por jogo) | [`525a6907`](https://github.com/dfdx047/Fractal/commit/525a6907), [`7f8d5a0f`](https://github.com/dfdx047/Fractal/commit/7f8d5a0f) |
| The Legend of Zelda: Breath of the Wild | A thread de renderização esperava 15,6 ms por quadro numa sincronização completa a cada `GX2DrawDone` | Opção por jogo, desligada nos perfis padrão do BotW: RP5 9,4 → 11,1 fps | [`951eb1e7`](https://github.com/dfdx047/Fractal/commit/951eb1e7) |
| Star Fox Zero | SIGBUS em atômicos de 64 bits desalinhados no ARM64 | Lock para atômicos desalinhados (do Cemu Plus) | [`c35755ec`](https://github.com/dfdx047/Fractal/commit/c35755ec) |
| Mario Kart 8 | Fechamento (estado do mixer de som) | Reset do `snd_user` trazido do Cemu oficial antes do merge completo | [`c62e6977`](https://github.com/dfdx047/Fractal/commit/c62e6977) |
| Jogos com diálogo de erro | Ficavam presos nos portáteis: os botões do diálogo de erro não recebiam foco sem mouse | A e B fecham o diálogo | [`5879e1f5`](https://github.com/dfdx047/Fractal/commit/5879e1f5) |
| Todos os jogos | Configurações e lista de jogos viravam zeros após reiniciar logo depois de salvar | Arquivos sincronizados com o armazenamento antes de substituir os antigos | [`9db92543`](https://github.com/dfdx047/Fractal/commit/9db92543) |
| Todos os jogos no PanVK | O PanVK do faux123 (Mali-G615) fechava todo jogo no primeiro quadro: ele anuncia `VK_KHR_present_wait`, que o WSI do Mesa no Android não implementa | `present_wait` desligado no PanVK; Twilight Princess HD roda no limite de 30 fps | [`45be70a4`](https://github.com/dfdx047/Fractal/commit/45be70a4) |
| Pikmin 3 | O driver da Qualcomm fechava ao criar pipelines de shaders que leem e escrevem GPRs por índice | Na Qualcomm esses shaders usam variáveis de GPR separadas: leitura por cadeia de seleção, escrita por switch | [`ab851c1a`](https://github.com/dfdx047/Fractal/commit/ab851c1a) |
| Yoshi's Woolly World | Driver da Mali: sem o Yoshi e os inimigos. O streamout era escrito com stores em storage buffer a partir do vertex shader, que o driver não suporta | Streamout por `VK_EXT_transform_feedback` em drivers sem stores no pipeline de vértices | [`95904874`](https://github.com/dfdx047/Fractal/commit/95904874) |
| Yoshi's Woolly World (qualquer jogo com texturas BC4/BC5 SNORM na Mali) | Manchas pretas sobre a lã: normal maps BC5 SNORM decodificados na CPU como bytes sem sinal, BC4 SNORM como UNORM | Gravados como bytes em complemento de dois, extremos SNORM mapeados certo | [`95904874`](https://github.com/dfdx047/Fractal/commit/95904874) |
| Yoshi's Woolly World | O Turnip T30 travava a GPU do RP5 no Mundo 1-1 | Perfil padrão: driver do sistema | [`5c8f856d`](https://github.com/dfdx047/Fractal/commit/5c8f856d) |
| Super Smash Bros. for Wii U | O Turnip (T19 e T30) reiniciava o RP5 no fim de 4 de 4 lutas | Perfil padrão: driver do sistema (RP5 42,6 fps, quase o que o Turnip dava; POCO 55 fps contra 37 no PanVK) | [`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685), [`614d2531`](https://github.com/dfdx047/Fractal/commit/614d2531) |
| Kirby and the Rainbow Curse | O Turnip em modo de memória do sistema reiniciava o RP5 ao entrar em Green Valley 1-1 em 4 de 5 runs | Perfil padrão: driver do sistema (60,0 fps no RP5), Turnip sem sysmem | [`803fed48`](https://github.com/dfdx047/Fractal/commit/803fed48) |
| Sonic Lost World | SIGBUS logo depois da tela de título: `stwcx.` num endereço ímpar virava `CASAL`, que exige alinhamento de 4 bytes no ARM64 | Endereços desalinhados usam comparação e escrita comuns, no recompilador e no interpretador | [`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796) |
| Sonic Lost World, Rayman Legends | Multi-core: o Sonic fecha ou trava ao pular o vídeo de abertura, o Rayman trava na tela de carregamento (POCO: 4 de 4 e 3 de 4 runs) | Perfis padrão: recompilador single-core. O Rayman depende de uma thread só e não perde nada (RP5 17,3 contra 17,0 fps) | [`fdbc093c`](https://github.com/dfdx047/Fractal/commit/fdbc093c) |
| Rayman Legends | Os drivers Mesa deixam a GPU a 99 % na primeira fase: Turnip 17 fps contra 59 no driver da Adreno 650, PanVK 18 contra 29,6 no driver da Mali-G615 | Perfil padrão: driver do sistema | [`1f6476e3`](https://github.com/dfdx047/Fractal/commit/1f6476e3) |
| Sonic Lost World | O PanVK reiniciou o POCO duas vezes em duas | Perfil padrão: driver do sistema (48 fps no driver da Mali-G615; 53 no RP5, igual ao Turnip) | [`122d88bb`](https://github.com/dfdx047/Fractal/commit/122d88bb) |
| Paper Mario: Color Splash | Pontinhos coloridos sobre a imagem no Turnip e no PanVK: um passe amostra o attachment de cor em que desenha, o que é indefinido mesmo com `VK_EXT_attachment_feedback_loop_layout` | O draw amostra uma cópia feita antes dele, em todo driver. Mesma velocidade no POCO (30,1 fps, limite 30) | [`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796) |
| Monster Hunter 3 Ultimate | Fechava (ou congelava) aos 245 s de um jogo novo, no fim do vídeo de abertura: o carregador do jogo diz que um arquivo está pronto enquanto o pedido para reabri-lo ainda está pendente. Em single-core fechava sempre no RP5 e no POCO | Correção do jogo: "pronto" também exige nenhum comando pendente. 6/6 runs chegam à vila | [`dd4aa043`](https://github.com/dfdx047/Fractal/commit/dd4aa043) |
| New Super Mario Bros. U | Fechava aos 57 s da abertura numa CPU lenta (Mi A3, 6/6 runs): a fila de desenho das cabeças de Mii era percorrida antes de outra thread preencher o modelo | Correção do jogo que pula só a entrada sem modelo (o pack "Title Screen Crash Fix" da comunidade pula a fila toda). Mi A3: 2/2 runs chegam ao vídeo de abertura | [`ff719783`](https://github.com/dfdx047/Fractal/commit/ff719783) |
| Resident Evil Revelations | Fechava na Mali (driver do sistema e PanVK) logo depois da abertura: o render target A2_B10_G10_R10 não tinha formato Vulkan | Mapeado para `A2B10G10R10_UNORM_PACK32` | [`71f3a05a`](https://github.com/dfdx047/Fractal/commit/71f3a05a) |
| The Legend of Zelda: Twilight Princess HD | Minimapa preto no driver da Qualcomm: o geometry shader da emulação de rects não declarava o mesmo `gl_PerVertex` dos vertex shaders | Mesmo bloco declarado nos dois estágios | [`a95f1b41`](https://github.com/dfdx047/Fractal/commit/a95f1b41) |
| Cars 3, Ninja Gaiden 3, Disney Infinity 3.0 | Draws pulados no driver da Qualcomm (Cars 3: 25 pipelines, Ninja Gaiden 3: 24, Disney Infinity 3.0: 2): o geometry shader de rects deixava entradas do pixel shader sem escrever | O geometry shader escreve zeros nelas. Cars 3: 0 pipelines com falha | [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396) |
| FIFA 13 | O Turnip T19 em modo de memória do sistema fechava o jogo 8 s depois de abrir (5 de 5 runs no RP5) | Perfil padrão: Turnip sem sysmem (T19 56,5 fps) | [`8d168fa9`](https://github.com/dfdx047/Fractal/commit/8d168fa9) |
| Call of Duty: Ghosts | Fechava quando o jogo se reabre no primeiro boot (POCO): o Android não tinha implementação de sistema para reabrir e sair do título | O renderizador é montado de novo nas mesmas superfícies; a saída fica no log | [`e2030e99`](https://github.com/dfdx047/Fractal/commit/e2030e99) |
| Hot Wheels: World's Best Driver | Congelava num carregamento (RP5 1 run em 3): duas corridas no handshake da thread de renderização do próprio jogo, facilitadas por threads do host interrompidas | Correção do jogo para as duas. RP5 8 de 8 e POCO 5 de 5 runs sem congelar | [`dfde2b64`](https://github.com/dfdx047/Fractal/commit/dfde2b64) |
| Teslagrad | 770 funções no interpretador e a thread do recompilador a 99 % na Adreno 610 | Mais instruções recompiladas e funções enormes traduzidas em janelas (ver a seção do recompilador): 1 % low 3,7 → 10,3 fps no Mi A3, depois 0 funções no interpretador | [`66f59e89`](https://github.com/dfdx047/Fractal/commit/66f59e89), [`85b29bd9`](https://github.com/dfdx047/Fractal/commit/85b29bd9) |
| Todos os jogos | Configurações mudadas no menu do jogo eram sobrescritas pela cópia antiga da biblioteca | Todo salvamento da biblioteca relê o arquivo antes, se ele mudou no disco | [`e91a7121`](https://github.com/dfdx047/Fractal/commit/e91a7121) |
| Todos os jogos | O app fechava ao abrir em ROMs customizadas que negam `Android/data` (issue #1 no GitHub) | Erros de pasta vão para o log em vez de fechar o app; a pasta padrão cai para a pasta privada do app | [`42a2fbd7`](https://github.com/dfdx047/Fractal/commit/42a2fbd7), [`5f607b21`](https://github.com/dfdx047/Fractal/commit/5f607b21) |

### Renderizador (Vulkan) para GPUs de celular por blocos

Adreno e Mali gravam e recarregam todos os attachments quando um render pass termina, então a maior parte deste
trabalho evita quebrar passes.

- Envio de buffers e occlusion queries não terminam mais o render pass; o cache de buffers fica em memória visível ao
  host nas GPUs de memória unificada ([`2c1895ac`](https://github.com/dfdx047/Fractal/commit/2c1895ac)).
- Trabalho de vértices continua sobreposto às barreiras de render target; dependências do próprio attachment
  rastreadas por attachment ([`7b36890f`](https://github.com/dfdx047/Fractal/commit/7b36890f),
  [`d9606be8`](https://github.com/dfdx047/Fractal/commit/d9606be8)).
- Nós ocupados do cache de buffers vão para memória nova em vez de terminar o pass
  ([`ec46fba8`](https://github.com/dfdx047/Fractal/commit/ec46fba8), [`a74fbb56`](https://github.com/dfdx047/Fractal/commit/a74fbb56)).
- Envio de texturas num command buffer de prólogo em vez de terminar o pass
  ([`2eeceaea`](https://github.com/dfdx047/Fractal/commit/2eeceaea)).
- Compressão de framebuffer (UBWC na Adreno, AFBC na Mali) mantida nas imagens de cor
  ([`017be0ef`](https://github.com/dfdx047/Fractal/commit/017be0ef)).
- Turnip: `vkGetFenceStatus` bloqueia no KGSL, então os command buffers terminados são achados por um marcador de
  conclusão ([`65047547`](https://github.com/dfdx047/Fractal/commit/65047547)).
- Limpeza de depth por render pass em vez de `vkCmdClearDepthStencilImage`, que custa ~3,5 ms por limpeza em 1080p no
  driver da Adreno 610 ([`aabeea76`](https://github.com/dfdx047/Fractal/commit/aabeea76)).
- Um cache de pipelines por driver, mantido ao trocar de driver ([`2e43b0b7`](https://github.com/dfdx047/Fractal/commit/2e43b0b7)).
- Pipelines compilados em 1-2 threads no Android em vez de até 7, o que reduziu os engasgos com cache frio
  ([`9893cf72`](https://github.com/dfdx047/Fractal/commit/9893cf72)).
- Swapchain escolhe um composite alpha suportado (Mesa PanVK) ([`c1f4cd8e`](https://github.com/dfdx047/Fractal/commit/c1f4cd8e)).
- Um draw que amostra o próprio attachment de cor lê uma cópia em todo driver, também onde existe
  `VK_EXT_attachment_feedback_loop_layout` ([`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796)).
- Streamout por `VK_EXT_transform_feedback` em drivers sem stores no pipeline de vértices (driver da Mali)
  ([`95904874`](https://github.com/dfdx047/Fractal/commit/95904874)).
- O resultado das occlusion queries chega ao jogo com a GPU ociosa; queries que terminam sem query do host rodando
  finalizam ([`1557a268`](https://github.com/dfdx047/Fractal/commit/1557a268), [`e5dc06a1`](https://github.com/dfdx047/Fractal/commit/e5dc06a1)).
- Um swap sem nada copiado para a tela mantém a última imagem ([`de5d2869`](https://github.com/dfdx047/Fractal/commit/de5d2869)).
- Cache de buffers: faixas de streamout descartadas como último recurso quando o heap enche ([`146fdb6e`](https://github.com/dfdx047/Fractal/commit/146fdb6e), [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396)).
- Sem `present_wait` no PanVK ([`45be70a4`](https://github.com/dfdx047/Fractal/commit/45be70a4)); render targets `A2_B10_G10_R10` mapeados ([`71f3a05a`](https://github.com/dfdx047/Fractal/commit/71f3a05a)).

### Recompilador PowerPC → ARM64

- Contador de ciclos num registrador, funções juntas numa área de código, saltos e imediatos mais curtos, desvios por
  flags, acesso à memória com offset zero sem add ([`23b10251`](https://github.com/dfdx047/Fractal/commit/23b10251)).
- `mfspr UPIR` ([`d61aee38`](https://github.com/dfdx047/Fractal/commit/d61aee38), [`613dc67b`](https://github.com/dfdx047/Fractal/commit/613dc67b)),
  `ps_nabs`, `dcbt` ([`a26051bf`](https://github.com/dfdx047/Fractal/commit/a26051bf)), `psq_lx`/`psq_stx`/`psq_lux`/`psq_stux`,
  `eieio`, `dcbi` ([`9107127b`](https://github.com/dfdx047/Fractal/commit/9107127b)) não caem mais no interpretador.
- Funções enormes ficam dentro do alcance de desvio do ARM64 ([`a26051bf`](https://github.com/dfdx047/Fractal/commit/a26051bf)).
- Flush-to-zero realmente ligado no Android ([`b385304f`](https://github.com/dfdx047/Fractal/commit/b385304f)).
- `stwcx.` em endereço desalinhado ([`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796)).
- Multiplicação e soma fundidas (`fmadd`/`fmsub`/`fnmsub` e as formas single) como no Espresso e no interpretador; antes,
  `a*c - round(a*c)` dava zero só no código recompilado ([`fec3837f`](https://github.com/dfdx047/Fractal/commit/fec3837f)).
- `addo`/`subfo`/`addco`/`subfco`, `mfxer`/`mtxer`, `fcmpo`, desvios no estilo `bdnzf` e desvios condicionais longos;
  uma função contígua enorme é traduzida em janelas (no Teslagrad um switch de 955 KB era traduzido 22 vezes)
  ([`66f59e89`](https://github.com/dfdx047/Fractal/commit/66f59e89)).
- `subfme`, `divwo`/`divwuo` e `bcl 20`: Disney Infinity 2.0 145 → 0 e Teslagrad 157 → 0 funções no interpretador
  ([`85b29bd9`](https://github.com/dfdx047/Fractal/commit/85b29bd9)).

### Extensões ARMv8 e NEON

- Extensões opcionais do ARMv8 (AES, PMULL, CRC32, atômicos LSE, FP16, dot product) detectadas em tempo de execução; a
  build continua ARMv8.0-A para todo celular arm64-v8a ([`b1078675`](https://github.com/dfdx047/Fractal/commit/b1078675)).
- AES-128 com a Crypto Extension na leitura de discos criptografados ([`852801bb`](https://github.com/dfdx047/Fractal/commit/852801bb)), instruções CRC32 no carregamento
  de RPX/RPL ([`64efc3e5`](https://github.com/dfdx047/Fractal/commit/64efc3e5)), máximo do index buffer com NEON ([`bd6af3dc`](https://github.com/dfdx047/Fractal/commit/bd6af3dc)) e decodificação BC1-BC5 por paleta de
  bloco com consultas de tabela NEON para GPUs sem BCn ([`c4e6b0aa`](https://github.com/dfdx047/Fractal/commit/c4e6b0aa)). Números em *Ganhos medidos*.
- `tools/neon_tests` compara cada caminho NEON com o código escalar bit a bit ([`852801bb`](https://github.com/dfdx047/Fractal/commit/852801bb)).

### Só Android

- O código de desktop saiu: interface wxWidgets, renderizadores OpenGL e Metal, recompilador x86-64, código de Windows,
  macOS e Linux desktop, entrada e áudio de desktop, presença no Discord (572 arquivos, cerca de 103 mil linhas). A
  biblioteca nativa caiu de 22,7 para 18,4 MB sem exportar símbolos de terceiros, só as bibliotecas arm64 vão no APK,
  recursos sem uso saem no build e os graphic packs baixam com `HttpURLConnection` em vez do Ktor ([`9aa94b36`](https://github.com/dfdx047/Fractal/commit/9aa94b36), [`ee6e63b1`](https://github.com/dfdx047/Fractal/commit/ee6e63b1), [`d07f65ea`](https://github.com/dfdx047/Fractal/commit/d07f65ea), [`54b44032`](https://github.com/dfdx047/Fractal/commit/54b44032), [`4d4403c3`](https://github.com/dfdx047/Fractal/commit/4d4403c3), [`4b0f2868`](https://github.com/dfdx047/Fractal/commit/4b0f2868), [`bc69967a`](https://github.com/dfdx047/Fractal/commit/bc69967a), [`10bd5abd`](https://github.com/dfdx047/Fractal/commit/10bd5abd), [`5e0236a6`](https://github.com/dfdx047/Fractal/commit/5e0236a6)).

### Decompilador de shaders

- Constantes de laço ([`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5)).
- Escritas indexadas em GPR como switch ([`1293cd7d`](https://github.com/dfdx047/Fractal/commit/1293cd7d)).
- Regras de NaN do R600 para min/max/clamp na Adreno ([`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223)).
- Vertex shaders de streamout independentes do pixel shader ligado, cache de SPIR-V pelo código-fonte para shaders
  grandes ([`9b6ee09b`](https://github.com/dfdx047/Fractal/commit/9b6ee09b)).
- Leitura e escrita de GPR por índice sem o array de GPRs na Qualcomm ([`ab851c1a`](https://github.com/dfdx047/Fractal/commit/ab851c1a)).
- Saídas de transform feedback para o streamout ([`95904874`](https://github.com/dfdx047/Fractal/commit/95904874)).
- Geometry shader da emulação de rects: mesmo `gl_PerVertex` dos vertex shaders, zeros nas entradas do pixel shader
  ([`a95f1b41`](https://github.com/dfdx047/Fractal/commit/a95f1b41), [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396)).
- Fetch shaders com cláusula de 5-8 ou 13-16 fetches detectados ([`42a2fbd7`](https://github.com/dfdx047/Fractal/commit/42a2fbd7)).

### Sistema e áudio

- Correções de áudio do AX ([`44cdd26b`](https://github.com/dfdx047/Fractal/commit/44cdd26b)).
- O núcleo principal e a thread da GPU dormem em vez de girar quando ociosos; o núcleo principal dorme com o jogo
  pausado ([`ff38f5b4`](https://github.com/dfdx047/Fractal/commit/ff38f5b4), [`bf0944a4`](https://github.com/dfdx047/Fractal/commit/bf0944a4)).
- Leitura dos argumentos do `GX2Init` ([`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223)).
- Herança de prioridade no `OSMutex` ([`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685)); `OSGetMapVirtAddrRange` e `OSGetDataPhysAddrRange`
  ([`ad5d1dc1`](https://github.com/dfdx047/Fractal/commit/ad5d1dc1)); corte de arquivos no Android ([`8daaafec`](https://github.com/dfdx047/Fractal/commit/8daaafec)); erros do `getsockname` ([`6b27c9ae`](https://github.com/dfdx047/Fractal/commit/6b27c9ae)).
- Reabrir e sair do título no Android ([`e2030e99`](https://github.com/dfdx047/Fractal/commit/e2030e99)); um jogo abre sem uma atualização que sabidamente impede o
  carregamento ([`b6cf7c8b`](https://github.com/dfdx047/Fractal/commit/b6cf7c8b)).
- Correções de jogos numa tabela de padrões exatos do código do jogo (Ninja Gaiden 3, Monster Hunter 3 Ultimate, New
  Super Mario Bros. U, Hot Wheels) ([`dd4aa043`](https://github.com/dfdx047/Fractal/commit/dd4aa043), [`ff719783`](https://github.com/dfdx047/Fractal/commit/ff719783), [`dfde2b64`](https://github.com/dfdx047/Fractal/commit/dfde2b64)).

### Interface

- Cinco temas, design tokens, fontes incluídas, superfícies de vidro, papéis de parede estáticos e animados
  ([`73fd0c96`](https://github.com/dfdx047/Fractal/commit/73fd0c96)); animação de abertura por tema
  ([`4489bc72`](https://github.com/dfdx047/Fractal/commit/4489bc72)); ícone do Sierpinski
  ([`a59f82f8`](https://github.com/dfdx047/Fractal/commit/a59f82f8)).
- Biblioteca em grade, carrossel e lista, configuração inicial, tela de aparência
  ([`80712467`](https://github.com/dfdx047/Fractal/commit/80712467)); tempo de jogo, última vez jogado, ordenação,
  exportar/importar save e cache de shaders, tela por jogo ([`ecf40b3b`](https://github.com/dfdx047/Fractal/commit/ecf40b3b)).
- Opções de Resolução e FPS detectadas nos graphic packs de qualquer jogo
  ([`d95f0191`](https://github.com/dfdx047/Fractal/commit/d95f0191), [`9a359b76`](https://github.com/dfdx047/Fractal/commit/9a359b76)).
- Configurações agrupadas em seções, telas antigas no estilo novo
  ([`b7428b86`](https://github.com/dfdx047/Fractal/commit/b7428b86), [`383af9d2`](https://github.com/dfdx047/Fractal/commit/383af9d2)).
- Navegação por controle e legenda de botões compacta em todas as telas (`4008a538` a `572b8a9e`), foco restaurado ao
  voltar (`eb7e614e` a `7dc5303c`), layout para celular na vertical ([`94e60896`](https://github.com/dfdx047/Fractal/commit/94e60896)).
- Menu lateral no jogo seguindo o tema ([`d6bba2e5`](https://github.com/dfdx047/Fractal/commit/d6bba2e5)).
- English, Português (Brasil) e Español em todos os textos do app
  ([`c9d8a940`](https://github.com/dfdx047/Fractal/commit/c9d8a940), [`a74e2a35`](https://github.com/dfdx047/Fractal/commit/a74e2a35)).

- **GamePad na segunda tela:** portáteis de duas telas (AYN Thor) e monitores externos recebem a tela do GamePad com
  toque; escala e tamanho próprios no menu do jogo; escolhas salvas entre jogos (base do branch `android-port-dual` do
  SapphireRhodonite) ([`f29f2800`](https://github.com/dfdx047/Fractal/commit/f29f2800)).
- Botão voltar dos portáteis cujo controle o envia (AYN Odin 3) abre o menu do jogo ([`026b580a`](https://github.com/dfdx047/Fractal/commit/026b580a)).
- **HUD de desempenho** no visual de cada tema: FPS, tempo de quadro, uso de CPU e GPU, temperaturas, RAM, consumo e
  bateria, com itens, tamanho, opacidade, disposição e posição nas configurações e no menu do jogo; portado do HUD do
  Winlator-Ludashi do StevenMXZ (GPL-3.0); uso da GPU pelos timestamps dos command buffers
  ([`bc5372e2`](https://github.com/dfdx047/Fractal/commit/bc5372e2), [`b558e01a`](https://github.com/dfdx047/Fractal/commit/b558e01a), [`378222ec`](https://github.com/dfdx047/Fractal/commit/378222ec)).
- Graphic packs e cheats no menu do jogo, aplicados ao jogo rodando (issue #5 no GitHub) ([`c6522627`](https://github.com/dfdx047/Fractal/commit/c6522627)); as
  configurações do jogo no menu do jogo com **Reiniciar jogo** ([`98364ff2`](https://github.com/dfdx047/Fractal/commit/98364ff2)).
- Caches de shaders da comunidade: download por jogo e compartilhamento anônimo, revisado antes de entrar
  ([`63968db1`](https://github.com/dfdx047/Fractal/commit/63968db1), [`c75e10c7`](https://github.com/dfdx047/Fractal/commit/c75e10c7)).
- Pasta de dados numa pasta escolhida pelo usuário (armazenamento interno ou cartão SD); instalar mods de um .zip ou de
  uma pasta ([`5f607b21`](https://github.com/dfdx047/Fractal/commit/5f607b21)).
- Uma resolução menor oferecida uma vez quando o jogo roda bem abaixo da velocidade cheia ([`08c67298`](https://github.com/dfdx047/Fractal/commit/08c67298)).
- Música do menu por tema com loop sem pausa, sons do menu, e música e efeitos do próprio usuário
  ([`98364ff2`](https://github.com/dfdx047/Fractal/commit/98364ff2), [`f1d24be9`](https://github.com/dfdx047/Fractal/commit/f1d24be9), [`c8e0da47`](https://github.com/dfdx047/Fractal/commit/c8e0da47)).
- Botões dos diálogos dividem a largura ou se empilham ([`af271be2`](https://github.com/dfdx047/Fractal/commit/af271be2)).

### Drivers de GPU

- Catálogo de drivers a partir dos releases do GitHub, instalação com um toque, limpeza de cache por driver
  ([`3927b698`](https://github.com/dfdx047/Fractal/commit/3927b698)); drivers experimentais para Mali, mais fontes, volta
  automática ao driver do sistema ([`7ce49626`](https://github.com/dfdx047/Fractal/commit/7ce49626),
  [`9e8cf0c3`](https://github.com/dfdx047/Fractal/commit/9e8cf0c3)); página por desenvolvedor com o Turnip do MrPurple666
  primeiro, drivers e graphic packs na configuração inicial ([`0386ec86`](https://github.com/dfdx047/Fractal/commit/0386ec86)).
- GPUs Samsung Xclipse detectadas, com drivers RADV customizados de cinco fontes; ainda não testado num aparelho
  Xclipse ([`8da0d804`](https://github.com/dfdx047/Fractal/commit/8da0d804)).
- Pacote universal do PanVK do faux123 para Mali ([`45be70a4`](https://github.com/dfdx047/Fractal/commit/45be70a4)).
- Os perfis padrão dos jogos escolhem o driver que não fecha ou é mais rápido (Smash, Yoshi's Woolly World, Kirby and
  the Rainbow Curse, Rayman Legends, Sonic Lost World, FIFA 13; ver a tabela acima).

### Entrada

- Controles na tela: layouts independentes da resolução, perfis, macros, turbo, alternância, deslizar, modo de edição,
  cores do tema, estilos e botões de atalho ([`c813042e`](https://github.com/dfdx047/Fractal/commit/c813042e),
  [`9c15535f`](https://github.com/dfdx047/Fractal/commit/9c15535f), [`5af1e981`](https://github.com/dfdx047/Fractal/commit/5af1e981)).
- Controles físicos mapeados automaticamente ([`841f97a3`](https://github.com/dfdx047/Fractal/commit/841f97a3),
  [`e1698c63`](https://github.com/dfdx047/Fractal/commit/e1698c63)); HOME no botão Mode/Guide
  ([`b343cab3`](https://github.com/dfdx047/Fractal/commit/b343cab3)); atalhos ([`571c9d87`](https://github.com/dfdx047/Fractal/commit/571c9d87)).
- Toques rápidos nos botões da tela não se perdem mais ([`8929b786`](https://github.com/dfdx047/Fractal/commit/8929b786)).
- Tipo de controle por jogo (GamePad / Pro / Classic / Wiimote), um aviso único quando o jogo pode ignorar a
  configuração, opção de layout de botões da Nintendo ([`5e6c4e42`](https://github.com/dfdx047/Fractal/commit/5e6c4e42)).

### Upscaling

- AMD FSR 1 e Snapdragon GSR, nitidez RCAS (do Cemu Plus)
  ([`2c5dba03`](https://github.com/dfdx047/Fractal/commit/2c5dba03), [`8328081a`](https://github.com/dfdx047/Fractal/commit/8328081a)).

### Geração de quadros

- Geração de quadros do Lossless Scaling (do Cemu Plus, que veio do lsfg-vk e do Eden) com o `Lossless.dll` do próprio
  usuário: multiplicador fixo 2x/3x/4x ou adaptativo até 60/90/120 FPS, resolução da estimativa de movimento,
  importação e ajustes pelas configurações e pelo menu do jogo. Desligada, não muda nada na criação do device Vulkan
  ([`0a4f24ce`](https://github.com/dfdx047/Fractal/commit/0a4f24ce), [`1fa22cec`](https://github.com/dfdx047/Fractal/commit/1fa22cec), [`eb14d852`](https://github.com/dfdx047/Fractal/commit/eb14d852)).
- Liberar o swapchain do GamePad com o jogo pausado não trava mais a interface ([`2171c12e`](https://github.com/dfdx047/Fractal/commit/2171c12e)).

### Depuração

- Relatório de depuração: registro de desempenho leve em toda sessão, `events.txt` com cada problema encontrado,
  vigia de travamento, notas de crash dizendo o que a thread fazia, log anterior guardado e **Compartilhar relatório de
  depuração** no app ([`be081807`](https://github.com/dfdx047/Fractal/commit/be081807)).
- Logs de crash e de travamento com código do console, registradores do host e pilhas das threads
  ([`c7923402`](https://github.com/dfdx047/Fractal/commit/c7923402), [`fcb9863e`](https://github.com/dfdx047/Fractal/commit/fcb9863e),
  [`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223)).
- Registro de desempenho, cenas de benchmark com roteiro e ferramentas de profiling (`tools/perf`).
- O relatório de depuração é oferecido na abertura seguinte depois de um crash (issue #2 no GitHub) ([`40d57923`](https://github.com/dfdx047/Fractal/commit/40d57923)).
- Vigia de escrita para código recompilado (`watch_jit=1`) ([`e79e82bd`](https://github.com/dfdx047/Fractal/commit/e79e82bd)); espera da GPU separada para cópias de
  superfícies lineares no registro de desempenho ([`52ff15de`](https://github.com/dfdx047/Fractal/commit/52ff15de)); toque no GamePad e toques contados em quadros nos
  roteiros de entrada ([`6a78d980`](https://github.com/dfdx047/Fractal/commit/6a78d980), [`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685)); 73 cenas de benchmark novas.
- `FRACTAL_TEST_BUILD=1` gera o "Fractal Teste", que instala ao lado do app oficial ([`a4363e5f`](https://github.com/dfdx047/Fractal/commit/a4363e5f)).

### Ganhos medidos

Das medições A/B nas mensagens de commit (mesma cena, mesmo aparelho, cache de shaders quente). As condições mudam
entre commits (driver, perfil de CPU), então compare cada linha só com ela mesma.

| Mudança | Aparelho | Antes → depois | Commit |
|---|---|---|---|
| Sem fim de render pass para envio de buffers / occlusion queries | RG Cube, Mario Kart 8 | 16,0 → 23,0 fps | [`2c1895ac`](https://github.com/dfdx047/Fractal/commit/2c1895ac) |
| Trabalho de vértices sobreposto às barreiras de render target | RG Cube, Mario Kart 8 | 24,3 → 30,2 fps | [`7b36890f`](https://github.com/dfdx047/Fractal/commit/7b36890f) |
| Compressão de framebuffer mantida | RP5, Super Mario 3D World | 57,0 → 59,8 fps, 1 % low 30 → 44 | [`017be0ef`](https://github.com/dfdx047/Fractal/commit/017be0ef) |
| Recompilador: contador de ciclos em registrador, área de código | RP5, Mario Kart 8 | 47,8 → 50,0-50,4 fps | [`23b10251`](https://github.com/dfdx047/Fractal/commit/23b10251) |
| Limpeza de depth por render pass | Mi A3, Shovel Knight | 34 → 52 fps | [`aabeea76`](https://github.com/dfdx047/Fractal/commit/aabeea76) |
| Sincronização do `GX2DrawDone` por jogo | RP5, Breath of the Wild | 9,4 → 11,1 fps | [`951eb1e7`](https://github.com/dfdx047/Fractal/commit/951eb1e7) |
| 1-2 threads de compilação de pipelines | Mi A3, SM3DW, cache frio | 17,0 s → 7,5 s em quadros acima de 100 ms | [`9893cf72`](https://github.com/dfdx047/Fractal/commit/9893cf72) |
| Pipelines pré-compilados batem com o cache do driver | Watch Dogs | congelamento de 38-68 s → 9 s ao entrar no jogo | [`69ac8320`](https://github.com/dfdx047/Fractal/commit/69ac8320) |
| Resultado das occlusion queries com a GPU ociosa | RP5 e POCO, Splinter Cell: Blacklist | 20 → 30 fps (limite do jogo) | [`1557a268`](https://github.com/dfdx047/Fractal/commit/1557a268) |
| Recompilador: formas com overflow, desvios longos, janelas para funções enormes | Mi A3, Teslagrad | 1 % low 3,7 → 10,3 fps; código host 118 → 39 MB | [`66f59e89`](https://github.com/dfdx047/Fractal/commit/66f59e89) |
| Aviso de resolução (640x360) | Mi A3: Kirby and the Rainbow Curse / NSMBU / Captain Toad | 12 → 27 / 20 → 34 / 16 → 30 fps | [`08c67298`](https://github.com/dfdx047/Fractal/commit/08c67298) |
| Driver do sistema em vez do Mesa no Rayman Legends | RP5 (Turnip) / POCO (PanVK) | 17 → 59 fps / 18 → 29,6 fps | [`1f6476e3`](https://github.com/dfdx047/Fractal/commit/1f6476e3) |
| AES-128 com a Crypto Extension | POCO, setor de 32 KiB | 32 MB/s → 4,1 GB/s | [`852801bb`](https://github.com/dfdx047/Fractal/commit/852801bb) |
| Instruções CRC32 | POCO | 1,2 → 13,6 GB/s | [`64efc3e5`](https://github.com/dfdx047/Fractal/commit/64efc3e5) |
| Máximo do index buffer com NEON | POCO, 30000 índices | u16 51,5 → 4,3 µs, u32 35,6 → 8,4 µs | [`bd6af3dc`](https://github.com/dfdx047/Fractal/commit/bd6af3dc) |
| Decodificação BCn por paleta, com NEON | POCO, 1024x1024 | BC1 4,0 → 1,5 ms, BC3 5,7 → 2,8, BC4 3,8 → 1,3, BC5 7,0 → 3,2 | [`c4e6b0aa`](https://github.com/dfdx047/Fractal/commit/c4e6b0aa) |

---

## Em andamento

- **Profiling de CPU na Mali:** no POCO X6 Pro (Bayonetta 2, Breath of the Wild, Xenoblade X, AC III) o laço ocioso do
  núcleo PowerPC principal gasta cerca de metade do tempo lendo o relógio e pegando as travas do timer e do áudio. Deixar
  essa espera sem travas não deu fps no RP5, no POCO nem no Mi A3, então ficou de fora; o próximo é a thread de render.
- **Need for Speed: Most Wanted U no POCO:** um crash durante o jogo com uma build anterior, não visto de novo em 7 runs.
- **The Binding of Isaac: Rebirth (USA):** roda (som, sem crash, 58,7 fps no RP5) com a imagem preta em todos os
  celulares; a tela do GamePad e a correção do logic op não mudam isso.
- **FIFA 13 no RP5:** crash na thread de áudio do jogo (lê o endereço 0) em cerca de 3 de 10 runs, com qualquer driver.
- **Splinter Cell: Blacklist:** nas missões só o céu e o chão são desenhados (a wiki de compatibilidade do Cemu cita o
  mesmo problema). Já descartados: occlusion queries, a pirâmide hi-Z e as consultas de mapeamento de memória.
- **Assassin's Creed III no POCO:** terreno e rochas próximos saem azul-escuro liso; em multi-core o jogo não envia o draw
  do terreno, em single-core ele é desenhado.
- **Watch Dogs no Turnip:** uma falha de página da GPU numa escrita de streamout pode travar a GPU depois de uns 25
  minutos. O driver do sistema não é afetado.
- **Watch Dogs superexposto no Android:** a partir da sala do Maurice a exposição automática pode travar clara demais em
  cerca de metade das sessões, no Turnip e no driver do sistema; a build de PC não é afetada. Já descartados: NaN/Inf nos
  pixel shaders, draws pulados durante a compilação assíncrona de pipelines e readback de texturas.
- **Watch Dogs na Mali** (RG Cube): a thread de renderização fica presa compilando os shaders de streamout enormes; menu
  em blocos e abertura preta.
- **Assassin's Creed IV, apagão na selva:** depois da correção do `fsel` o mundo ainda fica preto em cerca de 4 de cada
  10 passagens pela ponte (PC e Android). Já descartados: a função de tempo de quadro no interpretador, arredondamento
  de 25 bits do `frC` nas multiplicações e a reinterpretação linear do mapa de oclusão. Próximo passo: gravar o mapa de
  oclusão quadro a quadro em volta do apagão.
- **Assassin's Creed IV, cutscenes:** comparando com a build de PC; as espadas compridas ficam penduradas retas do mesmo
  jeito no PC e no RP5, ainda não confirmado como bug.
- **Odin 2 Mini no Turnip:** Tekken Tag Tournament 2 fica preto na abertura com o MrPurple T30; Batman e Watch Dogs
  travaram em parte das runs. O Turnip 27.0 R2 do StevenMXZ fecha dentro do driver ao compilar pipelines do Ninja
  Gaiden 3.
- **Ninja Gaiden 3 no Turnip:** cabeças escuras flutuando e céu estourado continuam depois da correção da profundidade
  de campo.
- **Need for Speed: Most Wanted U:** achar a causa raiz da folhagem branca (o pack da comunidade esconde o problema).
- **Xenoblade Chronicles X no driver do sistema da Qualcomm:** personagens como silhuetas brancas (bug do driver); por
  enquanto use o Turnip.
- **Mali em geral:** o POCO X6 Pro (Mali-G615) agora é aparelho de teste principal; o RG Cube, mantido no modo
  econômico de CPU, segue como referência para aparelhos Mali mais fracos.

## Planejado

- Medições com cena fixa no Odin 3 e no AYN Thor.
- Testar os drivers customizados para Samsung Xclipse num aparelho Exynos.
- Geração de quadros também na tela do GamePad e um botão rápido para ela no menu do jogo.
- Testes de compatibilidade nos principais jogos de Wii U e uma lista pública de compatibilidade.
- Um botão "marcar problema" dentro do jogo que salva uma captura e uma marca no log para os relatos de bug.
- Streamout em drivers Mali sem stores no pipeline de vértices e sem transform feedback (por exemplo com um passe de
  compute).
- Recursos de GPU que faltam, achados nos testes: compute draw calls, alpha-to-mask (`GX2SetAlphaToMaskReg`),
  `GX2ExpandDepthBuffer` e `SQ_VTX_SEMANTIC_CLEAR`, que nunca é aplicado.
- Mais desempenho no lado da CPU: Breath of the Wild e Mario Kart 8 no RP5 são limitados pelo núcleo PowerPC principal e
  pela thread de renderização, não pela GPU.
- Continuar acompanhando o `main` do Cemu oficial.
