<p align="center">
  <a href="README.md">English</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <b>Español</b>
</p>

<p align="center">
  <img src="docs/images/icon.png" alt="Icono de Fractal" width="128" height="128">
</p>

<h1 align="center">Fractal</h1>

<p align="center">
  <b>Emulación de Wii U hecha para consolas portátiles y teléfonos Android.</b><br>
  Un fork de Cemu ajustado para GPUs Adreno y Mali, con una interfaz nueva pensada para mando.
</p>

<p align="center">
  <a href="https://github.com/dfdx047/Fractal/releases"><img alt="Descargas" src="https://img.shields.io/github/downloads/dfdx047/Fractal/total?style=flat-square&color=FF2D95&label=descargas"></a>
  <a href="https://github.com/dfdx047/Fractal/releases"><img alt="Última versión" src="https://img.shields.io/github/v/release/dfdx047/Fractal?include_prereleases&style=flat-square&label=versi%C3%B3n&color=8A5CF6"></a>
  <a href="#requisitos"><img alt="Android 11+" src="https://img.shields.io/badge/Android-11%2B-3DDC84?style=flat-square&logo=android&logoColor=white"></a>
  <a href="#requisitos"><img alt="ABI arm64-v8a" src="https://img.shields.io/badge/ABI-arm64--v8a-555555?style=flat-square"></a>
  <a href="#requisitos"><img alt="Vulkan 1.1+" src="https://img.shields.io/badge/Vulkan-1.1%2B-AC162C?style=flat-square&logo=vulkan&logoColor=white"></a>
  <a href="LICENSE.txt"><img alt="Licencia MPL-2.0" src="https://img.shields.io/badge/licencia-MPL--2.0-orange?style=flat-square"></a>
  <a href="docs/ROADMAP.es.md"><img alt="Estado: beta pública" src="https://img.shields.io/badge/estado-beta%20p%C3%BAblica-FF2D95?style=flat-square"></a>
</p>

> [!WARNING]
> **Fractal está en beta pública (v0.1-beta).** Muchos juegos funcionan, muchos todavía no, y el mismo juego puede
> comportarse distinto en otro dispositivo o driver de GPU. Haz copias de seguridad de tus partidas y
> [cuéntanos lo que encuentres](#cómo-reportar-errores).

## Contenido

- [Destacados](#destacados)
- [Capturas de pantalla](#capturas-de-pantalla)
- [Requisitos](#requisitos)
- [Instalación](#instalación)
- [Primeros pasos](#primeros-pasos)
- [Drivers de GPU](#drivers-de-gpu)
- [Generación de fotogramas](#generación-de-fotogramas)
- [Segunda pantalla (GamePad)](#segunda-pantalla-gamepad)
- [Rendimiento](#rendimiento)
- [Problemas conocidos](#problemas-conocidos)
- [Controles](#controles)
- [Qué corrige Fractal](#qué-corrige-fractal)
- [Cómo reportar errores](#cómo-reportar-errores)
- [Roadmap y changelog](#roadmap-y-changelog)
- [Cómo compilar](#cómo-compilar)
- [Créditos](#créditos)
- [Licencia](#licencia)

## Destacados

- **Hecho para portátiles.** Todas las pantallas funcionan con mando, con una leyenda de botones de una línea y diseños
  que caben en pantallas bajas en horizontal (Retroid Pocket, Anbernic, AYN) y en teléfonos en vertical.
- **Cinco temas:** Solid, Liquid Glass, Blur, Material You y Synthwave, con fondos estáticos o animados (o una imagen
  tuya) y una animación de inicio para cada tema.
- **Una biblioteca de verdad:** cuadrícula, carrusel o lista; tiempo de juego, última partida, favoritos y orden;
  ajustes por juego con opciones de **Resolución y FPS** detectadas en los paquetes gráficos de la comunidad; exportar e
  importar partidas y caché de shaders.
- **Drivers de GPU con un toque:** catálogo de builds de Turnip para Adreno (MrPurple666, StevenMXZ, faux123, K11MCH1 y
  otros) y drivers experimentales para Mali, elegidos en general o por juego, con vuelta automática al driver del
  sistema si un driver no logra abrir el juego.
- **GamePad en la segunda pantalla:** en las portátiles de dos pantallas (AYN Thor) o con un monitor externo, la
  pantalla del GamePad pasa a la otra pantalla y acepta toques. La escala y el tamaño de la imagen del GamePad se ajustan
  en el menú del juego.
- **Generación de fotogramas (Lossless Scaling):** con **tu** `Lossless.dll`, multiplica la tasa de fotogramas (2x, 3x,
  4x) o completa hasta 60, 90 o 120 FPS. Se activa, se desactiva y se ajusta desde el menú del juego.
- **Renderizador ajustado para GPUs móviles:** menos cortes de render pass en GPUs por bloques (tile-based), compresión
  de framebuffer (UBWC/AFBC) activa, soluciones para bugs del driver de Qualcomm y corrección de las esperas bloqueantes
  de fence en Turnip.
- **Recompilador ARM64 más rápido:** más instrucciones PowerPC recompiladas en lugar de interpretadas, y menos coste por bloque.
- **Controles en pantalla** con perfiles, macros, turbo, alternar y deslizar entre botones; mandos físicos asignados
  automáticamente; atajos (hotkeys) y un menú lateral dentro del juego.
- **AMD FSR 1 y Snapdragon GSR** para escalado, con nitidez RCAS.
- **Informe de depuración integrado:** cada sesión guarda un registro ligero, y un toque reúne todo lo que necesita un
  reporte de error.
- **English, Português (Brasil) y Español**, elegidos según el idioma del dispositivo la primera vez.

## Capturas de pantalla

**Temas**

<table>
  <tr>
    <td align="center"><img src="docs/images/screens/theme-solid.png" alt="Tema Solid, biblioteca en cuadrícula" width="400"><br><sub>Solid</sub></td>
    <td align="center"><img src="docs/images/screens/theme-liquid-glass.png" alt="Tema Liquid Glass, biblioteca en carrusel" width="400"><br><sub>Liquid Glass</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/screens/theme-blur.png" alt="Tema Blur, biblioteca en lista" width="400"><br><sub>Blur</sub></td>
    <td align="center"><img src="docs/images/screens/theme-material-you.png" alt="Tema Material You, biblioteca en cuadrícula" width="400"><br><sub>Material You</sub></td>
  </tr>
  <tr>
    <td align="center" colspan="2"><img src="docs/images/screens/theme-synthwave.png" alt="Tema Synthwave, biblioteca en carrusel" width="400"><br><sub>Synthwave</sub></td>
  </tr>
</table>

**Interfaz**

<table>
  <tr>
    <td align="center"><img src="docs/images/screens/welcome-setup.png" alt="Configuración inicial" width="400"><br><sub>Configuración inicial</sub></td>
    <td align="center"><img src="docs/images/screens/game-settings.png" alt="Ajustes por juego" width="400"><br><sub>Ajustes por juego</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/screens/drivers.png" alt="Página de drivers de GPU" width="400"><br><sub>Drivers de GPU</sub></td>
    <td align="center"><img src="docs/images/screens/settings.png" alt="Pantalla de configuración" width="400"><br><sub>Configuración</sub></td>
  </tr>
  <tr>
    <td align="center" colspan="2"><img src="docs/images/screens/ingame-menu.png" alt="Menú lateral en el juego" width="400"><br><sub>Menú en el juego</sub></td>
  </tr>
</table>

**Controles**

<table>
  <tr>
    <td align="center"><img src="docs/images/screens/controls-overlay.png" alt="Controles en pantalla durante el juego" width="400"><br><sub>Controles en pantalla</sub></td>
    <td align="center"><img src="docs/images/screens/controls-edit.png" alt="Modo de edición de los controles en pantalla" width="400"><br><sub>Modo de edición</sub></td>
  </tr>
  <tr>
    <td align="center" colspan="2"><img src="docs/images/screens/controls-mapping.png" alt="Asignación de mando físico" width="400"><br><sub>Asignación de mando</sub></td>
  </tr>
</table>

## Requisitos

| | Mínimo | Notas |
|---|---|---|
| Android | **11** (API 30) | |
| CPU | **ARM de 64 bits (arm64-v8a)** | No hay builds de 32 bits ni x86. |
| GPU | **Vulkan 1.1** | Adreno 6xx/7xx es el objetivo principal. Mali funciona, con más problemas (ver [Drivers de GPU](#drivers-de-gpu)). |
| RAM | Sin mínimo fijo | Probado con hasta 4 GB (Mi A3). Los dispositivos con menos de 6 GB compilan shaders en un solo hilo. |
| Almacenamiento | Espacio para los juegos y las cachés de shaders | |

Fractal se desarrolla y se prueba en:

- **Retroid Pocket 5**: Snapdragon 865, Adreno 650, Android 13
- **Anbernic RG Cube**: Unisoc T820, Mali-G57 MC4, Android 13
- **Xiaomi Mi A3**: Snapdragon 665, Adreno 610 (referencia de teléfono de gama baja)
- **AYN Odin 2 Mini**: Snapdragon 8 Gen 2, Adreno 740, Android 13
- **AYN Odin 3**: Snapdragon 8 Elite, Adreno 830, Android 15
- **AYN Thor**: Snapdragon 8 Gen 2, Adreno 740, Android 13, dos pantallas (GamePad en la pantalla de abajo)

Emular Wii U es exigente. Espera velocidad completa en los juegos más ligeros en dispositivos de la clase del
Snapdragon 865 y menos cuadros por segundo en los más pesados (Breath of the Wild, Xenoblade Chronicles X). Las GPUs más
débiles, sobre todo las Mali, irán más lentas.

## Instalación

1. Descarga el APK en la página de [Releases](https://github.com/dfdx047/Fractal/releases).
2. Ábrelo en el dispositivo y permite instalar apps de ese origen cuando Android lo pida.
3. Abre **Fractal**. La configuración inicial recorre idioma, tema, fondo, carpeta de juegos, claves, paquetes gráficos y
   driver de GPU. Cualquier paso se puede saltar y volver a abrir luego en **Opciones › Configuración inicial**.

Fractal se instala como una app aparte (`dev.dfd.fractal`) y no toca otras versiones de Cemu que tengas.

## Primeros pasos

### Tus juegos

Fractal **no** incluye ni descarga juegos, claves ni firmware. Usa copias de juegos que tengas, hechas desde tu propia
consola y tus discos. No pidas ni compartas archivos protegidos por derechos de autor en este repositorio.

- Elige la carpeta de los juegos en la configuración inicial o en **Opciones › Carpetas de juegos**. Fractal también
  busca en las carpetas que hay dentro.
- Formatos admitidos: **.wua**, **.wux**, **.wud**, **.iso** y **.rpx** (y carpetas con `code/content/meta`).
- **Juegos descifrados** (.wua, o carpetas `code/content/meta`) funcionan **sin claves**.
- **Juegos cifrados** (.wud, .wux) necesitan un `keys.txt` con la clave de disco de cada juego en la carpeta de Fractal.
- Actualizaciones y DLC: usa un .wua que ya las incluya, o instálalas con **Opciones › Administrador de títulos**. El
  administrador también convierte contenido instalado a .wua.
- Las funciones en línea necesitan `otp.bin` y `seeprom.bin` extraídos de **tu** consola.

El menú **Opciones** está en la barra superior de la biblioteca (o en **Start** del mando). La carpeta de Fractal es
`Android/data/dev.dfd.fractal/files`; **Opciones › Abrir carpeta de Fractal** la abre en un gestor de archivos.

### Paquetes gráficos

Los paquetes gráficos (graphic packs) de la comunidad añaden opciones de resolución y de cuadros por segundo y corrigen
problemas de algunos juegos. Descárgalos en la configuración inicial o en **Opciones › Paquetes gráficos**; son muy
recomendables. Algunas correcciones de la comunidad comprobadas en los dispositivos de prueba vienen activadas (por
ejemplo el Bloom Fix de Need for Speed: Most Wanted U) y se pueden desactivar.

### Ajustes por juego

Pulsa **Y** con un juego seleccionado (o elige **Editar perfil del juego** en su menú) para abrir sus ajustes:
Resolución y FPS, CPU, gráficos, driver de GPU, paquetes gráficos, partidas y caché de shaders. Una pulsación larga
sobre el juego (o **Select**) abre su menú: exportar e importar partida, exportar e importar caché de shaders, borrar
cachés de shaders, crear acceso directo.

## Drivers de GPU

En Android el driver de la GPU importa tanto como el emulador. Fractal permite elegir uno en general o uno por juego.

**Adreno (Snapdragon)**

- **Driver del sistema (Qualcomm):** funciona sin instalar nada. Algunos juegos chocan con bugs de su compilador de
  shaders; Fractal sortea los encontrados hasta ahora.
- **Turnip (Mesa):** driver de código abierto, a menudo más rápido y compatible. **Opciones › Drivers** muestra las
  builds por desarrollador y las instala con un toque. La serie T de MrPurple666 para Adreno 6xx/7xx aparece primero y
  es un buen punto de partida.
- En Turnip, Fractal renderiza en la memoria del sistema por defecto (opción por juego), lo que evita cuelgues de la GPU
  vistos con el renderizado por bloques.

**Mali**

- El driver del sistema es el predeterminado. Los drivers personalizados (builds de PanVK y malisx2) se pueden instalar
  desde la misma página, pero son **experimentales**.

**Si un driver falla** al abrir un juego, Fractal lo ejecuta con el driver del sistema y, en el siguiente inicio,
devuelve al driver del sistema el ajuste que eligió ese driver y explica el motivo. Cambiar de driver borra solo la
caché de pipelines de ese driver; tu caché de shaders se conserva.

Si un juego se ve mal o se cierra, lo primero es probar otro driver, y el driver usado es el dato más útil en un
reporte de error.

## Generación de fotogramas

La generación de fotogramas crea fotogramas intermedios entre los fotogramas del juego, con los shaders de
[Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) (portado de Cemu Plus, que lo tomó de
lsfg-vk y de Eden). Fractal **no** incluye ni descarga Lossless Scaling: importas `Lossless.dll` de **tu** copia (está
en su carpeta de instalación en Windows). El archivo solo se lee, nunca se ejecuta.

1. Copia `Lossless.dll` al dispositivo.
2. Impórtala en **Configuración › Gráficos › Generación de fotogramas** o en el **menú del juego › Generación de
   fotogramas**. Sirve para todos los juegos.
3. Activa la generación de fotogramas y vuelve a abrir el juego (las funciones de Vulkan que usa se activan al abrir el
   juego).

Ajustes, todos también en el menú del juego y aplicados al momento:

- **Modo:** *Multiplicador fijo* (2x, 3x o 4x cada fotograma) o *Adaptativo* (genera solo lo que falta para llegar a
  60, 90 o 120 FPS).
- **Resolución de la estimación de movimiento:** Automático, 50 %, 75 % o 100 %. Cuanto menor, menos carga la GPU.

Solo funciona en la pantalla de la TV y fuerza el VSync mientras está activa. Necesita un driver con Vulkan memory model,
float16 y null descriptors; el estado en el menú indica si tu driver los tiene. El contador de FPS muestra los
fotogramas mostrados, el multiplicador y los FPS base del juego. Ejemplo: The Wind Waker HD en el AYN Thor, 30 FPS base
→ 60 FPS con 2x.

Los fotogramas generados no cambian la respuesta de los controles ni la velocidad del juego: un juego a 20 FPS sigue
respondiendo como a 20 FPS.

## Segunda pantalla (GamePad)

Con **Mostrar pantalla del GamePad** activado en el menú del juego, el GamePad aparece:

- **en la otra pantalla**, si hay una (AYN Thor, monitor o TV conectados por cable). La opción **GamePad en la segunda
  pantalla** aparece cuando Fractal encuentra esa pantalla, y se puede desactivar para ver el GamePad junto a la TV;
- **junto a la TV** en la misma pantalla, en la posición elegida en **Configuración › Configuración general**.

**Escala de la pantalla del GamePad** (Mantener relación de aspecto o Estirar) y **Tamaño de la pantalla del GamePad**
(25 % a 100 %) están en el menú del juego, y el toque sigue a la imagen. **Intercambiar TV y GamePad** pone la imagen de
la TV en la pantalla del GamePad y viceversa. Las elecciones se guardan para los siguientes juegos.

## Rendimiento

FPS medios / 1 % low en una escena fija de cada juego (guion de entradas repetido, caché de shaders caliente, resolución
nativa, build final del 2026-11-15). La RP5 está en un perfil de reloj fijo y la RG Cube en el modo económico de CPU,
para que las mediciones sean comparables; con la configuración de fábrica van más rápido.

| Juego | RP5, Turnip (MrPurple T19) | RP5, driver del sistema | RG Cube, Mali | Odin 2 Mini, Turnip (MrPurple T30) |
|---|---|---|---|---|
| Ninja Gaiden 3: Razor's Edge | 38,6 / 23,8 | 41,7 / 27,8 | 32,2 / 13,0 | 60,0 / 57,5 |
| The Amazing Spider-Man 2 | 36,9 / 22,0 | 38,1 / 21,7 | 15,0 / 11,5 | 60,0 / 57,6 |
| Tekken Tag Tournament 2 | 46,6 / 15,8 | 41,9 / 10,3 | | |
| Batman: Arkham Origins | 25,9 / 16,0 | 28,3 / 18,4 | | |
| Watch Dogs | 25,9 / 10,9 | 24,3 / 9,0 | se cuelga | |
| Assassin's Creed IV: Black Flag (inicio) | 22,1 / 13,1 | 23,1 / 13,8 | | |

- **Odin 2 Mini** (Snapdragon 8 Gen 2) mantiene 60 FPS en Ninja Gaiden 3 y Spider-Man 2. Tekken, Batman y Watch Dogs
  todavía se colgaron o se cerraron en parte de las ejecuciones con Turnip, por eso quedan fuera de la tabla.
- **Odin 3** (Snapdragon 8 Elite): Ninja Gaiden 3 (Turnip) y Assassin's Creed IV (driver del sistema) se mantienen en
  60 FPS la mayor parte del tiempo. Las mediciones con escena fija aún están por hacer.
- **AYN Thor:** The Wind Waker HD a 30 FPS (el límite del juego), 60 con generación de fotogramas 2x.
- **Mi A3** (Adreno 610): Shovel Knight a 52 FPS. Los juegos 3D pesados van lentos.
- **PC** (build de Linux de Fractal, NVIDIA): Watch Dogs 42,6 / 28,0.

## Problemas conocidos

- **Assassin's Creed IV:** en la selva (la misión de encontrar al asesino, cerca del puente y la cascada) el mundo puede
  quedar negro, solo con el HUD. La corrección del `fsel` del recompilador lo hizo mucho más raro, pero todavía pasa.
  También en PC.
- **Watch Dogs en Android:** a partir de la sala de Maurice la imagen puede quedar demasiado clara en cerca de la
  mitad de las sesiones. En Turnip un fallo de página de la GPU puede colgar el juego después de unos 25 minutos. En
  Mali la intro queda negra.
- **Ninja Gaiden 3 en Turnip:** algunas cabezas oscuras y el cielo quemado en ciertas fases.
- **Need for Speed: Most Wanted U sin su actualización (v0):** de noche el follaje y las luces salen blancos, quemados,
  y la imagen puede verse oscura, porque el paquete "Bloom Fix" solo corrige el juego con la actualización v32
  instalada. Por ahora, instala la actualización v32.
- **Xenoblade Chronicles X con el driver del sistema de Qualcomm:** los personajes se ven como siluetas blancas; usa
  Turnip.
- **Mali:** más lenta y con más problemas que Adreno; los drivers personalizados para Mali son experimentales.

La lista completa, con lo que está en curso, está en el [roadmap](docs/ROADMAP.es.md#en-curso).

## Controles

- **Controles en pantalla:** aparecen por defecto cuando no hay ningún mando conectado. Desde el menú del juego se
  pueden editar: mover, cambiar tamaño, ajustar a la cuadrícula, cambiar opacidad, estilo y forma de cada botón, guardar
  diseños como perfiles, hacer que un botón alterne o repita (turbo), crear macros (combinaciones o secuencias con
  tiempo) y botones de atajo que ejecutan acciones del emulador.
- **Mandos físicos** (los controles integrados de la portátil, mandos Bluetooth y USB) se asignan automáticamente a un
  mando emulado libre al conectarse. El jugador 1 es un Wii U GamePad por defecto. Las asignaciones se cambian en
  **Configuración › Controles**, y el botón HOME de Wii U queda en el botón Mode/Guide del mando.
- **Atajos (hotkeys):** pausar, captura de pantalla, cambiar pantalla TV/GamePad, mostrar u ocultar los controles en
  pantalla, contador de FPS, relación de aspecto, filtro de escalado y movimiento.
- **Menú en el juego:** pantalla del GamePad (en la segunda pantalla, con escala y tamaño propios), cambio TV/GamePad, escalado, generación de fotogramas, contador de FPS, filtro de escalado y
  nitidez, VSync, controles en pantalla, asignación de mandos, atajos, pausa y capturas (guardadas en `Pictures/Fractal`).

## Qué corrige Fractal

Fractal partió del port de Android de Cemu hecho por SSimco y desde entonces sumó más de 180 commits propios, además de
un merge de la rama `main` del Cemu oficial. Parte de las correcciones están en el núcleo del emulador que comparte la
versión de PC, así que también resuelven problemas que existen en el Cemu oficial:

| Juego | Problema | Corregido con |
|---|---|---|
| The Amazing Spider-Man | Nunca muestra imagen ("no display" en la wiki de compatibilidad de Cemu): el juego copia los cuadros al scan buffer con `GX2CopySurface` | Los cuadros copiados así se presentan. Jugable en la build de PC de Fractal |
| Watch Dogs | Se cierra al iniciar y durante el juego, imagen congelada desde el prólogo | Memoria de la consola reservada con la alineación que necesita el asignador del juego (el Cemu oficial tiene el mismo problema), resultados de streamout copiados de vuelta a la memoria de la consola, draws indexados con streamout en el orden del índice, sin descarte de caras en primitivas rect, carrera al iniciar corregida |
| Injustice: Gods Among Us | Cuelgue de la GPU, luchadores estirados | Constantes de bucle de los shaders emuladas, offsets de streamout corregidos |
| Need for Speed: Most Wanted U | Chasquidos y crujidos en el sonido del motor | `AXDecodeAdpcmData` implementado, historial del filtro biquad con precisión completa |
| Todos los juegos | Los shaders precompilados al iniciar se armaban con el estado equivocado y se compilaban de nuevo durante el juego | Estado del pipeline armado a partir de los registros de la caché (el Cemu oficial tiene el mismo bug) |

Y muchas correcciones para GPUs y drivers de Android: Tekken Tag Tournament 2, Assassin's Creed IV, Ninja Gaiden 3:
Razor's Edge, Xenoblade Chronicles X, Bayonetta 2, Pokkén Tournament, Breath of the Wild y más. La lista completa, con
los commits de cada punto, está en el [roadmap](docs/ROADMAP.es.md).

## Cómo reportar errores

Los reportes con el informe de depuración adjunto son la forma más rápida de que un juego se corrija.

1. Reproduce el problema (cierre, cuelgue o defecto gráfico) y vuelve a abrir Fractal.
2. En la biblioteca, abre **Opciones › Compartir informe de depuración**. Fractal crea `debug_report.zip` con
   `log.txt`, el log de la sesión anterior (útil tras un cierre), las dos últimas sesiones de juego (dispositivo, driver,
   ajustes, tiempos de cuadro y problemas encontrados), `settings.xml` y los perfiles de esos juegos.
3. Guarda o envía el zip, abre un [nuevo issue](https://github.com/dfdx047/Fractal/issues/new/choose) y adjúntalo.

Indica el dispositivo, el SoC, la versión de Android, el driver de GPU, el juego con su **Title ID** y versión, y los
pasos para reproducirlo. Los formularios de issue están en inglés, pero puedes escribir en español. `settings.xml`
contiene las rutas de tus carpetas de juegos; quita lo que no quieras compartir.

**No reportes errores de Fractal al equipo de Cemu.** Si el juego también falla igual en el Cemu oficial de PC,
menciónalo en el issue.

## Roadmap y changelog

- [Roadmap](docs/ROADMAP.es.md): lo que está hecho, lo que está en curso y lo planeado
- [Changelog](CHANGELOG.md) (en inglés)

## Cómo compilar

Consulta [BUILD.md](BUILD.md#android) (en inglés). El proyecto Android está en `src/android` (Gradle, NDK). Las contribuciones
son bienvenidas; lee [CONTRIBUTING.md](CONTRIBUTING.md) antes.

## Créditos

Fractal se apoya en el trabajo de:

- **[Cemu](https://github.com/cemu-project/Cemu)**, del equipo de Cemu: el núcleo del emulador (recompilador PowerPC,
  emulación de la GPU Latte, Cafe OS, audio, entrada) y la mayor parte del código de este repositorio.
- **[Port de Android de Cemu](https://github.com/SSimco/Cemu)**, de SSimco: la build de Android, la capa JNI, la app en
  Kotlin/Jetpack Compose y el trabajo de ARM64 del que nace Fractal.
- **[Cemu Plus](https://github.com/danielcamilo1/cemu-plus)**, de danielcamilo1: escalado AMD FSR 1 y Snapdragon GSR con
  nitidez RCAS, la generación de fotogramas con Lossless Scaling, la corrección de atómicos de 64 bits desalineados
  (Star Fox Zero), detalles de cierres en el log y dos correcciones de texturas y de log.
- **[lsfg-vk](https://github.com/PancakeTAS/lsfg-vk)** y **[Eden](https://git.eden-emu.dev/eden-emu/eden)**: la
  implementación en Vulkan de la generación de fotogramas de Lossless Scaling que portó Cemu Plus.
- **[SapphireRhodonite](https://github.com/SapphireRhodonite/Cemu)**: la base del GamePad en la segunda pantalla
  (rama `android-port-dual`).
- **Proyectos de drivers de GPU** listados en la página de drivers de la app, cada uno con nombre y enlace. Fractal no
  incluye ningún driver.
- Comunidad de los **[paquetes gráficos de Cemu](https://github.com/cemu-project/cemu_graphic_packs)**.
- **Fuentes:** Space Grotesk e Inter (SIL Open Font License 1.1).

Detalles en [NOTICE.md](NOTICE.md).

## Licencia

Fractal usa la [Mozilla Public License 2.0](LICENSE.txt), como Cemu. Los archivos en `dependencies/` y algunos
archivos en `src/` conservan las licencias indicadas en sus cabeceras. La generación de fotogramas
(`src/Cafe/HW/Latte/Renderer/Vulkan/FrameGen/`) es **GPL-3.0 o posterior**, así que el APK, que la incluye, también
sigue los términos de la GPL-3.0. Ver [NOTICE.md](NOTICE.md).

Fractal no está afiliado a Nintendo ni respaldado por ella. Wii U es una marca registrada de Nintendo. Fractal no es una
versión oficial de Cemu.
