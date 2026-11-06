# Auditoría del groovebox (motor de sonido)

Fecha: 4-11-2026 · Código auditado: `src/*.h`, `src/groovebox.cpp`, `ui/index.html`, `build.sh` (sin `third_party/`).
Leyenda: **[verificado]** = reproducido, medido o leído sin ambigüedad · **[probable]** = deducido del código, sin reproducir.

---

## Resumen ejecutivo

El motor está bien planteado para tocar en directo: el hilo de audio no usa bloqueos, la comunicación con él va por colas sin bloqueo, los samples nunca se liberan mientras el audio puede leerlos y el consumo de CPU es bajo. En el peor caso que he montado (3 pistas × 6 voces, batería, sampler, breaks, granular, voz y efectos de máster) gasta un **10 % de un núcleo** en un Xeon a 2,1 GHz. En la Pi 5 calculo un 20–30 %, así que la CPU no es el problema. Los riesgos están en la robustez, y pesan más ahora que el instrumento va a funcionar sin pantalla:

1. **Si se desenchufa y se vuelve a enchufar el Oxi o una controladora, el MIDI no vuelve** hasta reiniciar el programa. Las notas que estaban sonando se quedan colgadas.
2. **No hay salida MIDI.** Sin pantalla, el músico no recibe ninguna respuesta (LEDs) y muchas funciones solo se pueden usar desde la web: elegir motor o kit, presets, mutes, efectos de máster, encender breaks/granular/voz, etc.
3. **Valores fuera de rango hacen caer o colgar el audio.** Con un mensaje mal formado o un archivo de estado dañado, el hilo de audio se puede quedar colgado (Game Boy con `Arp speed` = 0) o el programa se cierra (`kb 5 …`).
4. **Faders, pan, envíos y mutes del mezclador cambian a saltos.** Producen *zipper* y clics. Además, los motores salen a niveles muy distintos (hasta 17 dB entre el Juno y el Moog).
5. **El servidor web puede bloquear el hilo de control.** Basta con que un móvil se duerma con la página abierta para que deje de guardarse el estado y de buscarse puertos MIDI nuevos. Tampoco limita el tamaño de los mensajes.

---

## Crítico (puede colgar, cortar o perder datos en el escenario)

### C1. El MIDI no se recupera tras desconectar un aparato **[verificado por lectura; el comportamiento exacto de ALSA, probable]**
- **Dónde:** `groovebox.cpp:1051-1068` (`MidiHub::scan`).
- **Qué pasa:** los puertos se identifican por nombre y nunca se eliminan de `ports`. Si el cable USB del Oxi se suelta y se vuelve a conectar, el nombre ya está en la lista (`have = true`) y no se vuelve a abrir. El `RtMidiIn` antiguo queda suscrito a un puerto ALSA/CoreMIDI que ya no existe, así que el instrumento se queda sin MIDI hasta reiniciar. Las notas que sonaban en el momento del tirón no reciben nunca su *note off*.
- **Solución:** en cada `scan`, comparar con la lista real de puertos y cerrar y borrar los que han desaparecido. Al desaparecer el puerto dueño del reloj, mandar un *all notes off* a las pistas de ese puerto y soltar el reloj (`clockOwner = nullptr`). Al reaparecer, reabrir el puerto. En ALSA conviene comparar también el número de cliente:puerto, no solo el nombre.

### C2. Valores sin validar pueden colgar o tirar el hilo de audio **[verificado]**
- **Dónde:** `Control::set` (`groovebox.cpp:856-861`) acepta cualquier número para cualquier clave existente. `load()` (`728-756`) tampoco comprueba rangos. Casos reproducidos con un arnés y ASan/UBSan:
  - `set t2.gb.11 0` (Arp speed = 0, cuando el mínimo de la especificación es 1): `arpPer = 0` y el bucle `while (v.arpT >= arpPer)` de `gameboy.h:91` no termina nunca. **El hilo de audio se queda colgado.** Reproducido: sigue colgado a los 3 s.
  - `kb 5 60 100` (pista 4–9 o negativa): `act[t]` y `held[t]` se salen del array en `groovebox.cpp:210-212`. **Segfault** reproducido. El índice `t` de `kb` no se comprueba en ningún sitio (`877`, `250`).
  - `voz.12 = 9` (Harmony): lectura fuera de `H[6][2]` en `voice.h:193` (UBSan).
  - `voz.0` negativo da `in[-1]` en `voice.h:68`. Muchos parámetros de tipo «lista» se convierten a `int` y se usan como índice sin `clamp`.
- **Por qué importa:** la interfaz web solo manda valores válidos, pero un `groovebox-estado.json` dañado, una versión futura con otros rangos o un mensaje escrito a mano se cargan **al arrancar**. Sin pantalla, eso puede acabar en un bucle de reinicios.
- **Solución:** un único punto de validación. En `Control::set` y en `load()`, buscar la `PSpec` de la clave y aplicar `clamp(v, min, max)` (y redondear si `step` es 1). Además, programación defensiva en los motores: `clamp` en `kb`, en `H[h]` y en `arpPer = max(arpPer, 1/60)`.

### C3. El guardado del estado puede dejar un archivo vacío o a medias **[verificado por lectura]**
- **Dónde:** `groovebox.cpp:801-804`.
- **Qué pasa:** se escribe `estado.json.tmp` sin comprobar errores y sin `fsync`, y después se hace `rename` siempre. Si la escritura falla (tarjeta SD llena o con errores), el `.tmp` truncado sustituye al bueno. En la Pi, si se corta la corriente justo después de guardar (algo habitual al desenchufar al acabar el bolo), el archivo puede quedar a cero bytes. Además se guarda a los 0,8 s de cada cambio, es decir, continuamente mientras se tocan knobs. Si `load()` encuentra un JSON roto, carga lo que puede sin avisar.
- **Solución:** comprobar `f.good()` tras escribir; `fflush` + `fsync(fd)` del archivo y del directorio antes de `rename`; guardar una copia `estado.json.bak` rotativa. Al arrancar, si el JSON no se puede analizar entero, usar el `.bak` y avisar (un LED o un parpadeo por MIDI).

### C4. Un cliente web lento bloquea el hilo de control **[probable]**
- **Dónde:** `server.h:103-114` (`rawSend` con sockets bloqueantes); `groovebox.cpp:912-927` (los medidores se mandan cada 60 ms a todos los clientes).
- **Qué pasa:** si un móvil o tablet deja de leer (pantalla bloqueada, wifi que se cae sin cerrar TCP), se llena el búfer del socket y `send()` se bloquea, a veces durante minutos, hasta que TCP da la conexión por perdida. Mientras tanto el hilo de control está parado: no se guarda el estado, no se buscan puertos MIDI nuevos, no se termina una grabación al sampler y se pierden los *feedback* de CC (cola `fb` llena). El audio y el MIDI siguen funcionando porque van por otra ruta.
- **Solución:** sockets en modo `O_NONBLOCK`, una cola de salida por cliente y desconectar al cliente cuya cola supere, por ejemplo, 256 KB. Para los medidores, mandar solo el último estado (si el anterior no ha salido, no se encola otro).

### C5. Sin recuperación si se cae la interfaz de audio **[verificado por lectura]**
- **Dónde:** `groovebox.cpp:1133-1147`. Ni se registra una función de error de RtAudio ni se mira el `RtAudioStreamStatus` del callback.
- **Qué pasa:** si la interfaz USB se desconecta o falla, el stream se detiene y el programa sigue vivo pero mudo, sin dar ninguna señal. Sin pantalla, nadie se entera.
- **Solución:** un vigilante en el hilo de control que compruebe que `process()` sigue llamándose (un contador atómico). Si pasan más de 500 ms sin llamadas, cerrar y reabrir el stream, o salir con un código de error para que systemd reinicie el programa (ver «Funcionamiento sin pantalla»). Contar también los xruns (`status & RTAUDIO_OUTPUT_UNDERFLOW`) para saber si la Pi va justa.

---

## Importante

### I1. Reservas de memoria en el hilo de audio **[verificado]**
- `held[t].push_back` (`groovebox.cpp:212`), `Granular::g_.push_back` (`live.h:338`), `Granular::notes_` (`live.h:307`) y `VoiceFx::notes_` (`voice.h:51`, que además hace `erase` del principio).
- Solo reservan memoria las primeras veces (luego la capacidad ya basta), pero esas primeras veces son justo al empezar a tocar. `malloc` puede bloquearse si otro hilo (control, RtMidi) está pidiendo memoria a la vez.
- **Solución:** `reserve(128)` / `reserve(kMaxG)` en `prepare()`, o mejor arrays fijos con contador, como ya hacen Moog y 303 con `stack_[16]`.

### I2. *Zipper* y clics en el mezclador y el máster **[verificado por lectura]**
- **Dónde:** `stageB` en `groovebox.cpp:355-369` (nivel, pan y envíos), `373` (mute/solo instantáneos), `398-399` (volumen máster).
- **Qué pasa:** los parámetros cambian de golpe al principio de cada bloque. La web los manda a 60 Hz y el MIDI a 128 pasos, así que un fader movido sobre un pad sostenido suena «a escalones». Un mute en mitad de un bombo hace clic.
- **Solución:** rampa lineal por bloque (de `prev` a `nuevo` a lo largo de las 128 muestras) para nivel, pan, envíos y volumen máster. Para mute y solo, una rampa de 5–10 ms. Son unas 20 líneas.

### I3. Niveles muy distintos entre motores **[verificado, medido]**
Con valores por defecto, nota 48 y velocidad 100, salida del motor antes del mezclador:

| Motor | RMS 1 nota | RMS acorde de 3 |
|---|---|---|
| Juno | −31,6 dBFS | −26,1 |
| Supersaw | −27,0 | −22,0 |
| DX / Wavetable | −23,3 / −23,6 | −18,8 |
| Game Boy | −21,4 | (arpegio) |
| West Coast | −20,0 | −15,7 |
| Órgano | −18,4 | −13,8 |
| 303 | −16,6 | mono |
| Moog | −14,2 | mono |

- El Juno queda unos **17 dB por debajo del Moog**. Al cambiar de motor en directo, el nivel salta.
- **Solución:** un `trim` fijo por motor (en `MeloEngines::render`, `params.h:218-231`) para dejarlos todos hacia −18 dBFS RMS con una nota. Hay que medirlo una vez y fijarlo en la especificación.

### I4. Granular: lectura fuera del búfer con notas agudas **[verificado por lectura]**
- **Dónde:** `live.h:333-334, 345-346`.
- **Qué pasa:** en modo «MIDI notes», `rate = 2^((Tune + nota−60)/12)` puede llegar a ~190 (Tune +24 y nota 127). Entonces `back = … + size*rate` supera `4·N_`, `fmod` da un valor negativo y la primera muestra de cada grano se lee en `ring_[(size_t)negativo]`, 700 KB antes del búfer. ASan no lo detecta porque cae en otra zona de memoria válida. Si esa basura es NaN o un número enorme, entra en la realimentación del granular y puede contaminar el máster.
- **Solución:** `g.pos = fmod(...); if (g.pos < 0) g.pos += N_;` y limitar `rate` (por ejemplo a 8).

### I5. Sampler: lectura fuera del sample con bucles muy cortos y transposiciones altas **[probable]**
- **Dónde:** `live.h:174-182`.
- **Qué pasa:** en modo *Loop*, si `inc > b−a` (bucle de 64 muestras con transposición de +4 octavas), tras restar `b−a` la posición sigue siendo ≥ `b` y `ix` puede pasar de `N−1`. `ix2` está limitado, pero `ix` no.
- **Solución:** ajustar la posición con módulo (`while (pos >= b) pos -= (b−a)`) y `ix = min(ix, N−1)`.

### I6. Retrigger de batería y robo de voces sin fundido **[verificado por lectura]**
- `DrumVoice::trigger` (`drums.h:107-108`) reinicia fase y envolvente de golpe. Un bombo largo (decay alto) que se redispara corta su cola en seco y hace clic. Lo mismo pasa con el robo de voz del Sampler (`live.h:144, 149`) y de los Breaks (`v_[0]` en `live.h:289`).
- **Solución:** dos voces por carril (la saliente se apaga en 3–5 ms) o un mini fundido de salida antes de reiniciar.

### I7. El reloj MIDI adelanta un *tick* tras Start **[probable]**
- **Dónde:** `groovebox.cpp:258-259`.
- **Qué pasa:** tras `0xFA`, `ticks = 0`. El primer `0xF8`, que según la norma MIDI es la posición 0, lo sube a 1, así que `pos = 1/6`. Breaks, gater y repeat van **un tick (~21 ms a 120 BPM) por delante** del Oxi. Tampoco se atiende *Song Position Pointer* (`0xF2`) para *Continue*.
- **Solución:** contar desde −1 tras Start (o incrementar después de usar el valor) y procesar `0xF2`.

### I8. *Jitter* de MIDI de hasta un búfer **[verificado por lectura]**
- **Dónde:** `groovebox.cpp:301-303`. Todas las notas y CC de un callback se aplican al principio del bloque.
- Con 128 muestras son hasta **2,7 ms de *jitter***, apreciable en *flams* y hats rápidos. Las marcas de tiempo de RtMidi no se usan.
- **Solución (media):** guardar en la cola `Midi` el instante de llegada (reloj monotónico), traducirlo a desfase de muestra dentro del bloque y partir `chunk()` en ese punto. Ya existe el bucle en trozos de `BLK`, así que se puede reutilizar.

### I9. Accesos a la vez desde el hilo de control y el de audio sin `atomic` **[verificado por lectura]**
- `au.bpm` y `au.sinceTick` se leen desde el hilo de control (`groovebox.cpp:920`) mientras el audio los escribe (`312`, `310`). En ARM64 no producirá valores corruptos, pero técnicamente es comportamiento indefinido y un compilador agresivo podría cachearlos.
- **Solución:** `std::atomic<float>` y `std::atomic<long>` con `memory_order_relaxed`.

### I10. Prioridad de tiempo real mínima **[verificado por lectura]**
- `opts.flags = RTAUDIO_SCHEDULE_REALTIME` pero no se fija `opts.priority` (`groovebox.cpp:1132`). RtAudio la ajusta al mínimo de `SCHED_RR`, que es **1**, por debajo de los hilos de interrupción de USB (50) y de casi todo lo que funciona en tiempo real.
- **Solución:** `opts.priority = 80`, más los permisos `rtprio`/`memlock` del usuario (ver «Build y despliegue»).

### I11. Servidor web: sin límites ni comprobación de origen **[verificado por lectura]**
- `server.h:90`: el búfer de entrada `c.in` crece sin límite. Una cabecera HTTP sin `\r\n\r\n` o un marco WebSocket que anuncie 2⁶³ bytes acumula memoria hasta agotarla.
- `server.h:159`: `pos + len` puede desbordarse con `len` de 64 bits.
- `server.h:165`: la respuesta a un *ping* de más de 125 bytes rompe el formato del marco.
- No se comprueba la cabecera `Origin`: **cualquier web abierta en el mismo portátil** puede conectarse a `ws://localhost:8080` y mandar órdenes (*cross-site WebSocket hijacking*). Con `--host 0.0.0.0`, cualquiera en la wifi del local.
- `sset samp.pN.file ../../loquesea.wav` (`groovebox.cpp:632-637, 599-602`) carga cualquier WAV del disco: recorrido de rutas, de impacto bajo.
- **Solución:** máximo de 64 KB por mensaje y 16 KB de cabecera (si se supera, cerrar la conexión); aceptar solo `Origin` del propio host; rechazar nombres de archivo con `/` o `..`. Si se abre a la red, poner una contraseña sencilla en la URL.

### I12. Cambiar el tipo de envío vacía 2 MB dentro del audio **[verificado, medido]**
- `K_FX` con `b == 5` llama a `SendFx::clear()` (`groovebox.cpp:241`, `fx.h:194`): ~530 000 floats puestos a cero dentro del callback. Medido: **47–257 µs** en el bloque del cambio, frente a 2 667 µs de presupuesto. No llega a provocar un corte, pero es un pico evitable y en la Pi será 2–3 veces mayor.
- **Solución:** fundido de salida del retorno y vaciado repartido en varios bloques, o vaciar solo la parte que usa el nuevo tipo.

### I13. Cambio de motor: las voces del motor anterior «resucitan» **[verificado por lectura]**
- `MeloEngines::select` (`params.h:217`) hace `allOff()` (solo un *gate off*) y deja de renderizar ese motor. Su cola de release se corta en seco (clic) y, si se vuelve a ese motor más tarde, las voces siguen su release donde se quedaron (cola fantasma).
- **Relación con MEJORAS («que se corten en seco las notas»):** además del corte, hay que **reiniciar de verdad** las voces y envolventes del motor saliente (un `reset()`), con un fundido de 5 ms del motor saliente.

---

## Mejora

- **M1. Denormales en ARM [probable, impacto bajo].** `_MM_SET_FLUSH_ZERO_MODE` (`groovebox.cpp:1077-1079`) solo actúa en x86. En la Pi y en un Mac con Apple Silicon no se activa nada, y en macOS el hilo de CoreAudio no lo hereda. Las colas de las reverbs FDN y los IIR que tienden a cero producen denormales. **Solución:** al empezar `process()`, una vez por hilo, en aarch64 `asm volatile("msr fpcr, %0" :: "r"(fpcr | (1<<24)))`.
- **M2. Distorsión con aliasing.** `Dist` (`fx.h:64`) sobremuestrea a 2× con interpolación lineal y Butterworth de orden 4. Con Fuzz y drive alto (ganancia ×49), el aliasing es audible en agudos. **Solución:** 4× con un filtro de media banda polifásico, o ADAA (*antiderivative antialiasing*) para `softClip`/`tanh`.
- **M3. Limitador sin lookahead.** Ataque de 2 ms (`fx.h:87-100`): los transitorios pasan y los recorta el `softClip` final (`groovebox.cpp:402`). **Solución:** 1–2 ms de lookahead en el limitador del máster.
- **M4. Filtro DJ.** Al cruzar de LP a HP se reutiliza el estado viejo del otro biquad (`fx.h:126-128`), lo que puede dar un clic. **Solución:** poner a cero el estado al cambiar de rama o hacer un fundido entre las dos.
- **M5. Repeat del máster.** El fundido en los bordes del bucle tiene un suelo de 0,3 (`fx.h:304-305`), no llega a cero y deja un clic en cada vuelta. **Solución:** `fade` de 0 a 1. Ya está en MEJORAS el rediseño del Repeat; aprovechar ese cambio.
- **M6. Tape/dub delay.** El tiempo de retardo salta al cambiar de división o de BPM (`fx.h:212, 217`). En un tape delay lo esperable es un deslizamiento de tono. **Solución:** suavizar `d0` (con un Smoother de unos 200 ms).
- **M7. Sin *soft takeover* ni CC relativos.** `fromCC` (`params.h:201`) es absoluto. Tras cargar un preset, el primer giro de un potenciómetro hace saltar el valor. Ver «Funcionamiento sin pantalla».
- **M8. Cola de órdenes llena sin aviso.** `cmds.push` ignora el `false` (`groovebox.cpp:858, 863`). `pushAll` manda 1 493 órdenes de golpe; la cola tiene 16 384, así que hoy cabe, pero conviene contar los descartes.
- **M9. `loadWav` lee el archivo entero en memoria** antes de recortarlo a 90 s (`live.h:26-27`). Un WAV de 2 GB en `samples/` agota la RAM de la Pi. **Solución:** leer solo las cabeceras y la parte necesaria del chunk `data`, y rechazar archivos de más de 200 MB.
- **M10. `std::system("mkdir -p '...'")`** (`groovebox.cpp:646`) se rompe si la ruta tiene comillas. **Solución:** `std::filesystem::create_directories`.
- **M11. Carpeta base deducida de `argv[0]`** (`groovebox.cpp:1091`). Si se lanza desde el `PATH` (sin barra), apunta a `./..` desde la carpeta actual. **Solución:** `readlink("/proc/self/exe")` en Linux y `_NSGetExecutablePath` en macOS.

### Arquitectura y mantenimiento

- **Índices mágicos repetidos.** `MeloEngines::set` (`params.h:232-291`) traduce «índice de knob → campo» con `case` numéricos que deben coincidir con el orden de `kEng[].p[]`. Lo mismo ocurre con `K_LANE` (`groovebox.cpp:245-246`, índices 0–10 frente a `kLaneKeys`), `K_FX` (`b == 5/6`), `kOnIdx = 30` / `kFrzIdx = 31` / `kBrkHold0 = 20`, que comparten espacio con los parámetros, y con los valores por defecto del máster duplicados (`groovebox.cpp:141` y `717`). Reordenar un parámetro rompe el estado guardado, como ya pasó en la versión 3, que tuvo que descartar los parámetros de los sintes.
  - **Propuesta:** que cada `PSpec` lleve un **identificador estable de texto** (`"cutoff"`) y un puntero al campo, o una lambda `set`. El estado se guarda por identificador, no por posición. Así añadir un motor o parámetro es tocar un solo sitio, y la capa de 16 knobs de MEJORAS se puede describir como una tabla de identificadores.
- **Funciones enormes y líneas de más de 300 caracteres** (`chunk` tiene unas 140 líneas, `handleMidi`, `toCmd`, `defaults`). Son difíciles de revisar y de comparar en un diff. Partir `chunk` en `renderSources / mixChannels / sends / master / outputs / record`.
- **Añadir un motor** obliga a tocar 6 sitios: enum, `kEng`, `factoryPresets` y los switch de `MeloEngines` (prepare, noteOn, noteOff, allOff, render, set). Lo bueno es que la interfaz web se construye sola a partir de la especificación. Una interfaz común (`struct Engine { virtual … }` o un `std::variant`) lo reduciría a dos sitios.
- **Pruebas.** El código compila limpio con `-Wall -Wextra` (solo hay avisos de `-Wconversion`). Falta una batería mínima: `--render` comparado con un WAV de referencia, una compilación con ASan/UBSan y un *fuzzer* de mensajes WebSocket y de `estado.json`. Los arneses que he usado en esta auditoría sirven como punto de partida (ver anexo).

### Interfaz web (prioridad baja: ahora es solo para configurar)

- `build()` reconstruye **todo el DOM** (las 4 pestañas y 15 tiras del mezclador) al cambiar motor, kit, tipo de efecto o fuente del sub, incluido cuando el cambio llega de otro cliente (`index.html:365, 369-373`). Si alguien está arrastrando un knob en ese momento, pierde el arrastre.
- **Fuga en `draws`:** `buildLive()` y `buildMix()` se llaman por separado (al cambiar de sample, al cargar un snapshot) y vuelven a registrar sus funciones sin quitar las viejas (`index.html:362, 473-475, 674-675`). En una sesión larga, cada mensaje `set` ejecuta cada vez más funciones sobre elementos que ya no están en la página.
- `send()` actualiza `ST` aunque el socket esté cerrado, así que la pantalla muestra valores que el motor no tiene hasta reconectar. No hay *ping* de vida: si el motor se cuelga, la página sigue diciendo «Engine connected».

---

## Funcionamiento sin pantalla

### (a) Qué solo se puede hacer hoy desde la web

Lo que **sí** llega por MIDI (`handleMidi`, `groovebox.cpp:257-295`):
- Notas en las 4 pistas.
- En las pistas de sinte: CC 16–31 y 40–55 → knobs del motor, en el orden de la pantalla. CC 102–109 → botones del motor. CC 120/123 → silencia esa pista.
- Sampler, breaks, granular y voz: notas, y sus knobs por CC 16–31/40–55. En los breaks, además, los *holds* (CC 102–109 y notas 60–67) y el salto a corte (notas 36–51).

**Sin camino MIDI** (solo desde la web):

| Área | Funciones que faltan | Dónde se ve |
|---|---|---|
| Batería | **Todo excepto las notas.** Los CC de la pista 1 se ignoran (`if (t == 0) continue`): tune, decay, tone, level, sweep, snap, drive, pan y mute por sonido; **kit** | `groovebox.cpp:283` |
| Sintes | **Cambio de motor**, **cargar y guardar preset** (no se atiende *Program Change*) | — |
| Sampler | **Elegir pad** (`samp.sel`): los CC solo editan el pad seleccionado, y no se puede cambiar cuál es. **Grabar** (rec on/off, fuente). Asignar archivo a un pad | `liveSet`, `pad < 0` solo llega desde la web |
| Breaks / Granular / Voz | **Encender y apagar** (`brk.on`, `gran.on`, `voz.on`), **Freeze** del granular, elegir el loop | `kOnIdx = 30` queda fuera de `np` (`groovebox.cpp:272`) |
| Mezclador | **Nada:** niveles, **mutes, solos**, cue, pan, EQ, distorsión, sidechain, envíos, grupos, retornos, tipo y parámetros de los efectos | — |
| Máster | **Nada:** volumen, limitador, filtro DJ, distorsión, **repeat, gater, riser, space, throw, tape stop** | — |
| Global | **Snapshots** de mezcla, **resets**, **pánico global**, tempo interno y elección de fuente de reloj, configuración de canales MIDI y notas de batería, parámetros del Sub | — |

**Propuesta de mapa (para discutir):**
- **Program Change** en el canal de cada pista: 0–9 elige motor y 10–17 carga los presets 1–8. En la pista 1, 0–3 eligen kit.
- **Un canal «Mezcla/Máster»** dedicado (por ejemplo el 16):
  - CC de nivel por canal.
  - Notas o CC como interruptores de mute/solo.
  - Un bloque de CC para los efectos de máster, que ya funcionan como «cantidad de 0 a 1» y por tanto encajan con un fader o un encoder.
  - Notas para snapshots 1–8 y CC para encender breaks, granular y voz.
- **Batería:** CC por sonido (8 sonidos × 8 parámetros caben en dos páginas de 32), siguiendo lo que ya está en MEJORAS para los parámetros por sonido.
- **Pad del sampler:** una nota de un rango reservado que seleccione sin disparar, o que el último pad tocado quede seleccionado (opción configurable).
- **Pánico global:** CC 123 en el canal de mezcla, o una combinación de botones.
- **Guardar preset:** una combinación con «shift» (mantener un botón y pulsar el hueco) para evitar guardados accidentales.

### (b) Arranque y robustez sin pantalla en la Raspberry Pi

1. **Arranque automático.** Un servicio systemd de usuario o de sistema con:
   - `ExecStart=/home/jorge/groovebox/bin/groovebox --audio-name "…" --host 127.0.0.1` (ruta absoluta; ver M11).
   - `Restart=always`, `RestartSec=1`.
   - `LimitRTPRIO=95`, `LimitMEMLOCK=infinity`, `Nice=-10`.
   - `After=sound.target`.
2. **Elegir la interfaz de audio por nombre.** `--audio 130` es un identificador de RtAudio 6 que depende del orden en que se enumeran los dispositivos (`groovebox.cpp:1117`); tras un reinicio con otro orden USB puede cambiar. Añadir `--audio-name <texto>` y buscar por subcadena.
3. **No morir si la interfaz aún no está.** Hoy, si no hay dispositivo, el programa sale (`1115`, `1139`). Con `Restart=always` se reintenta, pero es más limpio esperar y reintentar dentro del programa cada 2 s. Ver también C5 para la caída a mitad de concierto.
4. **Controladoras que se desconectan:** C1 (reabrir puertos) y soltar las notas colgadas de ese puerto.
5. **Apagado seguro.** `SIGTERM` ya guarda el estado (`1153`, `942`). Falta una forma de apagar la Pi sin pantalla:
   - Una combinación MIDI que llame a `systemctl poweroff` (con confirmación: mantener 3 s).
   - O un botón GPIO con `gpio-shutdown` en `config.txt`.
   - Junto con C3 (`fsync` + `.bak`), un corte de corriente pierde como mucho los últimos cambios y nunca el archivo.
   - Valorar el sistema de archivos raíz en solo lectura (overlayfs de `raspi-config`), con el estado y los samples en una partición aparte.
6. **Tiempo real en la Pi.** Ver «Build y despliegue»: `performance` governor, `rtprio`, `mlockall`, aislar un núcleo.
7. **Servidor web.** Arrancar en `127.0.0.1` y solo con `--host 0.0.0.0` cuando se vaya a editar desde el móvil. Ver C4 e I11.

### (c) Qué ve el músico sin pantalla

Hoy **no ve nada**: no hay ningún `RtMidiOut` en el código. La cola `fb` (`groovebox.cpp:906-910`) solo alimenta a la web.

**Sobre MEJORAS «Devolver el MIDI a las controladoras»:** pasa a ser **la prioridad nº 1**. Además de lo que ya está anotado (valores al cargar preset, snapshot, motor, kit o reset; sin eco al knob que originó el cambio), recomiendo:
- **Estado, no solo valores:**
  - LED de mute/solo por canal.
  - Motor y preset activos (un LED del 1 al 10, o el anillo de un encoder en modo «posición»).
  - Hueco de snapshot activo.
  - Breaks, granular y voz encendidos.
  - **Grabando** (LED parpadeando).
  - **Reloj del Oxi enganchado / perdido**.
  - **Alarmas:** clip en el máster, CPU > 70 %, xruns, puerto MIDI perdido o recuperado, error al cargar el estado.
- **Encoders relativos** (modo de dos complementos / *binary offset* del Launch Control XL3 o similares). Con encoders sin fin y anillo de LEDs desaparece el problema del salto de valor. Para potenciómetros y faders sin motor, implementar *soft takeover* (el valor no cambia hasta que el control «pasa» por el valor actual).
- **Resolución:** 7 bits dan pasos de ~0,9 semitonos en el cutoff logarítmico; los motores lo suavizan, pero el mezclador no. Valorar NRPN o CC de 14 bits para cutoff y niveles.
- **Ritmo de envío:** agrupar y limitar a ~100 mensajes/s por controladora (las de USB-MIDI clase compatible aguantan bien, pero un DIN a 31 250 baudios no).
- Al conectar una controladora (C1), **mandar el estado completo** de su página.

---

## Eficiencia

**Cómo se ha medido:** arnés propio que incluye `groovebox.cpp`, bloques de 128 muestras a 48 kHz, Xeon @ 2,1 GHz (VM, ruido de ±15 % entre ejecuciones), `-O2` como en `build.sh`. **No se ha medido en la Pi 5.** Para un Cortex-A76 @ 2,4 GHz con libm estimo entre 1,5 y 2,5 veces estas cifras.

| Escenario | % de un núcleo |
|---|---|
| `--render` del proyecto | 4,3 % |
| Reposo con valores por defecto (sin notas; la batería y los efectos «duermen») | 1,9 % |
| Juno, 6 notas | ~3,3 % (solo el motor) |
| DX, 6 notas | ~2,3 % |
| Moog / 303 / GB / WT / órgano | 1,2–1,6 % |
| Supersaw, 6 notas | ~1,6 % |
| West Coast, 6 notas | ~3,9 % |
| **Modal, 6 notas** | **~7,7 %** (el más caro) |
| **Peor caso:** Supersaw + Juno + Wavetable × 6 voces, batería, sampler 8 voces, breaks, granular, voz en armonía, space, throw y 4 envíos | **10,2 %** (vocoder: 9,6 %) |
| Por bloque en el peor caso | mediana 258–410 µs, p99 332–707 µs, máximo 772–4 674 µs (pico aislado, probablemente la VM). Presupuesto: 2 667 µs |

**Conclusión:** hay mucho margen. En la Pi 5 el peor caso quedaría en torno al 20–30 % de un núcleo. Lo importante en directo no es la media sino los picos: **prioridad RT (I10), `mlockall`, governor `performance`** y evitar trabajo puntual dentro del audio (I12).

**Puntos calientes** (callgrind del peor caso):
- `DrumVoice::process`: 13 % de las instrucciones.
- Juno: 15 %.
- `expf`: 7 %, `exp2f`: 3 %, `powf`: 3 %, `tanf`: 3 %.

**Optimizaciones y ganancia esperada:**

| # | Optimización | Ganancia esperada |
|---|---|---|
| E1 | **Modal:** `std::pow(10, …)` por muestra en `synths2.h:325` (cuerda) y **por modo y por muestra** en `336` (8 modos × 6 voces). Es constante dentro del bloque: calcular una vez por bloque. `tanh` por muestra (`342`) → `softClip`. Callgrind: `powf` + `tanh` = 46 % del motor | El Modal baja de ~7,7 % a ~2,5 %. Con `-ffast-math` medí 1,6 %, lo que confirma que es casi todo libm |
| E2 | **Batería:** varios `std::exp(-t/τ)` por muestra y voz (`drums.h:124, 126, 132, 134, 151`) y el `std::max` de 7 valores para `life`, también por muestra (`173`). Sustituir por envolventes recursivas (`e *= k`, con `k` calculado en `trigger`) y calcular `life` una sola vez | DrumVoice −50–60 % (≈ −6 % del total del peor caso) |
| E3 | **Juno:** por voz y muestra hace `mtof` (exp2), `pow(f/261.6, kbd)`, `exp2(envMod…)`, `tan` dentro de `Ladder::process` (45 ns/llamada medidos) y 2 `exp` dentro de `Env::process` → `coef()` (`dsp.h:51`). Calcular `mtof` y `pow(kbd)` al empezar la nota o por bloque, actualizar el corte cada 8–16 muestras (como ya hacen Wavetable y Supersaw) y cachear los coeficientes de `Env` por bloque | Juno −40–50 % |
| E4 | **Moog:** igual que el Juno, pero el ladder funciona a 2× (2 `tan` por muestra) | Moog −30 % |
| E5 | **Voz:** `vsib_.set()` (cos + sin) **en cada muestra** dentro del vocoder (`voice.h:235`); `fx1_.set()` por muestra en megáfono y radio (`242-243`); `log10` + `pow` por muestra en el compresor (`74-75`). Pasar los `set` a `prepare`/por bloque; compresor en dB con una aproximación rápida o calculando la ganancia cada 4 muestras | Voz −20–30 % |
| E6 | **`DelayLine::read`:** dos `%` enteros por lectura (`fx.h:150`); el FDN hace 10 lecturas por muestra. Tamaños potencia de 2 con máscara | FDN −15–25 % (hay hasta 6 FDN: 4 envíos, space, voz) |
| E7 | **Indicadores de compilación** (ver la siguiente sección): `-O3 -fno-math-errno` midió −2–15 % según el motor | Gratis |
| E8 | **`--render`** no cubre los módulos de directo ni el peor caso. Añadir el escenario «peor caso» del anexo para vigilar regresiones | — |

---

## Build y despliegue

- `build.sh` usa `-O2` sin ajuste de CPU. Para la Pi 5 conviene `-O3 -mcpu=cortex-a76 -fno-math-errno` (seguro). `-ffast-math` acelera mucho el Modal y activa *flush-to-zero*, pero cambia el tratamiento de NaN/Inf; solo merece la pena tras E1, y mejor aplicado a archivos concretos. LTO no aporta nada: el código del motor es una sola unidad de compilación.
- Añadir `-g` (los símbolos no afectan a la velocidad) para poder analizar un *core dump* de un cuelgue en un bolo.
- Una variante `./build.sh debug` con `-O1 -g -fsanitize=address,undefined` para probar.
- **Linux en tiempo real:**
  - `opts.priority = 80` (I10).
  - `/etc/security/limits.d/audio.conf` con `@audio - rtprio 95` y `@audio - memlock unlimited`; el usuario en el grupo `audio`.
  - `mlockall(MCL_CURRENT | MCL_FUTURE)` al arrancar.
  - Governor `performance`: `cpufreq` o `force_turbo=1` según el modelo.
  - Desactivar el ahorro de energía de la wifi si se usa la web.
  - Valorar el kernel `PREEMPT_RT` de Raspberry Pi OS y `isolcpus=3` con afinidad del hilo de audio.
- **ALSA:** abrir el dispositivo `hw:` directamente, sin PulseAudio/PipeWire si no se usan; 128 frames × 2 periodos.

---

## Victorias rápidas (≤ 1 hora cada una)

1. `clamp` del índice de pista en `kb` y `arpPer = max(arpPer, 1/60)` en Game Boy (C2, cierra los dos cuelgues reproducidos).
2. Validar rangos en `Control::set`/`load` con la `PSpec` de cada clave (C2).
3. `fsync` + comprobación de errores + `.bak` en `save()` (C3).
4. `opts.priority = 80` y `mlockall` (I10).
5. `reserve()` de `held`, `g_` y `notes_` en `prepare` (I1).
6. Rampa por bloque para nivel, pan, envíos y volumen máster, y rampa corta para mute/solo (I2).
7. `if (g.pos < 0) g.pos += N_` y límite de `rate` en el granular (I4).
8. *Flush-to-zero* en ARM al empezar `process()` (M1).
9. Modal: `pow` por bloque y `softClip` en lugar de `tanh` (E1).
10. Límite de tamaño de mensaje y comprobación de `Origin` en `server.h` (I11).
11. `-O3 -mcpu=cortex-a76 -fno-math-errno -g` en `build.sh` para Linux/aarch64.
12. Trims de nivel por motor (I3), con la tabla de esta auditoría como base.

---

## Comentarios sobre MEJORAS.md

- **Devolver el MIDI a las controladoras:** de acuerdo, y sube al primer puesto ahora que no hay pantalla. Ver «Funcionamiento sin pantalla (c)»: añadir LEDs de estado y alarmas, encoders relativos y *soft takeover*.
- **Capa de 16 knobs igual en todos los sintes:** muy buena idea para tocar sin pantalla. Conviene implementarla como **tabla de identificadores de parámetro** (ver Arquitectura) y no con más índices numéricos. Cambiarla obligará a subir la versión del estado.
- **Al cambiar de motor, cortar las notas:** ver I13. Además del corte, reiniciar las voces del motor saliente, porque si no reaparecen al volver a él.
- **Batería (parámetros por sonido, kit FM, velocidad):** hoy la pista de batería no acepta CC (`groovebox.cpp:283`). Al rehacer los parámetros, darles un mapa CC desde el principio. También hace falta subir la versión del estado.
- **Repeat:** al rehacerlo, corregir también el fundido con suelo 0,3 (M5) y el tick de adelanto del reloj (I7), que se notará más en el repeat.
- **Kit DRM:** confirmado que ya no aparece: `kKitMap` (`groovebox.cpp:80`) y `specJson` solo listan 4 kits, y el estado anterior a la versión 6 descarta `d.kit`. Lo más probable es que siguiera abierto el programa anterior.

---

## Anexo: arneses usados (fuera del proyecto)

En `/tmp/claude-0/-home-claude/4031f99e-7e25-51ca-975b-5d50a25ddd35/scratchpad/audit/`:
- `bench.cpp`: CPU por motor y en el peor caso.
- `wc2.cpp`: distribución del tiempo por bloque.
- `crash.cpp` y `c2.cpp`: reproducciones de C2, I12 y el granular, con ASan/UBSan.
- `lvl.cpp`: niveles por motor (I3).
- `st.cpp`: tamaño del estado. 3 359 claves y 56 KB; con 8 snapshots, 6 887 claves y 117 KB; arranque de `initLive` en 471 ms.
- `modal.cpp` / `wc.cpp`: perfiles callgrind.

Se compilan con `g++ -std=c++17 -O2 -I<proyecto>/src -I<proyecto>/third_party -D__LINUX_ALSA__ X.cpp <proyecto>/third_party/RtAudio.cpp <proyecto>/third_party/RtMidi.cpp -lasound -lpthread`.
