# Freon Machine

(Antes «groovebox».)

Es el aparato completo menos el secuenciador. El Oxi One toca, y este programa pone los
sintes, las baterías, el sampler, los breaks, el granular, la voz, la mezcla y los efectos.
La pantalla es una página web que sirve el propio programa. Es el mismo código que irá en
la Raspberry.

## Qué hay

Todo se puede tocar desde controladoras MIDI, sin pantalla (ver «Controladoras»). La página web
queda para configurar desde el móvil o el portátil. Está en inglés y tiene cuatro pestañas:
Instruments, Live, Mixer y Settings, con **todos** los parámetros a la vista.

### Instrumentos

- **T1 Batería:** 8 sonidos y 5 kits. Hay botón de reset por sonido y para todo el kit (no toca las notas MIDI). Debajo de cada sonido están el silencio y el canal de mezcla (Bombo, Caja o Resto). Los kits:
  - **808 y 909:** bombo, caja, toms y rim con los modelos de Mutable Instruments **Plaits**. Hi-hats, ride 909 y claps con voz propia basada en los circuitos: los hi-hats de Plaits salían con ruido muestreado y sin agudos.
    - Knobs: Level, Pitch, Decay, Tone (el TONE de la 808/909, o el brillo en hats y clap), Snap, Sweep, Filter, Reso, y en la fila extra Velocity y Pan.
    - **Snap:** golpe de ataque en bombo, tom y rim; el «snappy» (los bordones) en la caja; ruido en los hihats; número de manos en el clap.
    - **Sweep:** barrido de tono en bombo y tom; el filtro cae en los hihats; separación entre manos en el clap.
  - **FM:** al estilo del Model:Cycles. Cada sonido es una máquina FM (bombo, caja, metal, perc, clap) con sus cuatro macros, y cada macro mueve varios parámetros internos a la vez.
    - Knobs: Level, Pitch, Decay, **FM**, **Ratio**, **Sweep**, Filter, Reso; en la fila extra, **FM decay**, **Noise**, Velocity y Pan.
    - **FM:** cantidad de modulación (brillo).
    - **Ratio:** relación del modulador (el carácter).
    - **Sweep:** barrido de tono (de filtro en los hats).
    - **FM decay:** lo que dura la modulación.
    - **Noise:** ruido encima; en la caja actúa siempre, en el resto de la mitad para arriba.
    - En el clap no hay FM: FM es el tono, Ratio las manos y FM decay la separación.
  - **Chip** (Game Boy) y **Experimental:** Level, Pitch, Decay, Velocity, Snap, Sweep, Filter, Reso y Pan.
  - En todos los kits:
    - **Filter:** a la izquierda paso bajo, a la derecha paso alto.
    - **Velocity:** cuánto cambia el golpe con la fuerza (volumen y brillo). A 0 suena siempre igual.
- **Juno, Moog y 303 con núcleos probados:**
  - **Juno:** usa el motor de KR-106 en modo **Juno-6**. Es una emulación calibrada con medidas de Junos reales: DCO, filtro IR3109 con las curvas del Juno-6, paso alto continuo, una sola ADSR y chorus BBD. Los knobs son los deslizadores del panel, de 0 a 1.
  - **303:** usa **Open303**, de Robin Schmidt, con el filtro de diodos medido sobre el aparato y la misma lógica de accent y slide. Como en la 303 de verdad, la resonancia alta se come los graves.
  - **Moog:** su filtro es el ladder de **Huovilainen**, el modelo clásico del filtro Moog. Cada etapa satura y, pasado el 90 % de Emphasis, autooscila.
- **DX7:** usa **MSFA**, el motor de Google del que salió Dexed: 6 operadores y 32 algoritmos.
  - **Sonidos:** toca los 8 de serie y cualquier banco del DX7 que copies en la carpeta `dx7/`, en archivos `.syx` de 32 voces o de una voz. Al arrancar los lee todos.
  - **Knob Patch:** recorre la biblioteca con los nombres de los sonidos.
  - **Resto de knobs:** retocan el sonido cargado. En el centro está el sonido tal cual.
    - Brightness: nivel de los moduladores.
    - Mod decay: cuánto tarda en apagarse el brillo.
    - Feedback, Detune y Vibrato.
    - Tone: un paso bajo de salida.
    - Attack, Decay, Sustain y Release.
    - Transpose, Vib rate y Velocity.
    - Algorithm: «Patch» deja el del sonido; 1–32 lo cambia.
  - **Cuándo se aplican:** como al editar un DX7, los cambios valen para las notas nuevas.
- **T2, T3 y T4:** cada una elige entre 8 motores: Juno, DX7, Moog, 303, Game Boy,
  Wavetable (6 tablas), Supersaw / string machine y Modal (cuerda, barra, campana y placa
  golpeadas, punteadas o frotadas). Todos con sus parámetros
  por secciones y **8 presets** por motor. Para guardar: pulsa «Guardar» y
  luego el hueco.
  - En la pantalla, los knobs salen **en el mismo orden que en la controladora**. Primero los 16
    de la capa principal, en dos filas de 8 y con su número de CC. Luego la segunda página
    (CC 40 en adelante) y después los botones.
  - Los **16 knobs de controladora** de cada motor: Fila de arriba: Cutoff,
    Resonance, Env amount, A, B, C, D y Volume. Fila de abajo: ADSR de filtro y ADSR de
    amplitud. A–D son los cuatro parámetros continuos más característicos de cada motor
    (ver la tabla al final).
  - **Velocidad:** tocar flojo suena claramente más flojo (unos 20 dB) y, en los motores
    con filtro, más cerrado. El knob Velocity de cada motor lo regula. El 303 tiene su
    propio knob de velocidad a volumen, además del accent (velocidad 100 o más). El órgano
    no responde a la velocidad, como uno de verdad.
  - Al cambiar de motor, las notas del anterior se apagan en 10 ms y no vuelven a sonar
    si vuelves a él.
- **Sub:** módulo aparte con su propio canal MIDI (9 por defecto). Toca las notas que le
  lleguen por ese canal. En el Oxi, manda a ese canal la nota grave del secuenciador de
  acordes. Tiene Octave, Tone y Glide (CC 16–18) y se enciende con CC 110. Sale por el canal
  Sub de la mezcla.

### Directo

- **Sampler:** 16 pads.
  - Cada pad tiene afinación, inicio y fin (también arrastrando sobre la onda), modo
    (entera, mientras pulsas o en bucle), reverso, ataque, release, filtro, resonancia,
    drive, bits, nivel, pan, grupo de corte y sensibilidad a la velocidad.
  - Trae 16 sonidos de serie hechos con los propios sintes.
  - Los WAV que copies a la carpeta `samples/` aparecen en la lista de cada pad.
  - **Grabar:** elige la fuente (Entrada 1, Entrada 2, Entradas 1+2, Mezcla o Máster),
    pulsa Grabar y otra vez para parar. Se guarda en `samples/` y queda en el pad elegido.
- **Breaks:** un loop cortado en semicorcheas que sigue el tempo.
  - 3 de serie; copia los tuyos a `breaks/`. Al elegir uno se calcula cuántos compases
    tiene (corrígelo con el knob Compases si hace falta).
  - 8 efectos mantenidos: repetir, repetir a 1/32, reverse, medio tiempo, aleatorio,
    octava abajo, cortar y hacia atrás.
  - Knobs de «caos»: aleatorio, reverso y repetición. «Repeat size» elige el tamaño del
    trozo que repite el botón Repeat (de 1/32 a un compás).
  - Pulsa un corte para saltar a él.
  - Suena mientras el Oxi está en marcha; sin reloj, con el tempo de la pantalla.
- **Granular:** graba sin parar los últimos 8 s de una fuente y los reparte en granos.
  - Fuentes: la mezcla, una entrada, la batería, una pista, el sampler, los breaks o la
    voz.
  - Posición, tamaño, densidad, afinación, dispersión, estéreo, reverso y feedback.
  - **Congelar:** deja de grabar y los granos salen de lo que ya hay.
  - Modo «Notas MIDI»: solo suena mientras llegan notas y las sigue de tono.
  - Visualizador con lo grabado, la zona de lectura y cada grano.
- **Voice:** transformador de voz en vivo.
  - Limpieza: entrada, ganancia, paso alto, puerta, compresor y de-esser.
  - Detecta el tono que cantas (lo enseña en pantalla) y cambia tono y formantes por
    separado, sin efecto «ardilla».
  - Modos: Normal, Auto-pitch (afina a la tonalidad y escala; Retune = cuánto),
    Robot (canta una nota fija o la que le mandes por MIDI), Harmony (voces añadidas o
    las notas MIDI que toques) y Vocoder de 16 bandas (portadora de sierra o pulso
    tocada por MIDI, o una de las pistas).
  - Efectos: megáfono, radio, estroboscopio, lo-fi y drive, más reverb y eco a tempo.
  - Balance limpio/procesado y nivel.

### Mezcla

- **15 canales:** Bombo, Caja, Resto, Sub, las 3 pistas, Granular, Sampler, Breaks, Voz
  y 4 entradas de audio.
- **Todo a la vista en cada canal:** botón Reset, salida (máster o grupo), distorsión
  (Fuzz o Bitcrusher) con drive, tono y mezcla y **nivel autorregulado** (subir el
  drive no sube el volumen), EQ de 3 bandas, pan, limitador, sidechain con cantidad y suelta,
  4 envíos, fader, M, S y C (pre-escucha).
- **2 grupos** con distorsión, filtro DJ, sidechain y limitador.
- **4 efectos de envío a elegir:** hall, placa (Dattorro), agujero negro, ping-pong, cinta,
  dub, multitap y chorus. Las reverbs tienen Mod (movimiento de la cola) y Low cut (quita graves
  antes de entrar). Los delays deslizan el tiempo sin saltos, y cinta y dub tienen dos cabezas estéreo.
- **Snapshots:** 8 memorias de toda la mezcla (Save y luego el hueco) y Reset mixer, que
  pide una segunda pulsación para confirmar.
- **Máster:** eliges el efecto con los botones, lo metes poco a poco con Amount y lo moldeas con
  sus knobs. Zero lo devuelve a cero de golpe y All off apaga todos. El piloto rojo marca los que
  están sonando. Además distorsión y limitador.
  - **Filter:** resonancia y Env (los golpes abren el filtro).
  - **Repeat:** tamaño, Decay y Pitch (cada vuelta sube o baja: el redoble que trepa).
  - **Gater:** división, Shape (de corte seco a trémolo) y Pan (alterna izquierda y derecha).
  - **Riser:** ruido. **Tape stop:** inercia.
  - **Space:** tamaño, Damp y Freeze (congela lo que ya suena).
  - **Throw:** tiempo, Feedback y Tone.
  - **Comb:** Pitch (nota MIDI), Feedback y Damp: un eco tan corto que se vuelve nota.
  - **Sweep:** phaser o flanger a tempo, con Rate (de 1/4 a 8 compases), Depth y Feedback.
- **Combinaciones (8 memorias):** juntan los efectos de máster que tú elijas, por ejemplo filtro,
  delay y repeat. Ajusta los efectos como quieras, pulsa Save y elige un hueco: entran los que tengan
  cantidad, con todos sus knobs, y el botón muestra cuáles son. Pulsar la deja puesta; mantener solo
  suena mientras mantienes.
  - **Crossfader Dry–Wet (CC 95 en el mezclador, knob 8 arriba en el MC-24):** lleva la combinación activa de seco a efecto, moviendo todos sus knobs desde donde estaban hasta lo guardado.
  - **Editarla:** con una activa, lo que muevas en sus efectos se guarda en ella al momento; si subes
    otro efecto se añade, y si bajas uno a cero sale. Save sobre un hueco ocupado lo sustituye.
  - Al apagarla, todo vuelve a como estaba, y los efectos que no están en ella siguen libres. All off
    también la apaga.
- **Suma analógica (knob Summing, junto a la distorsión):** los canales se juntan como en una mesa:
  un canal solo sale igual que entró, pero al sumarse se empujan un poco entre sí. A 0 es una suma
  digital normal. Basada en la idea de Console, de Airwindows.
- **Salidas directas (mezclador analógico):** si la interfaz tiene más de 2 salidas, cada
  canal o grupo puede salir por su propio par (Out 3-4, 5-6…) en vez de ir al máster.
  La pre-escucha usa 3-4 solo si ningún canal las ocupa.
- **CPU:** arriba se ve cuánto del tiempo de cada bloque de audio se gasta en calcular. Por
  encima del 70 % puede crujir.
- **Pre-escucha:** por las salidas 3-4 si la interfaz las tiene; si no, máster por la
  izquierda y pre-escucha por la derecha.
- **Tempo:** los efectos y los breaks van a tempo con el reloj del Oxi. Si no llega reloj,
  usan el tempo que pongas en la pantalla.

### MIDI

Canales por defecto, todos configurables en Settings:

| Pista o módulo | Canal |
|---|---|
| Batería | 10 |
| Pistas 2-4 | 2, 3 y 4 |
| Sampler | 5 |
| Breaks | 6 |
| Granular | 7 |
| Voz | 8 |
| Sub | 9 |
| Mezclador (controladoras) | 16 |

- **Nombres de nota:** como en el Oxi (36 = C1, 60 = C3).
- **Notas del sampler:** 36–51 son los pads 1–16. De la 60 en adelante tocan el pad
  elegido en cromático; 72 es el tono original.
- **Notas de los breaks:** 36–51 saltan al corte y 60–67 son los efectos mantenidos.
- **Entradas MIDI:** se abren todas a la vez (USB, Bluetooth, controladores) y las que
  conectes después aparecen solas en 2 segundos. El reloj se toma solo de la primera que
  lo mande. No conectes el Oxi por USB y por Bluetooth a la vez: las notas llegarían dobles.

### Controladoras: todo sin pantalla

Las controladoras se conectan a la Raspberry (no al Oxi). Cada una manda por el canal del
instrumento que controla, y el mezclador tiene su propio canal (16 por defecto). El programa
les devuelve los valores y las luces: al cargar un preset o un snapshot, al cambiar de motor
o de kit, al hacer un reset o al tocar en la web, los anillos y LEDs se ponen al día. Al
conectar una controladora se le manda todo. Al Oxi no se le devuelve nada (en Settings,
«Don’t send to»).

**Modo de los knobs** (Settings → Controllers):

- *Pickup*, por defecto: para potenciómetros y faders. El knob no hace saltar el valor; empieza a mandar
  cuando pasa por el valor actual.
- *Relative*: para encoders sin fin. Hay dos variantes según la controladora: *two's complement* o
  *binary offset*.
- *Absolute*: el valor salta al del knob.

**Botones** (mutes, solos, cue, encendidos): una pulsación corta cambia y se queda. Si lo
mantienes más de 0,4 s, solo dura mientras lo mantienes y vuelve al soltar. Funciona igual con
botones momentáneos y con los que la controladora manda como interruptor (127 y luego 0).

**Solos:** en modo *latch* se suman. En modo *X-OR* (botón «Solo X-OR» en Mixer, o nota 62),
al encender un solo se apagan los demás.

**Sintes** (canal de cada pista):

- CC 16–31: los 16 knobs de la capa principal.
- CC 40–63: el resto de parámetros.
- CC 102–109: los botones del motor (pulsar = encender/apagar).
- CC 110: shift.
- Program Change 0–7 elige motor y 10–17 carga los presets 1–8. Con shift pulsado, los guarda.
- CC 3: motor (0–7). CC 4: preset (0–7). Con el número tal cual, para el Oxi. En el canal de la batería, CC 3 es el kit (0–4). Solo actúan si el número cambia.
- CC 8–14: los 7 primeros knobs de la capa (Cutoff, Reso, Env, A, B, C, D). CC 15: crossfader Dry–Wet. Es la fila de arriba del MC-24.

**Instrumentos para el Oxi:** en `oxi-instrumentos/` hay 13 instrumentos para importar con la OXI App (ver su LEEME.txt). Con el Oxi, los knobs siempre van en absoluto, sea cual sea el modo elegido.

**Batería** (canal 10):

- CC 16–79: 8 knobs × 8 sonidos. Las columnas van en el orden de la pantalla: bombo, caja,
  HH cerrado, HH abierto, clap, rim, tom y perc. El knob k de la columna c es el CC 16 + 8·k + c. Los 8 knobs son los de cada kit, en el orden de la pantalla.
- CC 80–87: pan. CC 88–95: el knob extra del kit (Velocity en 808/909, FM decay en FM). En FM, además, CC 96–103: Noise y CC 112–119: Velocity.
- Program Change 0–4: kit (808, 909, FM, Chip, Experimental).

**Sampler, breaks, granular y voz:**

- Sampler: CC 14 elige el pad (0–15), CC 16–31 son sus parámetros y CC 111 graba.
- Breaks: CC 16–27 son los parámetros. CC 102–109 son los efectos mantenidos, que suenan mientras
  pulsas. CC 110 enciende y apaga.
- Granular: CC 16–26, CC 110 enciende y apaga, CC 111 congela.
- Voz: CC 16–31 y 40–47, CC 110 enciende y apaga.

**Mezclador** (canal 16):

| Qué | Control |
|---|---|
| Nivel de los 15 canales / volumen máster | CC 16–30 / CC 31 |
| Pan · envío A · envío B · drive | CC 32–46 · 48–62 · 64–78 · 80–94 |
| Efectos de máster: cantidad (Repeat, Gater, Riser, Space, Throw, Tape stop, Filter) | CC 102–108 |
| Su segundo knob (tamaño del repeat, división, ruido, tamaño, tiempo, inercia, resonancia) | CC 111–117 |
| Comb · Sweep: cantidad | CC 96 · 97 |
| Filter Env · Repeat Decay · Repeat Pitch · Gater Shape · Gater Pan | CC 1 · 2 · 3 · 4 · 5 |
| Space Damp · Throw Feedback · Throw Tone | CC 8 · 9 · 10 |
| Comb Pitch · Feedback · Damp | CC 11 · 12 · 13 |
| Sweep Rate · Depth · Feedback · Flanger (botón) | CC 14 · 15 · 47 · 63 |
| Space Freeze (botón) | CC 79 |
| Crossfader Dry–Wet de la combinación | CC 95 |
| Todos los efectos de máster a cero / shift | CC 109 / CC 110 |
| Mute · solo · cue de cada canal | notas 0–14 · 16–30 · 32–46 |
| Snapshots 1–8 (con shift, guardar) / mute de los grupos | notas 48–55 / 60–61 |
| Modo de los solos: se suman (latch) o exclusivo (X-OR) | nota 62 |
| Apagar la Pi (mantener 3 s; solo si se arrancó con `--apagado-midi`) | nota 127 |
| Combinaciones 1–8: pulsar = se queda, mantener = momentáneo (con shift, guardar) | notas 64–71 |

**Luces de estado** (notas 96–104 en el canal del mezclador, a una botonera con LEDs):

| Nota | Se enciende cuando... |
|---|---|
| 96 | se está grabando |
| 97 | llega reloj del Oxi |
| 98 | el máster satura |
| 99 | el procesador pasa del 70 % |
| 100 | el reloj se ha perdido con el Oxi en marcha |
| 101 | no se pudo guardar, o el audio se ha caído |
| 102–104 | breaks, granular y voz están encendidos |

### Knobs en la pantalla

Arrastra para moverlos. El scroll mueve la página; para mover un knob con la rueda o dos dedos, mantén ⌥ (Alt).

### Teclado del ordenador, para probar sin el Oxi

- 1–4 eligen pista, 5 el sampler (las 16 teclas son los pads), 6 el granular, 7 la voz y 8 el Sub.
- A W S E D F T G Y H U J K O L P son notas.
- Z / X cambian de octava.

### Guardado

El estado se guarda solo en `freon-estado.json`, con los archivos asignados a cada pad.
La primera vez que arranca con el nombre nuevo, recoge sola el estado del groovebox si la
carpeta `groovebox` está al lado (por ejemplo, las dos en el Escritorio).
Se escribe aparte y se cambia de golpe, y la versión anterior queda como
`freon-estado.json.bak`. Un apagón a mitad de guardado nunca deja el archivo vacío; si
el archivo queda dañado, se carga la copia.

## Instalar en el Mac (una sola vez)

1. Abre la app **Terminal**.
2. Si no lo hiciste ya con la 303: `xcode-select --install`
3. Descomprime `freon-machine.zip` en el Escritorio y entra en la carpeta:

       cd ~/Desktop/freon-machine

4. Compila (un minuto):

       ./build.sh

## Usarlo

1. Conecta el Oxi por USB y enciende la interfaz.
2. Mira los números de tus interfaces, de las entradas y del Oxi:

       ./bin/freon-machine --list

3. Arranca (cambia el número por el de tu interfaz, o pon parte de su nombre):

       ./bin/freon-machine --audio 130
       ./bin/freon-machine --audio "MacBook"

4. Abre **http://localhost:8080** en Chrome o Safari.
5. En **Ajustes**, pon los canales MIDI que manda cada pista del Oxi y la nota de cada
   sonido de la batería.
6. Para salir: `Ctrl + C` en la Terminal.

### Entradas de audio

- Si la interfaz de salida tiene entradas (la MOTU, por ejemplo), se usan esas, hasta 4.
- En el Mac, los altavoces y el micro son dispositivos distintos. Si la salida no tiene
  entradas, se abre la entrada por defecto del sistema.
- Para elegir otra, mira el número en `--list` y añade `--entrada N`. Por ejemplo:

      ./bin/freon-machine --audio 130 --entrada 129

- La primera vez, macOS pedirá permiso para usar el micro: acéptalo para la Terminal.
- Las entradas salen en los canales Entrada 1–4 de la mezcla y alimentan la Voz, el
  Granular y la grabación del sampler.
- Si usas la Voz, baja o silencia en la mezcla el canal de la entrada que uses, para no
  oírla dos veces.
- Usa auriculares para no acoplarte.

### Otras opciones

- `--out 3`: salir por las salidas 3-4.
- `--buffer 64`: menos latencia, si tu interfaz lo aguanta (por defecto 128 = 2,7 ms).
- `--host 0.0.0.0`: abrir la pantalla desde el iPad o el móvil en la misma red, con
  `http://IP-del-Mac:8080`.
- `--sin-entradas`: no abrir ninguna entrada.
- `--entrada "Scarlett"`: la entrada también se puede elegir por nombre.
- `--esperar-audio 60`: si la interfaz aún no está (la Pi recién encendida), esperar hasta 60 s.
- `--apagado-midi`: permite apagar la Pi con la nota 127 del canal del mezclador (3 s).

Si la interfaz de audio se desconecta en pleno concierto, el programa lo nota en un segundo
y la vuelve a abrir en cuanto reaparece.

**Volumen de instrumento:** en cada tira de Mixer de Sub y sintes, junto al Limiter, está el knob Volume del propio instrumento: es el mismo que en Instruments, y al mover uno se mueve el otro. En los sintes se guarda con el preset. El del Sub también va por CC 19 en su canal.

**Trim de batería:** las tiras Kick, Snare y Drums rest tienen un Trim junto al Limiter, de −24 a +12 dB, para subir o bajar el grupo antes del fader.

**Sidechain:** bombeo. Solo cuenta el golpe del canal fuente (un salto brusco de nivel, aunque el anterior siga sonando): el sonido baja en 1 ms y sube en línea recta durante SC release, hasta 1 s. Para pads, prueba entre 200 y 400 ms según el tempo. SC amount es la profundidad: al 100 %, unos −30 dB.

**Compresor de bus (efectos de máster):** une la mezcla. Amount baja el umbral (hasta −24 dB), Ratio 2:1, 4:1 o 10:1, Attack, Release y Mix (en paralelo). Recupera nivel solo y muestra cuánto comprime. Para pegar sin que se note: 4:1, Attack 10–30 ms, 1–3 dB de reducción.

**Sub:** sigue la nota más grave de una pista (T2 por defecto) o toca lo que le llega por su propio canal MIDI; se elige en su tarjeta.

**Núcleos:** en la Pi el audio se reparte entre 4 núcleos (batería, cada pista de sinte, sampler, breaks, granular y cada efecto de envío pueden ir por separado). En el Mac va en uno, porque allí no hay tiempo real para los demás. `--nucleos 1` lo pone todo en un núcleo, y suena exactamente igual: es la salida de emergencia si algo fallara en la Pi. Si un núcleo no consigue prioridad de tiempo real, se queda fuera y el programa lo avisa al arrancar («Núcleos para el audio»).

**Niveles:** `./bin/freon-machine --niveles` mide la sonoridad de todos los motores y presets (ponderación K, ventana de 400 ms). Los sintes quedan por debajo de la batería: es la mezcla de partida, con los faders a 0 dB y el equilibrio en el Volume de cada instrumento. La batería se mide por golpe: bombo −13, caja −15, clap −17, rim −20, tom −15, HH cerrado −21, HH abierto −18 y perc −20 dB.

**Prueba de estrés:** `./bin/freon-machine --estres 30 --nucleos 4` toca el peor caso (18 voces de Juno, batería, 4 efectos de envío y todos los efectos moviéndose al azar) y dice cuánto gasta. «Pico de cálculo» es lo que cuesta de verdad; «pico de reloj» incluye las veces que el sistema le quita el procesador. En un Mac sin tiempo real el de reloj sale alto y no significa nada; en la Pi, con el servicio parado (`sudo systemctl stop freon-machine`) y si la prueba dice «Tiempo real: sí», tiene que quedar por debajo del 100 %.

## Arranque lo más corto posible (una vez)

En la Pi, desde la carpeta del proyecto: `./deploy/arranque-rapido.sh`. Freon arranca sin esperar a «sound.target», el arranque es silencioso y sin logo, y se apagan Bluetooth, cloud-init, los discos externos y las tareas automáticas (actualizaciones y mantenimiento) que podrían saltar en mitad de un concierto. Se deshace con `./deploy/arranque-rapido.sh deshacer`. Además, Freon guarda los sonidos de fábrica del sampler y los breaks en `cache/` la primera vez (en la Pi 3 generarlos costaba unos 5 s de cada arranque). Para ver cuánto tarda cada paso: `FREON_TIEMPOS=1 ./bin/freon-machine`.

## Arranque rápido y wifi propia (una vez)

En la Pi, desde la carpeta del proyecto: `./deploy/ajustes-pi.sh "tu-clave"`. Freon deja de esperar a la red al arrancar, y si la Pi no encuentra tu wifi crea la suya, «Freon-Machine», con esa clave: te conectas desde el móvil y abres http://10.42.0.1:8080. Si la Pi dejara de verse en la red, conéctala por cable al router.

Si Freon arranca sin la interfaz enchufada, suena por la salida de auriculares de la Pi (calidad baja, solo de reserva). En cuanto enchufas la interfaz elegida pasa a ella sola en unos segundos; si enchufas otra interfaz USB, pasa a esa mientras no esté la elegida. Las interfaces USB 1.1 (las baratas, como las de chip Burr-Brown) se usan solo como salida: en la Pi 3, con entradas a la vez suenan «crusheadas». Si la interfaz se desconecta en marcha, tras unos segundos pasa a la salida de la Pi y vuelve sola cuando reaparece.

Wifi propia: la crea hostapd (`./deploy/wifi-propia.sh`, una vez). Al encender, si en 90 s no hay wifi conocida, aparece «Freon-Machine»; y si la wifi conocida se pierde durante 1 minuto con la Pi encendida, también (hasta reiniciar). Web: http://10.42.0.1:8080 o http://freonmachine.local:8080. En Ajustes → System → Network eliges: «Home wifi» (la de casa con prioridad; para el desarrollo) u «Own wifi always» (la Pi crea siempre la suya; para el uso normal). Al cambiar de wifi, Freon reabre el audio sola a los pocos segundos: el USB puede desalinear la interfaz y sonar «crusheada».

## Ajustes de sistema, sin terminal

En la pestaña **Settings**, tarjeta **System**, eliges salida y entradas de audio, buffer, frecuencia, núcleos, salidas y qué puertos MIDI se usan. Con **Apply and restart Freon** se guarda y Freon se reinicia sola en unos segundos.

- **Latencia:** el retraso de salida es buffer × Safety blocks. Bájalo poco a poco y mira el contador de cortes (dropouts).
- **Pi 3:** el buffer mínimo es 256. Con menos, los cortes pueden bloquear el USB de la interfaz, y entonces solo se sale quitando la corriente de la Pi.
- Si lo elegido no funciona al arrancar, vuelve sola a lo anterior y lo avisa.
- Si la interfaz no está (por ejemplo, desconectada), usa la de por defecto; y si no hay ninguna, arranca sin audio, para que siempre puedas arreglarlo desde la web.
- Ahí mismo ves temperatura, CPU, pico y cortes, y tienes Restart Freon y Power off.
- Se guarda en `freon-sistema.txt` y manda sobre las opciones de arranque. Si borras ese archivo, vuelven las de arranque.

## En la Raspberry Pi (arranque automático, sin pantalla)

1. Copia la carpeta a la Pi (por ejemplo a `~/freon-machine`) y entra en ella.
2. Instala, con parte del nombre de tu interfaz de audio:

       ./deploy/install-pi.sh "Scarlett"

   Esto instala lo necesario, compila y da permisos de tiempo real. También pone el procesador a máxima
   velocidad y crea el servicio que arranca Freon Machine al encender la Pi. Por último, permite el
   apagado desde el mezclador MIDI.
3. Reinicia la Pi una vez: `sudo reboot`.
4. Para ver lo que dice el programa: `journalctl -u freon-machine -f`.

Si algo falla, el servicio lo vuelve a arrancar en un segundo.

En el Oxi, activa el envío de reloj MIDI por USB para que los delays, el gater, el repeat
y los breaks vayan a tempo.

## Por dentro

- `src/freon-machine.cpp`: el programa (audio, MIDI, estado, servidor, sonidos de serie).
- `src/juno.h`, `src/dx.h`, `src/moog.h`, `src/voice303.h`, `src/gameboy.h`: los
  motores.
- `src/drums.h`: los kits.
- `deploy/`: instalación y servicio de arranque para la Raspberry Pi.
- `src/live.h`: sampler, breaks, granular, voz y lectura y escritura de WAV.
- `src/fx.h`: distorsión, EQ, limitador, sidechain, reverbs, delays y efectos del máster.
- `src/params.h`: los parámetros de cada motor y sus presets de fábrica. La pantalla se
  construye a partir de aquí.
- `src/server.h`: servidor web mínimo para la pantalla.
- `ui/index.html`: la pantalla.
- `samples/` y `breaks/`: tus WAV (16, 24 o 32 bits; mono o estéreo; cualquier
  frecuencia).

Consumo medido en el ordenador de pruebas:

| Situación | Consumo de un núcleo |
|---|---|
| Sintes y batería sonando | 5 % |
| Además, los 16 pads, el break, el granular y la voz a la vez | 3 % más |
| Peor caso de la mezcla | 11 % |

En la Raspberry Pi 5, más o menos el doble.

## Capa de 16 knobs de cada motor

Arriba: Cutoff, Resonance, Env amount, **A, B, C, D**, Volume. Abajo: ADSR de filtro y ADSR de amplitud.
Donde un motor no tiene filtro o envolvente, va el equivalente más parecido.

| Motor | «Cutoff / Reso / Env» | A | B | C | D |
|---|---|---|---|---|---|
| Juno (Juno-6) | Cutoff, Resonance, Env | PWM | Sub | VCF LFO | LFO rate |
| DX7 | Brightness, Mod decay, Feedback | Detune | Vibrato | Patch | Tone |
| Moog | Cutoff, Resonance, Contour | Osc 2 fine | Level 3 | Drive | Glide |
| 303 | Cutoff, Resonance, Env mod | Accent | Drive | Wave | Sub |
| Game Boy | DMG filter, —, — | Vib depth | Vib rate | Glide | Fine |
| Wavetable | Cutoff, Resonance, Env→filter | Position | Env→pos | LFO→pos | Detune |
| Supersaw | Cutoff, Resonance, Env→filter | Detune | Mix | Ensemble | High pass |
| Modal | Brightness, Structure, Exciter level | Position | Damping | Noise | Spread |

En el Juno, la fila de abajo es su única ADSR (filtro y VCA a la vez, como en el Juno-6), más
HPF, ruido, DCO LFO y retardo del LFO.

## Código de otros

- **Open303** (Robin Schmidt): el 303. En `third_party/open303`, con dos arreglos: no reserva memoria al tocar y corrige una escritura fuera de una tabla.
- **KR-106** (Ultramaster / Kayrock, licencia GPL v3): el Juno. En `third_party/kr106`. Los cambios son:
  - el modo Juno-6;
  - el paso alto continuo;
  - las voces calladas no se calculan;
  - una tabla compartida entre los Juno.
- **MSFA** (Google, licencia Apache 2.0): el DX7. En `third_party/msfa`, con estos cambios:
  - avisa cuando una nota ha terminado, para liberar la voz;
  - corrige un fallo de origen: la realimentación arrancaba con valores sin inicializar.
- **Mutable Instruments Plaits** (Emilie Gillet, licencia MIT): bombos y cajas de los kits 808 y 909. En `third_party/mi`, con la parte de stmlib que necesitan.
- **Filtro ladder de Huovilainen:** escrito para Freon a partir de la versión de Victor Lazzarini para Csound.
- **RtAudio y RtMidi:** audio y MIDI.

Por el código GPL del Juno, si algún día se reparte Freon Machine a otras personas, hay que
hacerlo con licencia GPL v3 y con su código. Para uso propio no cambia nada.
