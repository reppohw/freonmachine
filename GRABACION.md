# Freon Machine · grabar los lives (estudio, 17-11-2026)

Objetivo: grabar un live entero sin preocuparse de nada, y poder después:
1. Tener la **mezcla estéreo** tal como sonó.
2. Tener **pistas separadas** (multipista) para remezclar en un DAW.
3. Tener el live **como interpretación** (notas, knobs, cambios) para **volver a reproducirlo y corregirlo**: una nota mal, un preset que no quedó bien, un knob que se pasó.
4. Más adelante, **emitirlo en directo** (streaming) con una cámara, por ejemplo la del móvil.

**Estado:** fase 1 (estéreo) hecha y probada en el Mac el 17-11-2026; falta la prueba larga en la Pi. Aparte sigue la grabación al sampler (unos segundos a un pad).

---

## Lo que ya tenemos y ayuda

- **Canales del mezclador** (15): Kick, Snare, Drums rest, Sub, Track 2, Track 3, Track 4, Granular, Sampler, Breaks, Voice e Input 1-4. Más 2 grupos, los envíos de efectos y el máster.
  - La batería **ya sale en 3 canales** y las **entradas de la interfaz ya son canales** (el monotron puede ir por Input 1). Corrijo lo que dije: no hay que construir esos buses.
- **Todo lo que toca el motor pasa por dos puertas:** las notas MIDI entran por una cola al audio, con su momento exacto, y los cambios de parámetros (web, controladoras, memorias) pasan por una sola función (`set`). Por ahí se puede grabar **todo lo que pasa**, sin tocar cada motor.
- **El estado completo** se puede sacar en cualquier momento (es lo que guardan las memorias).
- **Ya existe un renderizador sin tiempo real** (`--render`): el motor puede generar audio más rápido que el tiempo real, sin tarjeta de sonido. Es la base para volver a reproducir un live.

---

## La propuesta: cada live es una «toma» con tres capas

Al pulsar Rec se crea una carpeta, por ejemplo `grabaciones/2026-11-17 2130/`, con:

| Capa | Qué guarda | Para qué |
|---|---|---|
| 1. Estéreo | `master.wav`: la salida tal como sonó | Escuchar, publicar, archivo |
| 2. Multipista | `pistas/Kick.wav`, `Snare.wav`… un WAV por canal, más los retornos de efectos | Remezclar en un DAW |
| 3. Interpretación | `live.freon`: el estado al empezar y cada nota, knob y cambio con su momento exacto. Más el audio de lo que entra de fuera (voz, monotron) | Reproducir el live de nuevo en Freon y corregirlo |

Todos los archivos empiezan en la misma muestra: se alinean solos en cualquier DAW.

### Capa 1: estéreo (lo primero, lo imprescindible)

- **Formato:** WAV de 24 bits a 48 kHz, y RF64 cuando pase de 4 GB (un WAV normal no puede ser mayor; RF64 lo abren Reaper, Ableton, Logic y Audacity).
  - Ocupa unos 1 GB por hora.
- **Seguro ante cortes de luz:** el archivo se va cerrando cada pocos segundos (se actualiza la cabecera), así que un apagón solo pierde los últimos segundos y el resto se abre bien. Si quedara a medias, Freon lo repara al arrancar.
- **Sin cortes de audio:** el hilo de audio nunca escribe en disco. Copia a una memoria intermedia (un anillo de unos 10 s) y otro hilo, con menos prioridad, la va pasando al disco. Si el disco se atasca más de 10 s, se marca el hueco y se avisa, pero el audio en directo no se corta nunca. Esta es la regla de oro.
- **«Grabar lo que acaba de pasar»:** opcional, pero muy útil en directo. Freon guarda siempre los últimos 5-10 minutos en memoria (unos 120-230 MB de RAM). Si te olvidaste de grabar y algo sale bien, pulsas «Guardar lo último» y lo tienes.
- **Marcas:** un botón (y un gesto en el LPD8) deja una marca en ese momento («aquí empieza el tema 3», «aquí me equivoqué»). Van dentro del WAV (marcadores BWF), y los DAW las muestran.

### Capa 2: multipista

- **Qué pistas:** los 15 canales, los retornos de efectos (reverb, delay y los demás envíos) y el máster.
  - Las pistas de canal se graban **después de su cadena (EQ, drive, filtro) y antes del fader**, con el pan y los envíos aparte. En el DAW tienes cada sonido «como sonaba», y el nivel lo rehaces tú.
  - Opción en Settings para grabarlas después del fader, si prefieres que sumadas den exactamente la mezcla.
  - Solo se graban los canales que suenan: uno que no ha sonado en toda la toma se borra al terminar.
- **Cuánto ocupa:** 15 canales en estéreo, más 4 retornos y el máster, son unos 20 pares: ~20 GB por hora en 24 bits. Por eso:
  - **Recomendación: un SSD por USB** (por ejemplo 500 GB, menos de 60 €). La tarjeta SD de la Pi aguanta el estéreo, pero el multipista la llenaría pronto y la desgastaría, y un atasco de escritura de la SD es justo lo que puede cortar una grabación.
  - Con FLAC ocuparía la mitad, pero gasta procesador en directo. Mejor grabar WAV y, si quieres, comprimir después, cuando la Pi esté parada.
- **CPU:** copiar las pistas es casi gratis; el coste está en el disco. Se mide en la Pi antes de darlo por bueno (prueba de 1 hora grabando todo, mirando cortes y CPU).

### Capa 3: la interpretación (el live como «MIDI», y más)

La idea de grabar el MIDI es muy buena, y se puede hacer mejor que un MIDI normal. Freon no guardaría solo las notas: guardaría **todo lo que le pasa al motor**.
- **Al empezar:** el estado completo (como una memoria) y la semilla de cada generador aleatorio.
- **Durante:** cada nota (del Oxi, del LPD8, del teclado), cada parámetro que cambia (venga de la web, de una controladora o del Oxi), cada memoria o combinación que se carga, y los cambios de tempo. Cada cosa con su **muestra exacta**, no con el tiempo aproximado del MIDI.
- **El audio que entra de fuera** (voz, monotron, Input 1-4) se graba siempre como audio, porque no se puede volver a generar.

Pesa muy poco: unos pocos MB por hora.

**Volver a reproducir el live:**
- Freon carga la toma y la vuelve a generar sola, sin tiempo real y más rápido de lo que duró (en el Mac, varias veces más rápido; en la Pi hay que medirlo).
- Sale un estéreo y un multipista nuevos, sin haber tocado nada.

**Corregir:**
1. **Desde Freon:** cambiar un preset o un parámetro de un instrumento para toda la toma (por ejemplo, el Phatty con el filtro Improved), silenciar o arreglar un tramo, mover una marca. Luego se vuelve a generar.
2. **Desde tu DAW:** Freon exporta la toma como **MIDI estándar**, un archivo `.mid` con una pista por instrumento, el tempo del Oxi y los knobs como automatización. Lo corriges en Ableton o Reaper (una nota, cuantizar, quitar un golpe), lo devuelves a Freon y vuelve a generar el audio con sus motores. Así corriges con las herramientas que ya conoces.
3. Más adelante, si hace falta, un editor sencillo en la web (lista de notas por pista, borrar o mover).

**Lo que hay que saber (límites honestos):**
- **No será idéntico bit a bit, pero sí musicalmente igual.** Las colas de reverb y delay que ya sonaban antes de pulsar Rec, el ruido y las partes aleatorias (deriva Vintage, S&H, dispersión del granular, Random de los breaks) dependen de lo que pasó antes. Guardando las semillas al empezar se acerca mucho. Se comprueba con una prueba de cancelación: el original y lo regenerado restados deberían quedar muy por debajo de la mezcla.
- **El granular grabando de la voz o de las entradas** se regenera bien solo porque el audio de entrada queda grabado (por eso la capa 3 lo incluye).
- **Los samples y breaks** tienen que seguir en la Pi. La toma guarda sus nombres y una huella: si cambias un WAV, avisa al regenerar.

### Controles

- **Web (también en el móvil):** botón ● Rec arriba, junto a ⌂ Home, con el tiempo grabado, el espacio libre y un aviso si el disco no llega. Tarjeta en Settings para elegir capas, pistas, disco y «Guardar lo último». Lista de tomas con descarga.
- **Sin pantalla:**
  - LPD8: un gesto como el de Home (por ejemplo, mantener dos pads 1 s) que arranca o para la grabación; los pads parpadean en rojo mientras graba.
  - Más adelante, un botón dedicado en la BCR32 o la Launch Control.
- **Arranque automático:** opción de empezar a grabar al recibir el Play del Oxi y parar con su Stop. Así cada live queda grabado sin pensar.
- **Descarga:** las tomas se bajan desde la web (en el móvil, la mezcla; en el ordenador, la carpeta entera en un ZIP) o se copian del SSD.

---

## Streaming (para más adelante, pero ya con la idea)

Lo más sencillo y sólido:
- **El móvil hace de cámara y de emisor**, con una app de streaming que admita audio externo, como Larix Broadcaster o la propia de YouTube o Instagram.
- **El audio va limpio:** de la salida de la interfaz a la entrada del móvil, con un adaptador USB-C de audio o una pequeña interfaz para móvil. Freon no hace nada, y el vídeo no le quita procesador a la Pi.
- **Alternativa con más control:** Freon emite su máster por la red (Opus por WebRTC) y un ordenador con OBS junta la cámara del móvil y ese audio. Es más trabajo, y otro aparato en el escenario.
- La grabación de Freon es independiente del streaming: aunque falle internet, el live queda grabado en limpio en la Pi.

---

## Orden de trabajo propuesto

| Fase | Qué | Por qué en ese orden |
|---|---|---|
| 1 | Estéreo sólido: grabación a disco sin cortes, seguro ante apagones, botón en la web y en el LPD8, marcas y «Guardar lo último» | Es lo imprescindible y la base del resto. Se prueba 1 hora en la Pi |
| 2 | Capa de interpretación más audio de las entradas, y regeneración sin tiempo real, con prueba de cancelación | Es lo más original y lo que permite corregir |
| 3 | Multipista (al SSD) | Necesita el disco; reaprovecha todo lo de la fase 1 |
| 4 | Exportar e importar MIDI estándar para corregir en el DAW | Reaprovecha la fase 2 |
| 5 | Arranque con el Play del Oxi, descarga en ZIP, editor sencillo en la web | Comodidad |
| 6 | Streaming | Cuando lo demás esté probado en directo |

Cada fase se prueba en la Pi con el live cargado (CPU, cortes y una grabación larga) antes de pasar a la siguiente.

## Decisiones (17-11-2026)

- **Pi:** 8 GB de RAM y tarjeta SD de 64 GB. El estéreo va a la SD; el multipista, al SSD por USB (sin SSD no se activa y avisa). Más adelante, quizá un disco interno.
- **Multipista después del fader:** las pistas sumadas dan exactamente la mezcla.
- **DAW:** Pro Tools o Ableton.
  - WAV de 24 bits con marca de tiempo (BWF). Pro Tools coloca cada pista con «Spot»; Ableton, desde el inicio.
  - Sin RF64: si una toma pasa de ~3 h (cerca de los 4 GB por archivo), se abre un archivo nuevo.
  - El MIDI exportado es estándar, con los knobs como CC (Pro Tools no importa automatización de plugins desde MIDI).
- **Sin arranque con el Play del Oxi:** Jorge usa el Play/Stop del Oxi musicalmente (parones). Se graba solo con el botón Rec (web, o un botón de controladora, por ejemplo de la Launch Control XL).
- **Guardar lo último:** 15 minutos en estéreo (~350 MB de RAM), cambiable en Settings.

## Fase 1 hecha (17-11-2026)

- **Código:** `src/recorder.h` (anillo, hilo de escritura, WAV, «lo último», reparación) y su conexión en `freon-machine.cpp` (`trec…`). El servidor web sirve `/rec/TOMA/ARCHIVO` por trozos (`src/server.h`).
- **Tomas:** `grabaciones/AAAA-MM-DD HH-MM-SS/master.wav` (24 bits, BWF con la hora de inicio) y `marcas.txt`. «Lo último» crea `… (last N min)/master.wav`. Si una toma pasa de 3,5 GB sigue en `master 2.wav`.
- **Controles:**
  - Web: ● Rec arriba (en marcha muestra el tiempo; parar pide un segundo toque), ◆ marca, y la tarjeta Settings › Recordings (guardar lo último, espacio libre, descargar, borrar).
  - Escuchar: ▶ en cada toma abre un reproductor en el propio móvil u ordenador (no suena por la Pi). El servidor sirve trozos del archivo (Range), así que se puede saltar a cualquier punto sin bajar la toma entera.
  - Canal del mezclador: nota 56 Rec/Stop, 57 marca, 58 guardar lo último. La luz de «grabando» (nota 96) se enciende.
- **Seguridad:** no arranca con menos de 500 MB libres y para sola por debajo de 300 MB. Si el disco no llega, se meten ceros del mismo largo y una marca «Gap».
- **Probado en el Mac** (sin tarjeta, `--sin-audio`):
  - WAV correcto: 24 bits, 48 kHz, hora BWF y marca en su sitio.
  - La descarga es idéntica byte a byte, y un intento de salir de la carpeta da 404.
  - Matando el motor a la fuerza en mitad de una toma, al arrancar queda reparada.
  - «Lo último» guarda lo que haya.
  - El motor entero gasta ~3 % de CPU.
- **Pendiente en la Pi:**
  - Grabar 1 hora con el live cargado, mirando CPU, cortes y huecos.
  - Comprobar la RAM de «lo último» (~260 MB fijos con mlockall).
  - Probar la nota 56 desde una controladora.
