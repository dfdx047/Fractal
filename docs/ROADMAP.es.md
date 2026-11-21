<p align="center">
  <a href="ROADMAP.md">English</a> · <a href="ROADMAP.pt-BR.md">Português (Brasil)</a> · <b>Español</b>
</p>

# Roadmap de Fractal

Estado: **v0.2-beta (en preparación)**, noviembre de 2026; el [changelog](../CHANGELOG.md) (en inglés) lista los cambios
de cada versión. Cada corrección en *Hecho* enlaza al commit que la hizo, y las cifras salen de las mediciones escritas
en esos commits.

## De dónde viene Fractal

- **Base:** el port de Android de Cemu hecho por SSimco (`android-port`, punto del fork `8759323e`, 2026-04-27).
- **Cemu oficial incorporado:** los 220 commits de `main` de `cemu-project/Cemu` hasta `3b103161` (2026-11-04), en `7c02ce75`.
- **Trabajo de Fractal:** unos 240 commits desde 2026-11-08: interfaz, drivers, entrada, renderizador, recompilador,
  código ARM/NEON, correcciones de juegos y herramientas de depuración.

Dispositivos de prueba: **Retroid Pocket 5** (Snapdragon 865, Adreno 650), **POCO X6 Pro** (Dimensity 8300 Ultra,
Mali-G615 MC6) y **Xiaomi Mi A3** (Snapdragon 665, Adreno 610). Los resultados anteriores también vienen de la **Anbernic
RG Cube** (Unisoc T820, Mali-G57 MC4), que sigue como referencia para dispositivos Mali más débiles.

---

## Hecho

### Correcciones de juegos en el núcleo compartido del emulador

Estos bugs están en código que Fractal comparte con la versión de PC de Cemu, así que no son exclusivos de Android.

| Juego | Qué fallaba | Corrección | Commit |
|---|---|---|---|
| Cualquier juego (encontrado en Super Mario 3D World) | Cuando el archivo de caché de shaders o pipelines crecía, la entrada de la propia tabla reubicada se escribía en la posición antigua, en medio de la tabla nueva. La entrada dañada decía ocupar ~4 GB: leerla gastaba 2 GB de RAM al arrancar (el Mi A3 cerraba el juego) y después todas las pipelines se leían como corruptas (1247 de 1252 recompiladas) | La entrada se escribe junto con la tabla; las entradas dañadas se descartan al abrir la caché, así que las cachés dañadas se reparan solas. Cemu upstream tiene el mismo bug | [`9b3dde81`](https://github.com/dfdx047/Fractal/commit/9b3dde81) |
| The Amazing Spider-Man | Nunca muestra imagen ("no display" en la wiki de compatibilidad de Cemu). El juego copia el cuadro terminado al scan buffer con `GX2CopySurface` en lugar de `GX2CopyColorBufferToScanBuffer` | Las copias a los rangos del scan buffer de la TV/GamePad se presentan. Jugable en la build de PC de Fractal (intro, título, capítulo 1) | [`79f2f691`](https://github.com/dfdx047/Fractal/commit/79f2f691) |
| Watch Dogs | Cierres aleatorios al iniciar: el asignador del juego necesita un área alineada a 128 KiB y recibía alineación de 16 KiB. El Cemu oficial tiene el mismo problema (64 KiB en Windows) | Memoria de la consola reservada en una dirección del host alineada a 4 GB | [`cb5321e6`](https://github.com/dfdx047/Fractal/commit/cb5321e6) |
| Watch Dogs | Cierre al iniciar: un hilo consulta un gestor y usa otro que el hilo principal crea microsegundos después | Parche del juego: el hilo espera al gestor correcto | [`d316661a`](https://github.com/dfdx047/Fractal/commit/d316661a) |
| Watch Dogs | Cierre durante el juego: el juego ejecuta tareas de visibilidad en la GPU con transform feedback y lee los resultados en la CPU; Cemu solo los guardaba en la GPU | Resultados de streamout copiados de vuelta a la memoria de la consola antes de que la CPU pueda verlos; orígenes del offset de streamout según el paquete | [`8b69778e`](https://github.com/dfdx047/Fractal/commit/8b69778e) |
| Watch Dogs | Cierre durante el juego: los draws indexados con streamout escribían las salidas en la posición del valor del índice, no en el orden del index buffer | Draws indexados con streamout dibujados a partir de los vértices reunidos en orden del índice | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Watch Dogs | Imagen congelada desde el prólogo (tablero, subtítulos apilados, tutorial "Shoot Maurice" detenido): se descartaban los rects del escalado final y del FXAA | Sin descarte de caras en primitivas rect | [`6d80adae`](https://github.com/dfdx047/Fractal/commit/6d80adae) |
| Watch Dogs | Congelamiento de 38-68 s al entrar al juego; shaders de streamout compilados de nuevo por cada pixel shader enlazado | Los vertex shaders con rasterización descartada no dependen del pixel shader; shaders grandes en caché por su código fuente. Ahora 9 s | [`9b6ee09b`](https://github.com/dfdx047/Fractal/commit/9b6ee09b), [`69ac8320`](https://github.com/dfdx047/Fractal/commit/69ac8320) |
| Watch Dogs | Congelamiento tras sesiones largas (alrededor de 1000 s en la RP5, 1250 s en PC): el juego comprueba que la GPU está detenida en una espera durante unos 10 µs y entonces llena el siguiente tramo de su lista de tareas de GPU. El procesador de comandos ya había pasado la espera y ejecutado el tramo vacío, así que una tarea de visibilidad nunca terminaba. El Cemu oficial tiene la misma carrera | El procesador de comandos vuelve a comprobar la espera justo antes de su primer efecto; barreras release/acquire en las display lists. 3000 s sin congelarse en PC y en la RP5 | [`210b613e`](https://github.com/dfdx047/Fractal/commit/210b613e) |
| Injustice: Gods Among Us | Cuelgue de la GPU: bucles de shader sin break se volvían infinitos | Constantes de bucle emuladas (`IT_SET_LOOP_CONST`, variables de bucle en los uniforms) | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Injustice: Gods Among Us | Luchadores estirados (skinning en la GPU vía streamout) | Offsets de streamout tomados del paquete, contados en DWORDs, sin el vértice base del draw | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Need for Speed: Most Wanted U | Chasquidos y crujidos en el sonido del motor al acelerar | `AXDecodeAdpcmData` implementado (faltaba), historial del biquad con precisión completa, contexto de bucle de ADPCM, desbordamiento del filtro paso bajo | [`44cdd26b`](https://github.com/dfdx047/Fractal/commit/44cdd26b) |
| Todos los juegos | Los pipelines precompilados al iniciar se armaban con el estado de la GPU de ese momento, así que la caché del driver nunca coincidía y se compilaban de nuevo durante el juego. El Cemu oficial tiene el mismo bug | Estado del pipeline armado a partir de los registros pasados al compilador | [`69ac8320`](https://github.com/dfdx047/Fractal/commit/69ac8320) |
| Todos los juegos | Caché de buffers: un uniform buffer con registro de tamaño `0xFFFFFFFF` se volvía un nodo de tamaño cero que corrompía el árbol de rangos | Tamaño comprobado | [`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5) |
| Super Smash Bros. for Wii U | Se congelaba al cargar un escenario en la RP5: un hilo de prioridad baja tenía el mutex que esperaba el cargador mientras un hilo de prioridad mayor ocupaba el núcleo, así que el dueño no volvía a ejecutarse. Cemu no tenía herencia de prioridad en `OSMutex` | `OSLockMutex` presta la prioridad del que espera al dueño (y a la cadena de dueños), `OSUnlockMutex` y `OSWaitCond` la devuelven, como en Cafe OS | [`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685) |
| Splinter Cell: Blacklist | Se cerraba a los 27 s de juego: el destino de una copia a una superficie 1D (512x1 R32F) se buscaba con una vista 2D, que la caché de texturas rechaza | Destinos de copia 1D y 1D-array creados como 2D y 2D-array, como los de cubemap | [`dd40e27e`](https://github.com/dfdx047/Fractal/commit/dd40e27e) |
| Splinter Cell: Blacklist | Limitado a 20 fps en todos los dispositivos y, en la RP5, parado en la pantalla de carga de la misión: el resultado de las occlusion queries solo llegaba a la memoria del juego en el vsync virtual (o cada 8 queries), y el juego espera 3 queries por cuadro | El bucle ocioso del procesador de comandos termina las queries pendientes en cuanto el renderizador las tiene. 30 fps (el límite del juego) en el POCO y la RP5, y la RP5 entra al juego | [`1557a268`](https://github.com/dfdx047/Fractal/commit/1557a268) |
| Splinter Cell: Blacklist | `OSGetMapVirtAddrRange` y `OSGetDataPhysAddrRange` no existían, así que el juego leía lo que hubiera en sus variables de salida | Devuelven el rango de `OSAllocVirtAddr` y la MEM2 | [`ad5d1dc1`](https://github.com/dfdx047/Fractal/commit/ad5d1dc1) |
| Batman: Arkham City | Se congelaba para siempre tras New Game ("Saving...") en todos los dispositivos: las occlusion queries de GX2 que terminaban sin ninguna query del host en marcha nunca terminaban | Sin nada en curso y sin query del host activa, todo evento de query emitido hasta ahí cuenta como terminado; se tratan las queries terminadas dos veces o reiniciadas pronto | [`e5dc06a1`](https://github.com/dfdx047/Fractal/commit/e5dc06a1) |
| The Legend of Zelda: Breath of the Wild | Se cerraba al cargar una caché de shaders de la comunidad (issue #3 en GitHub): los fetch shaders con una cláusula de 5-8 o 13-16 fetches iban al parser antiguo. Cemu upstream tiene la misma comprobación | Máscara corregida; los 5981 vertex shaders de esa caché siguen el camino de CF | [`42a2fbd7`](https://github.com/dfdx047/Fractal/commit/42a2fbd7) |
| LEGO Batman 2: DC Super Heroes | Parpadeo negro cada dos cuadros en todas las GPUs: el juego hace dos swaps por cuadro y copia a la TV una sola vez, y el segundo swap presentaba una imagen negra | Una vez presentada una imagen del juego, un swap sin copia no presenta nada, como el scan buffer de la consola | [`de5d2869`](https://github.com/dfdx047/Fractal/commit/de5d2869) |
| Teslagrad | Se detenía (SIGTRAP) al guardar: `SetEndOfFile` en Unix/Android era un esbozo vacío, también en upstream | El archivo se corta en la posición actual | [`8daaafec`](https://github.com/dfdx047/Fractal/commit/8daaafec) |
| Disney Infinity 3.0 | Se detenía en `getsockname` con un socket que el emulador no conoce | Devuelve -1 con `EINVAL`, o el error del host | [`6b27c9ae`](https://github.com/dfdx047/Fractal/commit/6b27c9ae) |
| Disney Infinity 3.0, Cars 3 | Se detenía cuando el heap de 164 MB de la caché de buffers se llenaba de rangos de streamout | Como último recurso se descartan los rangos de streamout sin uso durante 2 cuadros (POCO 3 de 3 y RP5 sin cierres); la asignación solo se repite tras un fallo | [`146fdb6e`](https://github.com/dfdx047/Fractal/commit/146fdb6e), [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396) |
| ZombiU (EE. UU.) | Pantalla negra al iniciar con la actualización v32, también en Cemu para PC. La wiki de Cemu dice que se quite la actualización, lo que un .wua no permite | El juego inicia sin esa actualización (queda en el log); intro y juego en el POCO | [`b6cf7c8b`](https://github.com/dfdx047/Fractal/commit/b6cf7c8b) |

También en código compartido: el recompilador ahora maneja `psq_lx`/`psq_stx` y sus formas con update, que el Cemu
oficial envía al intérprete ([`9107127b`](https://github.com/dfdx047/Fractal/commit/9107127b); en The Amazing Spider-Man 2
son los conversores de muestras de audio que tenían un núcleo cerca del 100 % durante la carga).

**Assassin's Creed IV: Black Flag:** en nuestras pruebas la versión oficial 2.6 de Cemu (AppImage) se quedó en
"loading..." sin uso de CPU, mientras que la build de PC de Fractal y la Retroid Pocket 5 ejecutan la cinemática inicial
y la cubierta del barco. Todavía no identificamos qué cambio marca la diferencia.

### Correcciones de juegos para Android, ARM64 y drivers de GPU móviles

| Juego | Qué fallaba | Corrección | Commit |
|---|---|---|---|
| Ninja Gaiden 3: Razor's Edge | Pantalla negra tras unos minutos en Android: una carrera en el código de event flags del propio juego dejaba dos hilos esperando para siempre | El "set" de event flag del juego ya no reinicia un evento que el otro hilo acaba de señalar | [`8eaae97a`](https://github.com/dfdx047/Fractal/commit/8eaae97a) |
| Ninja Gaiden 3: Razor's Edge | Cierre: funciones de más de 1 MB de código ARM64 dejaban saltos fuera de alcance | Código frío emitido entre segmentos; un error de codificación deja la función interpretada en lugar de abortar | [`a26051bf`](https://github.com/dfdx047/Fractal/commit/a26051bf) |
| Ninja Gaiden 3: Razor's Edge | Turnip: toda la imagen borrosa por la profundidad de campo leyendo un depth buffer desactualizado | Barrera al final de cada render pass en Turnip | [`64c4b75c`](https://github.com/dfdx047/Fractal/commit/64c4b75c) |
| Ninja Gaiden 3: Razor's Edge | Cierre en su propio asignador con zlib-ng como zlib del host | Estado de zlib asignado en el heap por defecto | [`8eaae97a`](https://github.com/dfdx047/Fractal/commit/8eaae97a) |
| Assassin's Creed IV: Black Flag | Mundo negro (solo el HUD) en la selva, cerca del puente: el `fsel` del recompilador copiaba solo la primera mitad del registro y el juego leía basura en la segunda en una rutina de tiempo de cuadro | `fsel` copia el registro entero, como el intérprete. Los apagones son mucho más raros pero no desaparecieron (ver En curso) | [`b8d27fdf`](https://github.com/dfdx047/Fractal/commit/b8d27fdf) |
| Tekken Tag Tournament 2 | El driver del sistema de Qualcomm se cerraba al compilar un shader de gather 4x4 en la primera pelea | Escrituras indexadas en GPR generadas como switch en lugar de un array de GPRs con índice dinámico | [`1293cd7d`](https://github.com/dfdx047/Fractal/commit/1293cd7d) |
| Assassin's Creed IV: Black Flag | Driver del sistema de Qualcomm: bloques verdes/magenta/azules sobre partículas, pantallas de carga y cinemáticas. El bloom muestrea la textura en la que dibuja, algo indefinido sin `VK_EXT_attachment_feedback_loop_layout` | Esos draws muestrean una copia de la textura | [`64c4b75c`](https://github.com/dfdx047/Fractal/commit/64c4b75c) |
| Watch Dogs | Estadio y bloom blancos en Adreno: Adreno devuelve NaN en min/max/clamp cuando un operando es NaN | min/max/clamp seguros frente a NaN activados por defecto en GPUs Qualcomm | [`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223) |
| Xenoblade Chronicles X | Turnip: cuelgue de la GPU unos 36 s después de empezar una partida nueva, siempre | Las imágenes `R8G8B8A8_UNORM` siguen siendo mutables (sin UBWC) en Turnip | [`2a72a16b`](https://github.com/dfdx047/Fractal/commit/2a72a16b) |
| Xenoblade Chronicles X | Adreno 650: el envío de command buffers fallaba tras ~160 s (copias entre imágenes comprimidas de formatos distintos) | Esas copias pasan por un buffer intermedio | [`017be0ef`](https://github.com/dfdx047/Fractal/commit/017be0ef) |
| Bayonetta 2 | Cierre 3 s después de iniciar (RG Cube), cuelgue en la pantalla de inicio (RP5): el backend ARM64 nunca cargaba el índice del núcleo (`mfspr UPIR`) | SPRs por encima de 998 cargados por nombre | [`613dc67b`](https://github.com/dfdx047/Fractal/commit/613dc67b) |
| Pokkén Tournament | Se cerraba cada vez que salía de un menú: almacenamiento local de hilo leído en un hilo del host y usado en otro (TLS dinámico en Android) | TLS del planificador y de las fibras leído en el hilo que ejecuta el código. Llega a los combates a 60 fps en la RP5 | [`8ea62197`](https://github.com/dfdx047/Fractal/commit/8ea62197) |
| Need for Speed: Most Wanted U | Cierre al entrar en la ciudad: descriptor sets agotados (más de 160 mil combinaciones de textura/sampler) | Los pools de descriptores crecen según se necesite, los sets sin uso durante 600 cuadros se liberan | [`1feba3d6`](https://github.com/dfdx047/Fractal/commit/1feba3d6) |
| Need for Speed: Most Wanted U | Follaje y luces blancos, quemados | El paquete gráfico "Bloom Fix" de la comunidad viene activado por defecto (causa raíz aún abierta) | [`0386ec86`](https://github.com/dfdx047/Fractal/commit/0386ec86) |
| The Legend of Zelda: Breath of the Wild | Turnip: cuelgue de la GPU al iniciar con renderizado por bloques | Turnip renderiza en la memoria del sistema por defecto (opción por juego) | [`525a6907`](https://github.com/dfdx047/Fractal/commit/525a6907), [`7f8d5a0f`](https://github.com/dfdx047/Fractal/commit/7f8d5a0f) |
| The Legend of Zelda: Breath of the Wild | El hilo de renderizado esperaba 15,6 ms por cuadro en una sincronización completa en cada `GX2DrawDone` | Opción por juego, desactivada en los perfiles por defecto de BotW: RP5 9,4 → 11,1 fps | [`951eb1e7`](https://github.com/dfdx047/Fractal/commit/951eb1e7) |
| Star Fox Zero | SIGBUS en atómicos de 64 bits desalineados en ARM64 | Lock para atómicos desalineados (de Cemu Plus) | [`c35755ec`](https://github.com/dfdx047/Fractal/commit/c35755ec) |
| Mario Kart 8 | Cierre (estado del mezclador de sonido) | Reinicio de `snd_user` traído del Cemu oficial antes del merge completo | [`c62e6977`](https://github.com/dfdx047/Fractal/commit/c62e6977) |
| Juegos con diálogo de error | Se quedaban atascados en las portátiles: los botones del diálogo de error no recibían foco sin ratón | A y B cierran el diálogo | [`5879e1f5`](https://github.com/dfdx047/Fractal/commit/5879e1f5) |
| Todos los juegos | La configuración y la lista de juegos quedaban en ceros tras reiniciar poco después de guardar | Los archivos se sincronizan con el almacenamiento antes de reemplazar los antiguos | [`9db92543`](https://github.com/dfdx047/Fractal/commit/9db92543) |
| Todos los juegos en PanVK | El PanVK de faux123 (Mali-G615) cerraba todos los juegos en su primer cuadro: anuncia `VK_KHR_present_wait`, que el WSI de Mesa en Android no implementa | `present_wait` desactivado en PanVK; Twilight Princess HD funciona en su límite de 30 fps | [`45be70a4`](https://github.com/dfdx047/Fractal/commit/45be70a4) |
| Pikmin 3 | El driver de Qualcomm se cerraba al crear pipelines de shaders que leen y escriben GPRs por índice | En Qualcomm esos shaders usan variables de GPR separadas: lectura con una cadena de selección, escritura con un switch | [`ab851c1a`](https://github.com/dfdx047/Fractal/commit/ab851c1a) |
| Yoshi's Woolly World | Driver de Mali: sin Yoshi ni los enemigos. El streamout se escribía con stores en storage buffer desde el vertex shader, que el driver no admite | Streamout con `VK_EXT_transform_feedback` en drivers sin stores en el pipeline de vértices | [`95904874`](https://github.com/dfdx047/Fractal/commit/95904874) |
| Yoshi's Woolly World (cualquier juego con texturas BC4/BC5 SNORM en Mali) | Manchas negras sobre la lana: los normal maps BC5 SNORM se decodificaban en la CPU como bytes sin signo, BC4 SNORM como UNORM | Se guardan como bytes en complemento a dos, extremos SNORM bien asignados | [`95904874`](https://github.com/dfdx047/Fractal/commit/95904874) |
| Yoshi's Woolly World | Turnip T30 colgaba la GPU de la RP5 en el Mundo 1-1 | Perfil por defecto: driver del sistema | [`5c8f856d`](https://github.com/dfdx047/Fractal/commit/5c8f856d) |
| Super Smash Bros. for Wii U | Turnip (T19 y T30) reiniciaba la RP5 al final de 4 de 4 combates | Perfil por defecto: driver del sistema (RP5 42,6 fps, casi lo que daba Turnip; POCO 55 fps contra 37 en PanVK) | [`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685), [`614d2531`](https://github.com/dfdx047/Fractal/commit/614d2531) |
| Kirby and the Rainbow Curse | Turnip en modo de memoria del sistema reiniciaba la RP5 al entrar en Green Valley 1-1 en 4 de 5 ejecuciones | Perfil por defecto: driver del sistema (60,0 fps en la RP5), Turnip sin sysmem | [`803fed48`](https://github.com/dfdx047/Fractal/commit/803fed48) |
| Sonic Lost World | SIGBUS justo después de la pantalla de título: `stwcx.` en una dirección impar se volvía `CASAL`, que exige alineación de 4 bytes en ARM64 | Las direcciones no alineadas usan una comparación y escritura normales, en el recompilador y el intérprete | [`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796) |
| Sonic Lost World, Rayman Legends | Multi-core: Sonic se cierra o se cuelga al saltar el video inicial, Rayman se cuelga en la pantalla de carga (POCO: 4 de 4 y 3 de 4 ejecuciones) | Perfiles por defecto: recompilador single-core. Rayman depende de un solo hilo y no pierde nada (RP5 17,3 contra 17,0 fps) | [`fdbc093c`](https://github.com/dfdx047/Fractal/commit/fdbc093c) |
| Rayman Legends | Los drivers Mesa dejan la GPU al 99 % en el primer nivel: Turnip 17 fps contra 59 en el driver de la Adreno 650, PanVK 18 contra 29,6 en el driver de la Mali-G615 | Perfil por defecto: driver del sistema | [`1f6476e3`](https://github.com/dfdx047/Fractal/commit/1f6476e3) |
| Sonic Lost World | PanVK reinició el POCO dos veces de dos | Perfil por defecto: driver del sistema (48 fps en el driver de la Mali-G615; 53 en la RP5, igual que Turnip) | [`122d88bb`](https://github.com/dfdx047/Fractal/commit/122d88bb) |
| Paper Mario: Color Splash | Puntitos de colores sobre la imagen en Turnip y PanVK: un pase muestrea el attachment de color en el que dibuja, lo que es indefinido incluso con `VK_EXT_attachment_feedback_loop_layout` | El draw muestrea una copia hecha antes, en todos los drivers. Misma velocidad en el POCO (30,1 fps, límite 30) | [`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796) |
| Monster Hunter 3 Ultimate | Se cerraba (o se congelaba) a los 245 s de una partida nueva, al terminar el video inicial: el cargador del juego dice que un archivo está listo mientras la petición de reabrirlo sigue pendiente. En single-core se cerraba siempre en la RP5 y el POCO | Corrección del juego: "listo" también exige que no haya comando pendiente. 6/6 ejecuciones llegan a la aldea | [`dd4aa043`](https://github.com/dfdx047/Fractal/commit/dd4aa043) |
| New Super Mario Bros. U | Se cerraba a los 57 s del arranque en una CPU lenta (Mi A3, 6/6 ejecuciones): la cola de dibujo de las cabezas Mii se recorría antes de que otro hilo pusiera el modelo | Corrección del juego que salta solo la entrada sin modelo (el paquete "Title Screen Crash Fix" de la comunidad salta la cola entera). Mi A3: 2/2 ejecuciones llegan al video inicial | [`ff719783`](https://github.com/dfdx047/Fractal/commit/ff719783) |
| Resident Evil Revelations | Se cerraba en Mali (driver del sistema y PanVK) justo después de la intro: el render target A2_B10_G10_R10 no tenía formato Vulkan | Asignado a `A2B10G10R10_UNORM_PACK32` | [`71f3a05a`](https://github.com/dfdx047/Fractal/commit/71f3a05a) |
| The Legend of Zelda: Twilight Princess HD | Minimapa negro con el driver de Qualcomm: el geometry shader de la emulación de rects no declaraba el mismo `gl_PerVertex` que los vertex shaders | El mismo bloque declarado en las dos etapas | [`a95f1b41`](https://github.com/dfdx047/Fractal/commit/a95f1b41) |
| Cars 3, Ninja Gaiden 3, Disney Infinity 3.0 | Draws omitidos con el driver de Qualcomm (Cars 3: 25 pipelines, Ninja Gaiden 3: 24, Disney Infinity 3.0: 2): el geometry shader de rects dejaba entradas del pixel shader sin escribir | El geometry shader escribe ceros en ellas. Cars 3: 0 pipelines fallidos | [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396) |
| FIFA 13 | Turnip T19 en modo de memoria del sistema cerraba el juego 8 s después de iniciar (5 de 5 ejecuciones en la RP5) | Perfil por defecto: Turnip sin sysmem (T19 56,5 fps) | [`8d168fa9`](https://github.com/dfdx047/Fractal/commit/8d168fa9) |
| Call of Duty: Ghosts | Se cerraba cuando el juego se reinicia a sí mismo en su primer arranque (POCO): Android no tenía implementación de sistema para reiniciar y salir del título | El renderizador se vuelve a crear sobre las mismas superficies; la salida queda en el log | [`e2030e99`](https://github.com/dfdx047/Fractal/commit/e2030e99) |
| Hot Wheels: World's Best Driver | Se congelaba en una carga (RP5 1 ejecución de 3): dos carreras en el handshake del hilo de renderizado del propio juego, facilitadas por hilos del host interrumpidos | Corrección del juego para las dos. RP5 8 de 8 y POCO 5 de 5 ejecuciones sin congelarse | [`dfde2b64`](https://github.com/dfdx047/Fractal/commit/dfde2b64) |
| Fast & Furious: Showdown | Se colgaba en la carga después de la escena inicial (RP5 T19 2 ejecuciones de 20, POCO Mali 1 de 20): el motor de Hot Wheels, con las mismas dos carreras | La corrección de Hot Wheels adaptada al código de este juego. 20 de 20 ejecuciones sin colgarse en el RP5 y el POCO | [`a71c3399`](https://github.com/dfdx047/Fractal/commit/a71c3399), [`8355f2d5`](https://github.com/dfdx047/Fractal/commit/8355f2d5) |
| Breath of the Wild, Need for Speed: Most Wanted U, Resident Evil Revelations (drivers del sistema de Mali y Qualcomm) | Lentos en los drivers sin feedback loop: los shaders que muestrean su propio adjunto ~330 veces por cuadro (BotW) lo copiaban antes de cada draw | Esos shaders dividen el render pass. POCO X6 Pro: NFS 17,2 → 28,8 fps, RER 24,9 → 30,0 fps, BotW 5,9 → 15,6 fps (apertura) | [`b96b5dca`](https://github.com/dfdx047/Fractal/commit/b96b5dca) |
| Generación de cuadros | Reducía a la mitad los fps del propio juego en teléfonos que dejan las apps a 60 Hz (issue #9) | Pide a Android la tasa de refresco más alta de la pantalla mientras está activada | [`0f0cc53b`](https://github.com/dfdx047/Fractal/commit/0f0cc53b) |
| Teslagrad | 770 funciones en el intérprete y el hilo del recompilador al 99 % en la Adreno 610 | Más instrucciones recompiladas y funciones enormes traducidas en ventanas (ver la sección del recompilador): 1 % low 3,7 → 10,3 fps en el Mi A3, luego 0 funciones en el intérprete | [`66f59e89`](https://github.com/dfdx047/Fractal/commit/66f59e89), [`85b29bd9`](https://github.com/dfdx047/Fractal/commit/85b29bd9) |
| Todos los juegos | Los ajustes cambiados en el menú del juego se sobrescribían con la copia antigua de la biblioteca | Cada guardado de la biblioteca relee antes el archivo si cambió en el disco | [`e91a7121`](https://github.com/dfdx047/Fractal/commit/e91a7121) |
| Todos los juegos | La app se cerraba al iniciar en ROMs personalizadas que niegan `Android/data` (issue #1 en GitHub) | Los errores de carpeta van al log en vez de cerrar la app; la carpeta por defecto pasa a la carpeta privada de la app | [`42a2fbd7`](https://github.com/dfdx047/Fractal/commit/42a2fbd7), [`5f607b21`](https://github.com/dfdx047/Fractal/commit/5f607b21) |

### Renderizador (Vulkan) para GPUs móviles por bloques

Adreno y Mali guardan y recargan todos los attachments cuando termina un render pass, así que la mayor parte de este
trabajo evita cortar passes.

- La subida de buffers y las occlusion queries ya no terminan el render pass; la caché de buffers vive en memoria
  visible para el host en GPUs de memoria unificada ([`2c1895ac`](https://github.com/dfdx047/Fractal/commit/2c1895ac)).
- El trabajo de vértices sigue solapado con las barreras de render target; dependencias del propio attachment
  rastreadas por attachment ([`7b36890f`](https://github.com/dfdx047/Fractal/commit/7b36890f),
  [`d9606be8`](https://github.com/dfdx047/Fractal/commit/d9606be8)).
- Los nodos ocupados de la caché de buffers se mueven a memoria nueva en lugar de terminar el pass
  ([`ec46fba8`](https://github.com/dfdx047/Fractal/commit/ec46fba8), [`a74fbb56`](https://github.com/dfdx047/Fractal/commit/a74fbb56)).
- Subida de texturas en un command buffer de prólogo en lugar de terminar el pass
  ([`2eeceaea`](https://github.com/dfdx047/Fractal/commit/2eeceaea)).
- Compresión de framebuffer (UBWC en Adreno, AFBC en Mali) activa en las imágenes de color
  ([`017be0ef`](https://github.com/dfdx047/Fractal/commit/017be0ef)).
- Turnip: `vkGetFenceStatus` bloquea en KGSL, así que los command buffers terminados se detectan con un marcador de
  finalización ([`65047547`](https://github.com/dfdx047/Fractal/commit/65047547)).
- Limpieza de depth con un render pass en lugar de `vkCmdClearDepthStencilImage`, que cuesta ~3,5 ms por limpieza en
  1080p en el driver de la Adreno 610 ([`aabeea76`](https://github.com/dfdx047/Fractal/commit/aabeea76)).
- Una caché de pipelines por driver, conservada al cambiar de driver ([`2e43b0b7`](https://github.com/dfdx047/Fractal/commit/2e43b0b7)).
- Pipelines compilados en 1-2 hilos en Android en lugar de hasta 7, lo que redujo los tirones con caché fría
  ([`9893cf72`](https://github.com/dfdx047/Fractal/commit/9893cf72)).
- La swapchain elige un composite alpha compatible (Mesa PanVK) ([`c1f4cd8e`](https://github.com/dfdx047/Fractal/commit/c1f4cd8e)).
- Un draw que muestrea su propio attachment de color lee una copia en todos los drivers, también donde existe
  `VK_EXT_attachment_feedback_loop_layout` ([`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796)).
- Streamout con `VK_EXT_transform_feedback` en drivers sin stores en el pipeline de vértices (driver de Mali)
  ([`95904874`](https://github.com/dfdx047/Fractal/commit/95904874)).
- El resultado de las occlusion queries llega al juego con la GPU ociosa; las queries que terminan sin query del host en
  marcha se completan ([`1557a268`](https://github.com/dfdx047/Fractal/commit/1557a268), [`e5dc06a1`](https://github.com/dfdx047/Fractal/commit/e5dc06a1)).
- Un swap sin nada copiado a la pantalla mantiene la última imagen ([`de5d2869`](https://github.com/dfdx047/Fractal/commit/de5d2869)).
- Caché de buffers: los rangos de streamout se descartan como último recurso cuando el heap se llena
  ([`146fdb6e`](https://github.com/dfdx047/Fractal/commit/146fdb6e), [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396)).
- Sin `present_wait` en PanVK ([`45be70a4`](https://github.com/dfdx047/Fractal/commit/45be70a4)); render targets `A2_B10_G10_R10` asignados ([`71f3a05a`](https://github.com/dfdx047/Fractal/commit/71f3a05a)).

### Recompilador PowerPC → ARM64

- Contador de ciclos en un registro, funciones agrupadas en un área de código, saltos e inmediatos más cortos, saltos
  por flags, acceso a memoria con offset cero sin add ([`23b10251`](https://github.com/dfdx047/Fractal/commit/23b10251)).
- `mfspr UPIR` ([`d61aee38`](https://github.com/dfdx047/Fractal/commit/d61aee38), [`613dc67b`](https://github.com/dfdx047/Fractal/commit/613dc67b)),
  `ps_nabs`, `dcbt` ([`a26051bf`](https://github.com/dfdx047/Fractal/commit/a26051bf)), `psq_lx`/`psq_stx`/`psq_lux`/`psq_stux`,
  `eieio`, `dcbi` ([`9107127b`](https://github.com/dfdx047/Fractal/commit/9107127b)) ya no recurren al intérprete.
- Las funciones enormes se mantienen dentro del alcance de salto de ARM64 ([`a26051bf`](https://github.com/dfdx047/Fractal/commit/a26051bf)).
- Flush-to-zero realmente activado en Android ([`b385304f`](https://github.com/dfdx047/Fractal/commit/b385304f)).
- `stwcx.` en una dirección no alineada ([`70eb6796`](https://github.com/dfdx047/Fractal/commit/70eb6796)).
- Multiplicación y suma fusionadas (`fmadd`/`fmsub`/`fnmsub` y sus formas single) como en Espresso y el intérprete;
  antes, `a*c - round(a*c)` daba cero solo en el código recompilado ([`fec3837f`](https://github.com/dfdx047/Fractal/commit/fec3837f)).
- `addo`/`subfo`/`addco`/`subfco`, `mfxer`/`mtxer`, `fcmpo`, saltos al estilo `bdnzf` y saltos condicionales lejanos;
  una función contigua enorme se traduce en ventanas (en Teslagrad un switch de 955 KB se traducía 22 veces)
  ([`66f59e89`](https://github.com/dfdx047/Fractal/commit/66f59e89)).
- `subfme`, `divwo`/`divwuo` y `bcl 20`: Disney Infinity 2.0 145 → 0 y Teslagrad 157 → 0 funciones en el intérprete
  ([`85b29bd9`](https://github.com/dfdx047/Fractal/commit/85b29bd9)).

### Extensiones ARMv8 y NEON

- Extensiones opcionales de ARMv8 (AES, PMULL, CRC32, atómicos LSE, FP16, dot product) detectadas en tiempo de
  ejecución; la build sigue siendo ARMv8.0-A para cualquier teléfono arm64-v8a ([`b1078675`](https://github.com/dfdx047/Fractal/commit/b1078675)).
- AES-128 con la Crypto Extension al leer discos cifrados ([`852801bb`](https://github.com/dfdx047/Fractal/commit/852801bb)), instrucciones CRC32 al cargar RPX/RPL
  ([`64efc3e5`](https://github.com/dfdx047/Fractal/commit/64efc3e5)), máximo del index buffer con NEON ([`bd6af3dc`](https://github.com/dfdx047/Fractal/commit/bd6af3dc)) y decodificación BC1-BC5 con una paleta por
  bloque y consultas de tabla NEON para GPUs sin BCn ([`c4e6b0aa`](https://github.com/dfdx047/Fractal/commit/c4e6b0aa)). Cifras en *Mejoras medidas*.
- `tools/neon_tests` compara cada camino NEON con el código escalar bit a bit ([`852801bb`](https://github.com/dfdx047/Fractal/commit/852801bb)).

### Solo Android

- Se quitó el código de escritorio: interfaz wxWidgets, renderizadores OpenGL y Metal, recompilador x86-64, código de
  Windows, macOS y Linux de escritorio, entrada y audio de escritorio, presencia en Discord (572 archivos, unas 103 mil
  líneas). La biblioteca nativa bajó de 22,7 a 18,4 MB sin exportar símbolos de terceros, el APK solo lleva las
  bibliotecas arm64, los recursos sin uso se quitan al compilar y los graphic packs se descargan con `HttpURLConnection`
  en lugar de Ktor ([`9aa94b36`](https://github.com/dfdx047/Fractal/commit/9aa94b36), [`ee6e63b1`](https://github.com/dfdx047/Fractal/commit/ee6e63b1), [`d07f65ea`](https://github.com/dfdx047/Fractal/commit/d07f65ea), [`54b44032`](https://github.com/dfdx047/Fractal/commit/54b44032), [`4d4403c3`](https://github.com/dfdx047/Fractal/commit/4d4403c3), [`4b0f2868`](https://github.com/dfdx047/Fractal/commit/4b0f2868), [`bc69967a`](https://github.com/dfdx047/Fractal/commit/bc69967a), [`10bd5abd`](https://github.com/dfdx047/Fractal/commit/10bd5abd), [`5e0236a6`](https://github.com/dfdx047/Fractal/commit/5e0236a6)).

### Decompilador de shaders

- Constantes de bucle ([`2eef8ff5`](https://github.com/dfdx047/Fractal/commit/2eef8ff5)).
- Escrituras indexadas en GPR como switch ([`1293cd7d`](https://github.com/dfdx047/Fractal/commit/1293cd7d)).
- Reglas de NaN del R600 para min/max/clamp en Adreno ([`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223)).
- Vertex shaders de streamout independientes del pixel shader enlazado, caché de SPIR-V por código fuente para shaders
  grandes ([`9b6ee09b`](https://github.com/dfdx047/Fractal/commit/9b6ee09b)).
- Lecturas y escrituras de GPR por índice sin el array de GPRs en Qualcomm ([`ab851c1a`](https://github.com/dfdx047/Fractal/commit/ab851c1a)).
- Salidas de transform feedback para el streamout ([`95904874`](https://github.com/dfdx047/Fractal/commit/95904874)).
- Geometry shader de la emulación de rects: el mismo `gl_PerVertex` que los vertex shaders, ceros en las entradas del
  pixel shader ([`a95f1b41`](https://github.com/dfdx047/Fractal/commit/a95f1b41), [`eca58396`](https://github.com/dfdx047/Fractal/commit/eca58396)).
- Se detectan los fetch shaders con una cláusula de 5-8 o 13-16 fetches ([`42a2fbd7`](https://github.com/dfdx047/Fractal/commit/42a2fbd7)).

### Sistema y audio

- Correcciones de audio de AX ([`44cdd26b`](https://github.com/dfdx047/Fractal/commit/44cdd26b)).
- El núcleo principal y el hilo de la GPU duermen en lugar de girar cuando están ociosos; el núcleo principal duerme con
  el juego en pausa ([`ff38f5b4`](https://github.com/dfdx047/Fractal/commit/ff38f5b4), [`bf0944a4`](https://github.com/dfdx047/Fractal/commit/bf0944a4)).
- Lectura de los argumentos de `GX2Init` ([`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223)).
- Herencia de prioridad en `OSMutex` ([`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685)); `OSGetMapVirtAddrRange` y `OSGetDataPhysAddrRange`
  ([`ad5d1dc1`](https://github.com/dfdx047/Fractal/commit/ad5d1dc1)); recorte de archivos en Android ([`8daaafec`](https://github.com/dfdx047/Fractal/commit/8daaafec)); errores de `getsockname` ([`6b27c9ae`](https://github.com/dfdx047/Fractal/commit/6b27c9ae)).
- Reiniciar y salir del título en Android ([`e2030e99`](https://github.com/dfdx047/Fractal/commit/e2030e99)); un juego inicia sin una actualización que se sabe que impide
  la carga ([`b6cf7c8b`](https://github.com/dfdx047/Fractal/commit/b6cf7c8b)).
- Correcciones de juegos en una tabla de patrones exactos del código del juego (Ninja Gaiden 3, Monster Hunter 3
  Ultimate, New Super Mario Bros. U, Hot Wheels) ([`dd4aa043`](https://github.com/dfdx047/Fractal/commit/dd4aa043), [`ff719783`](https://github.com/dfdx047/Fractal/commit/ff719783), [`dfde2b64`](https://github.com/dfdx047/Fractal/commit/dfde2b64)).

### Interfaz

- Cinco temas, design tokens, fuentes incluidas, superficies de cristal, fondos estáticos y animados
  ([`73fd0c96`](https://github.com/dfdx047/Fractal/commit/73fd0c96)); animación de inicio por tema
  ([`4489bc72`](https://github.com/dfdx047/Fractal/commit/4489bc72)); icono de Sierpinski
  ([`a59f82f8`](https://github.com/dfdx047/Fractal/commit/a59f82f8)).
- Biblioteca en cuadrícula, carrusel y lista, configuración inicial, pantalla de apariencia
  ([`80712467`](https://github.com/dfdx047/Fractal/commit/80712467)); tiempo de juego, última partida, orden,
  exportar/importar partidas y caché de shaders, pantalla por juego ([`ecf40b3b`](https://github.com/dfdx047/Fractal/commit/ecf40b3b)).
- Opciones de Resolución y FPS detectadas en los paquetes gráficos de cualquier juego
  ([`d95f0191`](https://github.com/dfdx047/Fractal/commit/d95f0191), [`9a359b76`](https://github.com/dfdx047/Fractal/commit/9a359b76)).
- Configuración agrupada en secciones, pantallas antiguas con el estilo nuevo
  ([`b7428b86`](https://github.com/dfdx047/Fractal/commit/b7428b86), [`383af9d2`](https://github.com/dfdx047/Fractal/commit/383af9d2)).
- Navegación con mando y leyenda de botones compacta en todas las pantallas (`4008a538` a `572b8a9e`), foco restaurado
  al volver (`eb7e614e` a `7dc5303c`), diseño para teléfono en vertical ([`94e60896`](https://github.com/dfdx047/Fractal/commit/94e60896)).
- Menú lateral en el juego con el tema de la app ([`d6bba2e5`](https://github.com/dfdx047/Fractal/commit/d6bba2e5)).
- English, Português (Brasil) y Español en todos los textos de la app
  ([`c9d8a940`](https://github.com/dfdx047/Fractal/commit/c9d8a940), [`a74e2a35`](https://github.com/dfdx047/Fractal/commit/a74e2a35)).

- **GamePad en la segunda pantalla:** las portátiles de dos pantallas (AYN Thor) y los monitores externos reciben la
  pantalla del GamePad con toque; escala y tamaño propios en el menú del juego; elecciones guardadas entre juegos (basado
  en la rama `android-port-dual` de SapphireRhodonite) ([`f29f2800`](https://github.com/dfdx047/Fractal/commit/f29f2800)).
- El botón atrás de las portátiles cuyo mando lo envía (AYN Odin 3) abre el menú del juego ([`026b580a`](https://github.com/dfdx047/Fractal/commit/026b580a)).
- **HUD de rendimiento** con el aspecto de cada tema: FPS, tiempo de cuadro, uso de CPU y GPU, temperaturas, RAM,
  consumo y batería, con elementos, tamaño, opacidad, disposición y posición en los ajustes y en el menú del juego;
  portado del HUD de Winlator-Ludashi de StevenMXZ (GPL-3.0); uso de la GPU con los timestamps de los command buffers
  ([`bc5372e2`](https://github.com/dfdx047/Fractal/commit/bc5372e2), [`b558e01a`](https://github.com/dfdx047/Fractal/commit/b558e01a), [`378222ec`](https://github.com/dfdx047/Fractal/commit/378222ec)).
- Paquetes gráficos y trucos en el menú del juego, aplicados al juego en marcha (issue #5 en GitHub) ([`c6522627`](https://github.com/dfdx047/Fractal/commit/c6522627));
  los ajustes del juego en el menú del juego con **Reiniciar juego** ([`98364ff2`](https://github.com/dfdx047/Fractal/commit/98364ff2)).
- Cachés de shaders de la comunidad: descarga por juego y envío anónimo, revisado antes de incluirse
  ([`63968db1`](https://github.com/dfdx047/Fractal/commit/63968db1), [`c75e10c7`](https://github.com/dfdx047/Fractal/commit/c75e10c7)).
- Carpeta de datos en una carpeta elegida por el usuario (almacenamiento interno o tarjeta SD); instalar mods desde un
  .zip o una carpeta ([`5f607b21`](https://github.com/dfdx047/Fractal/commit/5f607b21)).
- Una resolución menor ofrecida una vez cuando un juego va muy por debajo de la velocidad completa ([`08c67298`](https://github.com/dfdx047/Fractal/commit/08c67298)).
- Música del menú por tema con un bucle sin cortes, sonidos del menú, y música y efectos propios del usuario
  ([`98364ff2`](https://github.com/dfdx047/Fractal/commit/98364ff2), [`f1d24be9`](https://github.com/dfdx047/Fractal/commit/f1d24be9), [`c8e0da47`](https://github.com/dfdx047/Fractal/commit/c8e0da47)).
- Los botones de los diálogos reparten el ancho o se apilan ([`af271be2`](https://github.com/dfdx047/Fractal/commit/af271be2)).

### Drivers de GPU

- Catálogo de drivers desde los releases de GitHub, instalación con un toque, limpieza de caché por driver
  ([`3927b698`](https://github.com/dfdx047/Fractal/commit/3927b698)); drivers experimentales para Mali, más fuentes,
  vuelta automática al driver del sistema ([`7ce49626`](https://github.com/dfdx047/Fractal/commit/7ce49626),
  [`9e8cf0c3`](https://github.com/dfdx047/Fractal/commit/9e8cf0c3)); página por desarrollador con el Turnip de MrPurple666
  primero, drivers y paquetes gráficos en la configuración inicial ([`0386ec86`](https://github.com/dfdx047/Fractal/commit/0386ec86)).
- GPUs Samsung Xclipse detectadas, con drivers RADV personalizados de cinco fuentes; aún sin probar en un dispositivo
  Xclipse ([`8da0d804`](https://github.com/dfdx047/Fractal/commit/8da0d804)).
- Paquete universal de PanVK de faux123 para Mali ([`45be70a4`](https://github.com/dfdx047/Fractal/commit/45be70a4)).
- Los perfiles por defecto de los juegos eligen el driver que no se cierra o es más rápido (Smash, Yoshi's Woolly World,
  Kirby and the Rainbow Curse, Rayman Legends, Sonic Lost World, FIFA 13; ver la tabla de arriba).

### Entrada

- Controles en pantalla: diseños independientes de la resolución, perfiles, macros, turbo, alternar, deslizar, modo de
  edición, colores del tema, estilos y botones de atajo ([`c813042e`](https://github.com/dfdx047/Fractal/commit/c813042e),
  [`9c15535f`](https://github.com/dfdx047/Fractal/commit/9c15535f), [`5af1e981`](https://github.com/dfdx047/Fractal/commit/5af1e981)).
- Mandos físicos asignados automáticamente ([`841f97a3`](https://github.com/dfdx047/Fractal/commit/841f97a3),
  [`e1698c63`](https://github.com/dfdx047/Fractal/commit/e1698c63)); HOME en el botón Mode/Guide
  ([`b343cab3`](https://github.com/dfdx047/Fractal/commit/b343cab3)); atajos ([`571c9d87`](https://github.com/dfdx047/Fractal/commit/571c9d87)).
- Los toques rápidos en los botones en pantalla ya no se pierden ([`8929b786`](https://github.com/dfdx047/Fractal/commit/8929b786)).
- Tipo de mando por juego (GamePad / Pro / Classic / Wiimote), un aviso único cuando el juego puede ignorar la
  configuración, opción de distribución de botones de Nintendo ([`5e6c4e42`](https://github.com/dfdx047/Fractal/commit/5e6c4e42)).

### Escalado

- AMD FSR 1 y Snapdragon GSR, nitidez RCAS (de Cemu Plus)
  ([`2c5dba03`](https://github.com/dfdx047/Fractal/commit/2c5dba03), [`8328081a`](https://github.com/dfdx047/Fractal/commit/8328081a)).

### Generación de fotogramas

- Generación de fotogramas de Lossless Scaling (de Cemu Plus, que la tomó de lsfg-vk y de Eden) con la `Lossless.dll`
  del propio usuario: multiplicador fijo 2x/3x/4x o adaptativo hasta 60/90/120 FPS, resolución de la estimación de
  movimiento, importación y ajustes desde la configuración y desde el menú del juego. Desactivada, no cambia nada en la
  creación del device Vulkan
  ([`0a4f24ce`](https://github.com/dfdx047/Fractal/commit/0a4f24ce), [`1fa22cec`](https://github.com/dfdx047/Fractal/commit/1fa22cec), [`eb14d852`](https://github.com/dfdx047/Fractal/commit/eb14d852)).
- Liberar el swapchain del GamePad con el juego en pausa ya no cuelga la interfaz ([`2171c12e`](https://github.com/dfdx047/Fractal/commit/2171c12e)).

### Depuración

- Informe de depuración: registro de rendimiento ligero en cada sesión, `events.txt` con cada problema encontrado,
  vigilante de cuelgues, notas de cierre que dicen qué hacía el hilo, log anterior conservado y **Compartir informe de
  depuración** en la app ([`be081807`](https://github.com/dfdx047/Fractal/commit/be081807)).
- Logs de cierres y cuelgues con código de la consola, registros del host y pilas de los hilos
  ([`c7923402`](https://github.com/dfdx047/Fractal/commit/c7923402), [`fcb9863e`](https://github.com/dfdx047/Fractal/commit/fcb9863e),
  [`f8dbb223`](https://github.com/dfdx047/Fractal/commit/f8dbb223)).
- Registro de rendimiento, escenas de benchmark con guion y herramientas de profiling (`tools/perf`).
- El informe de depuración se ofrece en el siguiente inicio tras un cierre (issue #2 en GitHub) ([`40d57923`](https://github.com/dfdx047/Fractal/commit/40d57923)).
- Vigilancia de escrituras para el código recompilado (`watch_jit=1`) ([`e79e82bd`](https://github.com/dfdx047/Fractal/commit/e79e82bd)); espera de GPU separada para
  copias de superficies lineales en el registro de rendimiento ([`52ff15de`](https://github.com/dfdx047/Fractal/commit/52ff15de)); toques en el GamePad y pulsaciones
  contadas en cuadros en los guiones de entrada ([`6a78d980`](https://github.com/dfdx047/Fractal/commit/6a78d980), [`5568a685`](https://github.com/dfdx047/Fractal/commit/5568a685)); 73 escenas de benchmark nuevas.
- `FRACTAL_TEST_BUILD=1` genera "Fractal Teste", que se instala junto a la app oficial ([`a4363e5f`](https://github.com/dfdx047/Fractal/commit/a4363e5f)).

### Mejoras medidas

De las mediciones A/B en los mensajes de commit (misma escena, mismo dispositivo, caché de shaders caliente). Las
condiciones cambian entre commits (driver, perfil de CPU), así que compara cada fila solo consigo misma.

| Cambio | Dispositivo | Antes → después | Commit |
|---|---|---|---|
| Sin fin de render pass para subidas de buffers / occlusion queries | RG Cube, Mario Kart 8 | 16,0 → 23,0 fps | [`2c1895ac`](https://github.com/dfdx047/Fractal/commit/2c1895ac) |
| Trabajo de vértices solapado con las barreras de render target | RG Cube, Mario Kart 8 | 24,3 → 30,2 fps | [`7b36890f`](https://github.com/dfdx047/Fractal/commit/7b36890f) |
| Compresión de framebuffer activa | RP5, Super Mario 3D World | 57,0 → 59,8 fps, 1 % low 30 → 44 | [`017be0ef`](https://github.com/dfdx047/Fractal/commit/017be0ef) |
| Recompilador: contador de ciclos en registro, área de código | RP5, Mario Kart 8 | 47,8 → 50,0-50,4 fps | [`23b10251`](https://github.com/dfdx047/Fractal/commit/23b10251) |
| Limpieza de depth con render pass | Mi A3, Shovel Knight | 34 → 52 fps | [`aabeea76`](https://github.com/dfdx047/Fractal/commit/aabeea76) |
| Sincronización de `GX2DrawDone` por juego | RP5, Breath of the Wild | 9,4 → 11,1 fps | [`951eb1e7`](https://github.com/dfdx047/Fractal/commit/951eb1e7) |
| 1-2 hilos de compilación de pipelines | Mi A3, SM3DW, caché fría | 17,0 s → 7,5 s en cuadros de más de 100 ms | [`9893cf72`](https://github.com/dfdx047/Fractal/commit/9893cf72) |
| Los pipelines precompilados coinciden con la caché del driver | Watch Dogs | congelamiento de 38-68 s → 9 s al entrar al juego | [`69ac8320`](https://github.com/dfdx047/Fractal/commit/69ac8320) |
| Resultado de las occlusion queries con la GPU ociosa | RP5 y POCO, Splinter Cell: Blacklist | 20 → 30 fps (límite del juego) | [`1557a268`](https://github.com/dfdx047/Fractal/commit/1557a268) |
| Recompilador: formas con overflow, saltos lejanos, ventanas para funciones enormes | Mi A3, Teslagrad | 1 % low 3,7 → 10,3 fps; código host 118 → 39 MB | [`66f59e89`](https://github.com/dfdx047/Fractal/commit/66f59e89) |
| Aviso de resolución (640x360) | Mi A3: Kirby and the Rainbow Curse / NSMBU / Captain Toad | 12 → 27 / 20 → 34 / 16 → 30 fps | [`08c67298`](https://github.com/dfdx047/Fractal/commit/08c67298) |
| Driver del sistema en vez de Mesa en Rayman Legends | RP5 (Turnip) / POCO (PanVK) | 17 → 59 fps / 18 → 29,6 fps | [`1f6476e3`](https://github.com/dfdx047/Fractal/commit/1f6476e3) |
| AES-128 con la Crypto Extension | POCO, sector de 32 KiB | 32 MB/s → 4,1 GB/s | [`852801bb`](https://github.com/dfdx047/Fractal/commit/852801bb) |
| Instrucciones CRC32 | POCO | 1,2 → 13,6 GB/s | [`64efc3e5`](https://github.com/dfdx047/Fractal/commit/64efc3e5) |
| Máximo del index buffer con NEON | POCO, 30000 índices | u16 51,5 → 4,3 µs, u32 35,6 → 8,4 µs | [`bd6af3dc`](https://github.com/dfdx047/Fractal/commit/bd6af3dc) |
| Decodificación BCn con paleta, con NEON | POCO, 1024x1024 | BC1 4,0 → 1,5 ms, BC3 5,7 → 2,8, BC4 3,8 → 1,3, BC5 7,0 → 3,2 | [`c4e6b0aa`](https://github.com/dfdx047/Fractal/commit/c4e6b0aa) |
| BC1-BC3 como ASTC 4x4 en drivers sin BCn | Mi A3 (driver de Qualcomm), POCO (Mali) | Texturas de SM3DW 272 → 144 MB; Captain Toad 35,9 → 37,2 fps; Batman AC 37,8 → 38,8 fps | [`e7ca709f`](https://github.com/dfdx047/Fractal/commit/e7ca709f) |
| Copias de streamout al final del render pass | POCO (Mali, PanVK) | BO2 17,2 → 34,6 fps (Mali), 3,0 → 15,7 (PanVK); Sonic Racing 20,8 → 22,7 | [`451ecf49`](https://github.com/dfdx047/Fractal/commit/451ecf49) |
| Las limpiezas de memoria saltan páginas ya en cero | Mi A3 | RAM de MK8 1410 → 997 MB a los 100 s; WD salta 512 MB de limpiezas | [`e4ae9d51`](https://github.com/dfdx047/Fractal/commit/e4ae9d51) |

---

## En curso

- **Profiling de CPU en Mali:** en el POCO X6 Pro (Bayonetta 2, Breath of the Wild, Xenoblade X, AC III) el bucle
  ocioso del núcleo PowerPC principal pasa cerca de la mitad del tiempo leyendo el reloj y tomando los bloqueos del timer
  y del audio. Quitar esos bloqueos de la espera no dio fps en el RP5, el POCO ni el Mi A3, así que quedó fuera; lo
  siguiente es el hilo de render.
- **Need for Speed: Most Wanted U en el POCO:** un cierre durante el juego con una build anterior, no visto otra vez en
  7 pruebas.
- **The Binding of Isaac: Rebirth (USA):** funciona (sonido, sin cierres, 58,7 fps en el RP5) con la imagen negra en
  todos los teléfonos; la vista del GamePad y la corrección del logic op no lo cambian.
- **FIFA 13 en el RP5:** cierre en el hilo de audio del juego (lee la dirección 0) en unas 3 de 10 pruebas, con
  cualquier driver.
- **Splinter Cell: Blacklist:** en las misiones solo se dibujan el cielo y el suelo (la wiki de compatibilidad de Cemu
  cita el mismo problema). Descartado por ahora: occlusion queries, la pirámide hi-Z y las consultas de mapeo de memoria.
- **Assassin's Creed III en el POCO:** el terreno y las rocas cercanas salen azul oscuro liso; en multi-core el juego no
  envía el draw del terreno, en single-core sí se dibuja.
- **Watch Dogs en Turnip:** un fallo de página de la GPU en una escritura de streamout puede colgar la GPU tras unos
  25 minutos. El driver del sistema no está afectado.
- **Watch Dogs sobreexpuesto en Android:** desde la sala de Maurice la exposición automática puede quedarse demasiado
  clara en cerca de la mitad de las sesiones, en Turnip y en el driver del sistema; la build de PC no está afectada.
  Descartado por ahora: NaN/Inf en los pixel shaders, draws omitidos durante la compilación asíncrona de pipelines y
  readback de texturas.
- **Watch Dogs en Mali** (RG Cube): el hilo de renderizado se queda compilando los enormes shaders de streamout; menú en
  bloques e intro negra.
- **Assassin's Creed IV, apagón en la selva:** tras la corrección del `fsel` el mundo todavía queda negro en cerca de 4
  de cada 10 pasadas por el puente (PC y Android). Descartado por ahora: la función de tiempo de cuadro en el
  intérprete, el redondeo de 25 bits de `frC` en las multiplicaciones y la reinterpretación lineal del mapa de oclusión.
  Siguiente paso: grabar el mapa de oclusión cuadro a cuadro alrededor del apagón.
- **Assassin's Creed IV, cinemáticas:** comparando con la build de PC; las espadas largas cuelgan rectas igual en PC y
  en la RP5, aún no confirmado como bug.
- **Odin 2 Mini en Turnip:** Tekken Tag Tournament 2 queda negro en el inicio con MrPurple T30; Batman y Watch Dogs se
  colgaron en parte de las ejecuciones. El Turnip 27.0 R2 de StevenMXZ se cierra dentro del driver al compilar
  pipelines de Ninja Gaiden 3.
- **Ninja Gaiden 3 en Turnip:** cabezas oscuras flotando y cielo quemado siguen tras la corrección de la profundidad de
  campo.
- **Need for Speed: Most Wanted U:** encontrar la causa raíz del follaje blanco (el paquete de la comunidad lo oculta).
  Sin la actualización (v0) el paquete no sirve: follaje y luces blancos de noche, imagen más oscura; la v32 sale bien.
  No es una regresión del código (la v0.1 hace lo mismo en v0) ni la caché de shaders; la misma prueba de alpha en los
  otros tres shaders de luz no sirvió. Siguiente paso: encontrar qué draw escribe el follaje blanco en v0 (shaders
  marcadores) y comparar los dumps de shaders de v0 y v32. Hasta entonces, hace falta la v32.
- **Xenoblade Chronicles X con el driver del sistema de Qualcomm:** personajes como siluetas blancas (bug del driver);
  por ahora usa Turnip.
- **Mali en general:** el POCO X6 Pro (Mali-G615) es ahora dispositivo de prueba principal; la RG Cube, en su modo
  económico de CPU, sigue como referencia para dispositivos Mali más débiles.

## Planeado

- Mediciones con escena fija en la Odin 3 y en la AYN Thor.
- Probar los drivers personalizados para Samsung Xclipse en un dispositivo Exynos.
- Generación de fotogramas también en la pantalla del GamePad y un botón rápido para ella en el menú del juego.
- Pruebas de compatibilidad en los principales juegos de Wii U y una lista pública de compatibilidad.
- Un botón "marcar problema" dentro del juego que guarde una captura y una marca en el log para los reportes.
- Streamout en drivers Mali sin stores en el pipeline de vértices ni transform feedback (por ejemplo con un pase de
  compute).
- Funciones de GPU que faltan, encontradas en las pruebas: compute draw calls, alpha-to-mask (`GX2SetAlphaToMaskReg`),
  `GX2ExpandDepthBuffer` y `SQ_VTX_SEMANTIC_CLEAR`, que nunca se aplica.
- Más rendimiento del lado de la CPU: Breath of the Wild y Mario Kart 8 en la RP5 están limitados por el núcleo PowerPC
  principal y el hilo de renderizado, no por la GPU.
- Seguir el `main` del Cemu oficial.
