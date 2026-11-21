<p align="center">
  <a href="README.md">English</a> · <b>Português (Brasil)</b> · <a href="README.es.md">Español</a>
</p>

<p align="center">
  <img src="docs/images/icon.png" alt="Ícone do Fractal" width="128" height="128">
</p>

<h1 align="center">Fractal</h1>

<p align="center">
  <b>Emulação de Wii U feita para portáteis e celulares Android.</b><br>
  Um fork do Cemu ajustado para GPUs Adreno e Mali, com uma interface nova pensada para controle.
</p>

<p align="center">
  <a href="https://github.com/dfdx047/Fractal/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/dfdx047/Fractal/total?style=flat-square&color=FF2D95&label=downloads"></a>
  <a href="https://github.com/dfdx047/Fractal/releases"><img alt="Última versão" src="https://img.shields.io/github/v/release/dfdx047/Fractal?include_prereleases&style=flat-square&label=vers%C3%A3o&color=8A5CF6"></a>
  <a href="#requisitos"><img alt="Android 11+" src="https://img.shields.io/badge/Android-11%2B-3DDC84?style=flat-square&logo=android&logoColor=white"></a>
  <a href="#requisitos"><img alt="ABI arm64-v8a" src="https://img.shields.io/badge/ABI-arm64--v8a-555555?style=flat-square"></a>
  <a href="#requisitos"><img alt="Vulkan 1.1+" src="https://img.shields.io/badge/Vulkan-1.1%2B-AC162C?style=flat-square&logo=vulkan&logoColor=white"></a>
  <a href="LICENSE.txt"><img alt="Licença MPL-2.0" src="https://img.shields.io/badge/licen%C3%A7a-MPL--2.0-orange?style=flat-square"></a>
  <a href="docs/ROADMAP.pt-BR.md"><img alt="Status: beta pública" src="https://img.shields.io/badge/status-beta%20p%C3%BAblica-FF2D95?style=flat-square"></a>
</p>

> [!WARNING]
> **O Fractal está em beta pública (v0.1-beta).** Muitos jogos rodam, muitos ainda não, e o mesmo jogo pode se
> comportar de um jeito diferente em outro aparelho ou driver de GPU. Faça backup dos seus saves e
> [conte o que encontrar](#como-reportar-bugs).

## Conteúdo

- [Destaques](#destaques)
- [Capturas de tela](#capturas-de-tela)
- [Requisitos](#requisitos)
- [Instalação](#instalação)
- [Primeiros passos](#primeiros-passos)
- [Drivers de GPU](#drivers-de-gpu)
- [Geração de quadros](#geração-de-quadros)
- [Segunda tela (GamePad)](#segunda-tela-gamepad)
- [Desempenho](#desempenho)
- [Problemas conhecidos](#problemas-conhecidos)
- [Controles](#controles)
- [O que o Fractal corrige](#o-que-o-fractal-corrige)
- [Como reportar bugs](#como-reportar-bugs)
- [Roadmap e changelog](#roadmap-e-changelog)
- [Como compilar](#como-compilar)
- [Créditos](#créditos)
- [Licença](#licença)

## Destaques

- **Feito para portáteis.** Toda tela funciona com controle, com uma legenda de botões de uma linha e layouts que cabem
  em telas baixas na horizontal (Retroid Pocket, Anbernic, AYN) e em celulares na vertical.
- **Cinco temas:** Solid, Liquid Glass, Blur, Material You e Synthwave, com papéis de parede estáticos ou animados (ou
  uma imagem sua) e uma animação de abertura para cada tema.
- **Uma biblioteca de verdade:** grade, carrossel ou lista; tempo de jogo, última vez jogado, favoritos e ordenação;
  configurações por jogo com opções de **Resolução e FPS** detectadas nos graphic packs da comunidade; exportar e
  importar save e cache de shaders.
- **Drivers de GPU com um toque:** catálogo de builds do Turnip para Adreno (MrPurple666, StevenMXZ, faux123, K11MCH1 e
  outros) e drivers experimentais para Mali, escolhidos no geral ou por jogo, com volta automática ao driver do sistema
  se um driver não conseguir abrir o jogo.
- **GamePad na segunda tela:** nos portáteis de duas telas (AYN Thor) ou com um monitor externo, a tela do GamePad vai
  para o outro display e aceita toque. Escala e tamanho da imagem do GamePad são ajustáveis no menu do jogo.
- **Geração de quadros (Lossless Scaling):** com o **seu** `Lossless.dll`, multiplica a taxa de quadros (2x, 3x, 4x) ou
  completa até 60, 90 ou 120 FPS. Liga, desliga e ajusta pelo menu do jogo.
- **Renderizador ajustado para GPUs de celular:** menos quebras de render pass nas GPUs por blocos (tile-based),
  compressão de framebuffer (UBWC/AFBC) mantida, contornos para bugs do driver da Qualcomm e correção das esperas
  bloqueantes de fence no Turnip.
- **Recompilador ARM64 mais rápido:** mais instruções PowerPC recompiladas em vez de interpretadas, e menos custo por bloco.
- **Controles na tela** com perfis, macros, turbo, alternância e deslizar entre botões; controles físicos mapeados
  automaticamente; atalhos (hotkeys) e um menu lateral dentro do jogo.
- **AMD FSR 1 e Snapdragon GSR** para upscaling, com nitidez RCAS.
- **Relatório de depuração embutido:** toda sessão guarda um registro leve, e um toque junta tudo o que um relato de bug
  precisa.
- **English, Português (Brasil) e Español**, escolhidos pelo idioma do aparelho na primeira abertura.

## Capturas de tela

**Temas**

<table>
  <tr>
    <td align="center"><img src="docs/images/screens/theme-solid.png" alt="Tema Solid, biblioteca em grade" width="400"><br><sub>Solid</sub></td>
    <td align="center"><img src="docs/images/screens/theme-liquid-glass.png" alt="Tema Liquid Glass, biblioteca em carrossel" width="400"><br><sub>Liquid Glass</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/screens/theme-blur.png" alt="Tema Blur, biblioteca em lista" width="400"><br><sub>Blur</sub></td>
    <td align="center"><img src="docs/images/screens/theme-material-you.png" alt="Tema Material You, biblioteca em grade" width="400"><br><sub>Material You</sub></td>
  </tr>
  <tr>
    <td align="center" colspan="2"><img src="docs/images/screens/theme-synthwave.png" alt="Tema Synthwave, biblioteca em carrossel" width="400"><br><sub>Synthwave</sub></td>
  </tr>
</table>

**Interface**

<table>
  <tr>
    <td align="center"><img src="docs/images/screens/welcome-setup.png" alt="Configuração inicial" width="400"><br><sub>Configuração inicial</sub></td>
    <td align="center"><img src="docs/images/screens/game-settings.png" alt="Configurações por jogo" width="400"><br><sub>Configurações por jogo</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/screens/drivers.png" alt="Página de drivers de GPU" width="400"><br><sub>Drivers de GPU</sub></td>
    <td align="center"><img src="docs/images/screens/settings.png" alt="Tela de configurações" width="400"><br><sub>Configurações</sub></td>
  </tr>
  <tr>
    <td align="center" colspan="2"><img src="docs/images/screens/ingame-menu.png" alt="Menu lateral no jogo" width="400"><br><sub>Menu no jogo</sub></td>
  </tr>
</table>

**Controles**

<table>
  <tr>
    <td align="center"><img src="docs/images/screens/controls-overlay.png" alt="Controles na tela durante o jogo" width="400"><br><sub>Controles na tela</sub></td>
    <td align="center"><img src="docs/images/screens/controls-edit.png" alt="Modo de edição dos controles na tela" width="400"><br><sub>Modo de edição</sub></td>
  </tr>
  <tr>
    <td align="center" colspan="2"><img src="docs/images/screens/controls-mapping.png" alt="Mapeamento de controle físico" width="400"><br><sub>Mapeamento de controle</sub></td>
  </tr>
</table>

## Requisitos

| | Mínimo | Observações |
|---|---|---|
| Android | **11** (API 30) | |
| CPU | **ARM 64 bits (arm64-v8a)** | Não há builds de 32 bits nem x86. |
| GPU | **Vulkan 1.1** | Adreno 6xx/7xx é o alvo principal. Mali funciona, com mais problemas (veja [Drivers de GPU](#drivers-de-gpu)). |
| RAM | Sem mínimo fixo | Testado com até 4 GB (Mi A3). Aparelhos com menos de 6 GB compilam shaders em uma thread só. |
| Armazenamento | Espaço para os jogos e os caches de shaders | |

O Fractal é desenvolvido e testado em:

- **Retroid Pocket 5**: Snapdragon 865, Adreno 650, Android 13
- **Anbernic RG Cube**: Unisoc T820, Mali-G57 MC4, Android 13
- **Xiaomi Mi A3**: Snapdragon 665, Adreno 610 (referência de celular de entrada)
- **AYN Odin 2 Mini**: Snapdragon 8 Gen 2, Adreno 740, Android 13
- **AYN Odin 3**: Snapdragon 8 Elite, Adreno 830, Android 15
- **AYN Thor**: Snapdragon 8 Gen 2, Adreno 740, Android 13, duas telas (GamePad na tela de baixo)

Emular o Wii U é pesado. Espere velocidade cheia nos jogos mais leves em aparelhos da classe do Snapdragon 865 e
taxas de quadros menores nos mais pesados (Breath of the Wild, Xenoblade Chronicles X). GPUs mais fracas, em especial
as Mali, vão rodar mais devagar.

## Instalação

1. Baixe o APK na página de [Releases](https://github.com/dfdx047/Fractal/releases).
2. Abra o arquivo no aparelho e permita a instalação de apps dessa origem quando o Android pedir.
3. Abra o **Fractal**. A configuração inicial passa por idioma, tema, papel de parede, pasta de jogos, chaves, graphic
   packs e driver de GPU. Dá para pular qualquer passo e reabrir depois em **Opções › Configuração inicial**.

O Fractal é instalado como um app separado (`dev.dfd.fractal`) e não mexe em outras versões do Cemu que você tenha.

## Primeiros passos

### Seus jogos

O Fractal **não** inclui nem baixa jogos, chaves ou firmware. Use cópias dos jogos que você tem, feitas do seu próprio
console e dos seus discos. Não peça nem compartilhe arquivos protegidos por direitos autorais neste repositório.

- Escolha a pasta dos jogos na configuração inicial ou em **Opções › Pastas de jogos**. O Fractal também procura nas
  pastas dentro dela.
- Formatos aceitos: **.wua**, **.wux**, **.wud**, **.iso** e **.rpx** (e pastas com `code/content/meta`).
- **Jogos descriptografados** (.wua, ou pastas `code/content/meta`) funcionam **sem chaves**.
- **Jogos criptografados** (.wud, .wux) precisam de um `keys.txt` com a chave de disco de cada jogo na pasta do Fractal.
- Atualizações e DLC: use um .wua que já as contenha, ou instale pelo **Opções › Gerenciador de títulos**. O
  gerenciador também converte conteúdo instalado para .wua.
- Recursos online precisam do `otp.bin` e do `seeprom.bin` tirados do **seu** console.

O menu **Opções** fica na barra superior da biblioteca (ou no **Start** do controle). A pasta do Fractal é
`Android/data/dev.dfd.fractal/files`; **Opções › Abrir pasta do Fractal** abre ela num gerenciador de arquivos.

### Graphic packs

Os graphic packs da comunidade trazem opções de resolução e de taxa de quadros e corrigem problemas de alguns jogos.
Baixe na configuração inicial ou em **Opções › Graphic packs**; eles são muito recomendados. Algumas correções da
comunidade conferidas nos aparelhos de teste já vêm ligadas (por exemplo o Bloom Fix do Need for Speed: Most Wanted U)
e podem ser desligadas.

### Configurações por jogo

Aperte **Y** com um jogo selecionado (ou escolha **Editar perfil do jogo** no menu dele) para abrir as configurações
dele: Resolução e FPS, CPU, gráficos, driver de GPU, graphic packs, saves e cache de shaders. Um toque longo no jogo (ou
**Select**) abre o menu dele: exportar e importar save, exportar e importar cache de shaders, apagar caches de shaders,
criar atalho na tela inicial.

## Drivers de GPU

No Android o driver da GPU pesa tanto quanto o emulador. O Fractal deixa escolher um no geral ou um para cada jogo.

**Adreno (Snapdragon)**

- **Driver do sistema (Qualcomm):** funciona sem instalar nada. Alguns jogos esbarram em bugs do compilador de shaders
  dele; o Fractal contorna os que foram encontrados até agora.
- **Turnip (Mesa):** driver de código aberto, muitas vezes mais rápido e compatível. **Opções › Drivers** lista as
  builds por desenvolvedor e instala com um toque. A série T do MrPurple666 para Adreno 6xx/7xx aparece primeiro e é um
  bom ponto de partida.
- No Turnip o Fractal renderiza na memória do sistema por padrão (opção por jogo), o que evita travamentos de GPU vistos
  com a renderização em blocos.

**Mali**

- O driver do sistema é o padrão. Drivers customizados (builds do PanVK e o malisx2) podem ser instalados pela mesma
  página, mas são **experimentais**.

**Se um driver falhar** ao abrir um jogo, o Fractal roda o jogo no driver do sistema e, na abertura seguinte, volta
para o driver do sistema a configuração que escolheu aquele driver e explica o motivo. Trocar de driver apaga só o cache
de pipelines daquele driver; o seu cache de shaders fica.

Se um jogo aparecer com gráficos quebrados ou fechar sozinho, a primeira coisa a fazer é testar outro driver, e o
driver usado é a informação mais útil num relato de bug.

## Geração de quadros

A geração de quadros cria quadros intermediários entre os quadros do jogo, com os shaders do
[Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) (portado do Cemu Plus, que veio do
lsfg-vk e do Eden). O Fractal **não** inclui nem baixa o Lossless Scaling: você importa o `Lossless.dll` da **sua**
cópia (fica na pasta de instalação dele no Windows). O arquivo só é lido, nunca executado.

1. Copie o `Lossless.dll` para o aparelho.
2. Importe em **Configurações › Gráficos › Geração de quadros** ou no **menu do jogo › Geração de quadros**. Ele vale
   para todos os jogos.
3. Ligue a geração de quadros e reabra o jogo (os recursos de Vulkan que ela usa são ativados quando o jogo abre).

Ajustes, todos também no menu do jogo e aplicados na hora:

- **Modo:** *multiplicador fixo* (2x, 3x ou 4x cada quadro) ou *adaptativo* (gera só o que falta para chegar a 60, 90 ou
  120 FPS).
- **Resolução da estimativa de movimento:** automática, 50 %, 75 % ou 100 %. Menor pesa menos na GPU.

Ela funciona só na tela da TV e força o VSync enquanto está ligada. Precisa de um driver com Vulkan memory model, float16
e null descriptors; o status no menu diz se o seu driver tem. O contador de FPS mostra os quadros
exibidos, o multiplicador e o FPS base do jogo. Exemplo: The Wind Waker HD no AYN Thor, 30 FPS base → 60 FPS com 2x.

Os quadros gerados não mudam a resposta dos controles nem a velocidade do jogo: um jogo a 20 FPS continua respondendo
como 20 FPS.

## Segunda tela (GamePad)

Com **Mostrar tela do GamePad** ligado no menu do jogo, o GamePad aparece:

- **na outra tela**, se houver uma (AYN Thor, monitor ou TV ligados por cabo). A opção **GamePad na segunda tela** aparece
  quando o Fractal acha esse display, e dá para desligá-la para ver o GamePad ao lado da TV;
- **ao lado da TV** na mesma tela, na posição escolhida em **Configurações › Configurações gerais**.

**Escala da tela do GamePad** (manter proporção ou esticar) e **Tamanho da tela do GamePad** (25 % a 100 %) ficam no
menu do jogo, e o toque acompanha a imagem. **Trocar TV e GamePad** coloca a imagem da TV na tela do GamePad e vice-versa.
As escolhas ficam salvas para os próximos jogos.

## Desempenho

FPS médio / 1 % low numa cena fixa de cada jogo (roteiro de entradas repetido, cache de shaders quente, resolução
nativa, build final de 15/11/2026). O RP5 fica num perfil de clock travado e o RG Cube no modo econômico de CPU, para as
medições serem comparáveis; na configuração de fábrica eles rodam mais rápido.

| Jogo | RP5, Turnip (MrPurple T19) | RP5, driver do sistema | RG Cube, Mali | Odin 2 Mini, Turnip (MrPurple T30) |
|---|---|---|---|---|
| Ninja Gaiden 3: Razor's Edge | 38,6 / 23,8 | 41,7 / 27,8 | 32,2 / 13,0 | 60,0 / 57,5 |
| The Amazing Spider-Man 2 | 36,9 / 22,0 | 38,1 / 21,7 | 15,0 / 11,5 | 60,0 / 57,6 |
| Tekken Tag Tournament 2 | 46,6 / 15,8 | 41,9 / 10,3 | | |
| Batman: Arkham Origins | 25,9 / 16,0 | 28,3 / 18,4 | | |
| Watch Dogs | 25,9 / 10,9 | 24,3 / 9,0 | trava | |
| Assassin's Creed IV: Black Flag (abertura) | 22,1 / 13,1 | 23,1 / 13,8 | | |

- **Odin 2 Mini** (Snapdragon 8 Gen 2) segura 60 FPS em Ninja Gaiden 3 e Spider-Man 2. Tekken, Batman e Watch Dogs
  ainda travaram ou fecharam em parte das runs com o Turnip, por isso ficam fora da tabela.
- **Odin 3** (Snapdragon 8 Elite): Ninja Gaiden 3 (Turnip) e Assassin's Creed IV (driver do sistema) ficam em 60 FPS na
  maior parte do tempo. As medições com cena fixa ainda vão ser feitas.
- **AYN Thor:** The Wind Waker HD a 30 FPS (o limite do jogo), 60 com geração de quadros 2x.
- **Mi A3** (Adreno 610): Shovel Knight a 52 FPS. Jogos 3D pesados ficam lentos.
- **PC** (build de Linux do Fractal, NVIDIA): Watch Dogs 42,6 / 28,0.

## Problemas conhecidos

- **Assassin's Creed IV:** na selva (a missão de achar o assassino, perto da ponte e da cachoeira) o mundo pode ficar
  preto, só com o HUD. A correção do `fsel` do recompilador deixou isso bem mais raro, mas ainda acontece. Também no PC.
- **Watch Dogs no Android:** a partir da sala do Maurice a imagem pode ficar clara demais em cerca de metade das sessões.
  No Turnip uma falha de página da GPU pode travar o jogo depois de uns 25 minutos. Na Mali a abertura fica preta.
- **Ninja Gaiden 3 no Turnip:** algumas cabeças escuras e o céu estourado em certas fases.
- **Need for Speed: Most Wanted U sem a atualização (v0):** à noite a folhagem e as luzes ficam brancas, estouradas, e a
  imagem pode ficar escura, porque o pack "Bloom Fix" só corrige o jogo com a atualização v32 instalada. Por enquanto,
  instale a atualização v32.
- **Xenoblade Chronicles X no driver do sistema da Qualcomm:** personagens viram silhuetas brancas; use o Turnip.
- **Mali:** mais lenta e com mais problemas que a Adreno; drivers customizados para Mali são experimentais.

A lista completa, com o que está em andamento, está no [roadmap](docs/ROADMAP.pt-BR.md#em-andamento).

## Controles

- **Controles na tela** aparecem por padrão quando nenhum controle está conectado. No menu do jogo dá para editar:
  mover, redimensionar, alinhar à grade, mudar opacidade, estilo e formato de cada botão, salvar layouts como perfis,
  fazer um botão alternar ou repetir (turbo), criar macros (combinações ou sequências com tempo) e botões de atalho que
  executam ações do emulador.
- **Controles físicos** (os controles embutidos do portátil, controles Bluetooth e USB) são mapeados automaticamente
  num controle emulado vazio quando conectados. O jogador 1 é um Wii U GamePad por padrão. Os mapeamentos podem ser
  mudados em **Configurações › Controles**, e o botão HOME do Wii U fica no botão Mode/Guide do controle.
- **Atalhos (hotkeys):** pausar, capturar a tela, trocar tela da TV/GamePad, mostrar ou esconder os controles na tela,
  contador de FPS, proporção da tela, filtro de upscaling e movimento.
- **Menu no jogo:** tela do GamePad (na segunda tela, com escala e tamanho próprios), troca TV/GamePad, escala, geração de quadros, contador de FPS, filtro de upscaling e nitidez, VSync,
  controles na tela, mapeamento de controle, atalhos, pausa e capturas de tela (salvas em `Pictures/Fractal`).

## O que o Fractal corrige

O Fractal começou a partir do port de Android do Cemu feito pelo SSimco e desde então ganhou mais de 180 commits
próprios, além de um merge do branch `main` do Cemu oficial. Parte das correções está no núcleo do emulador que a
versão de PC também usa, então também resolve problemas que existem no Cemu oficial:

| Jogo | Problema | Corrigido com |
|---|---|---|
| The Amazing Spider-Man | Nunca mostra imagem ("no display" na wiki de compatibilidade do Cemu): o jogo copia os quadros para o scan buffer com `GX2CopySurface` | Quadros copiados assim são apresentados. Jogável na build de PC do Fractal |
| Watch Dogs | Fecha na abertura e durante o jogo, imagem congelada a partir do prólogo | Memória do console reservada com o alinhamento que o alocador do jogo precisa (o Cemu oficial tem o mesmo problema), resultados de streamout copiados de volta para a memória do console, draws indexados com streamout na ordem do índice, sem descarte de faces em primitivas rect, corrida na abertura corrigida |
| Injustice: Gods Among Us | Travamento da GPU, lutadores esticados | Constantes de laço dos shaders emuladas, offsets de streamout corrigidos |
| Need for Speed: Most Wanted U | Estalos e chiados no som do motor | `AXDecodeAdpcmData` implementado, histórico do filtro biquad em precisão total |
| Todos os jogos | Shaders pré-compilados na abertura eram montados com o estado errado e compilados de novo durante o jogo | Estado do pipeline montado a partir dos registradores do cache (o Cemu oficial tem o mesmo bug) |

E muitas correções para GPUs e drivers do Android: Tekken Tag Tournament 2, Assassin's Creed IV, Ninja Gaiden 3:
Razor's Edge, Xenoblade Chronicles X, Bayonetta 2, Pokkén Tournament, Breath of the Wild e outros. A lista completa, com
os commits de cada item, está no [roadmap](docs/ROADMAP.pt-BR.md).

## Como reportar bugs

Relatos de bug com o relatório de depuração anexado são o caminho mais rápido para um jogo ser corrigido.

1. Reproduza o problema (fechamento, travamento ou defeito gráfico) e abra o Fractal de novo.
2. Na biblioteca, abra **Opções › Compartilhar relatório de depuração**. O Fractal gera o `debug_report.zip` com o
   `log.txt`, o log da sessão anterior (útil depois de um fechamento), as duas últimas sessões de jogo (aparelho,
   driver, configurações, tempos de quadro e problemas encontrados), o `settings.xml` e os perfis desses jogos.
3. Salve ou envie o zip, abra uma [nova issue](https://github.com/dfdx047/Fractal/issues/new/choose) e anexe o arquivo.

Informe o aparelho, o SoC, a versão do Android, o driver de GPU, o jogo com o **Title ID** e a versão, e os passos para
reproduzir. Os formulários de issue estão em inglês, mas pode escrever em português. O `settings.xml` tem os caminhos
das suas pastas de jogos; tire o que não quiser compartilhar.

**Não reporte bugs do Fractal para a equipe do Cemu.** Se o jogo também falha do mesmo jeito no Cemu oficial no PC,
diga isso na issue.

## Roadmap e changelog

- [Roadmap](docs/ROADMAP.pt-BR.md): o que está feito, o que está em andamento e o que está planejado
- [Changelog](CHANGELOG.md) (em inglês)

## Como compilar

Veja o [BUILD.md](BUILD.md#android) (em inglês). O projeto Android fica em `src/android` (Gradle, NDK). Contribuições são bem-vindas;
leia o [CONTRIBUTING.md](CONTRIBUTING.md) antes.

## Créditos

O Fractal se apoia no trabalho de:

- **[Cemu](https://github.com/cemu-project/Cemu)**, da equipe do Cemu: o núcleo do emulador (recompilador PowerPC,
  emulação da GPU Latte, Cafe OS, áudio, entrada) e a maior parte do código deste repositório.
- **[Port de Android do Cemu](https://github.com/SSimco/Cemu)**, do SSimco: a build de Android, a camada JNI, o app em
  Kotlin/Jetpack Compose e o trabalho de ARM64 de onde o Fractal saiu.
- **[Cemu Plus](https://github.com/danielcamilo1/cemu-plus)**, do danielcamilo1: upscaling AMD FSR 1 e Snapdragon GSR com
  nitidez RCAS, a geração de quadros com Lossless Scaling, a correção de atômicos de 64 bits desalinhados (Star Fox
  Zero), detalhes de crash no log e duas correções de texturas e de log.
- **[lsfg-vk](https://github.com/PancakeTAS/lsfg-vk)** e **[Eden](https://git.eden-emu.dev/eden-emu/eden)**: a
  implementação de Vulkan da geração de quadros do Lossless Scaling que o Cemu Plus portou.
- **[SapphireRhodonite](https://github.com/SapphireRhodonite/Cemu)**: a base do GamePad na segunda tela
  (branch `android-port-dual`).
- **Projetos de drivers de GPU** listados na página de drivers do app, cada um com nome e link. O Fractal não inclui
  nenhum driver.
- Comunidade dos **[graphic packs do Cemu](https://github.com/cemu-project/cemu_graphic_packs)**.
- **Fontes:** Space Grotesk e Inter (SIL Open Font License 1.1).

Detalhes no [NOTICE.md](NOTICE.md).

## Licença

O Fractal usa a [Mozilla Public License 2.0](LICENSE.txt), como o Cemu. Os arquivos em `dependencies/` e alguns
arquivos em `src/` mantêm as licenças indicadas nos seus cabeçalhos. A geração de quadros
(`src/Cafe/HW/Latte/Renderer/Vulkan/FrameGen/`) é **GPL-3.0 ou posterior**, então o APK, que a inclui, também segue os
termos da GPL-3.0. Veja o [NOTICE.md](NOTICE.md).

O Fractal não tem relação com a Nintendo nem é endossado por ela. Wii U é marca registrada da Nintendo. O Fractal não é
uma versão oficial do Cemu.
