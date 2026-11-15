# Evaluación de Freon Machine · noviembre de 2026

Fecha: 14-11-2026. Versión evaluada: commit `38fa020` (estado v16, «mezcla de Jorge como punto de partida»).
No se ha cambiado código ni nada en la Pi: este informe solo diagnostica y propone.

**Qué se ha hecho**
- **En el Mac** (Apple Silicon): compilación normal, debug (ASan + UBSan) y tsan; `--render` con 1 y 4 núcleos, `--niveles` y `--estres`.
- **Banco de medidas propio**, fuera del repo. Incluye el motor tal cual y mide:
  - el grave a la salida, etapa por etapa;
  - el coste por motor y por efecto;
  - el coste de cargar una memoria.

  El código está en la carpeta de notas compartida (`banco-medidas-grave.cpp`), por si se quiere repetir.
- **La web** abierta con `--sin-audio` (usando una copia del estado de la Pi), a 375 × 812 (móvil) y 800 × 480 (pantalla de 7").
- **La Pi 5 estaba accesible**: solo se ha leído (procesos, tensión, temperatura, tarjetas de audio, interrupciones, registro, estado y memorias). Freon no se ha reiniciado ni se ha tocado.

**Cómo leer cada hallazgo**
- **Medido:** lo he visto ocurrir, en el Mac, en la Pi o con el banco de medidas.
- **Código:** se deduce sin dudas leyendo las líneas citadas, pero no lo he visto ocurrir.
- **Sospecha:** depende de algo que no he podido comprobar.

**Gravedad:** crítica, importante o menor. **Esfuerzo del arreglo:** mínimo (una línea), bajo (una tarde), medio (un día con pruebas) o alto (varios días).

Lo que ya estaba resuelto de `AUDITORIA.md` no se repite. Al final del punto 3 hay una tabla con su estado actual.

---

## Resumen

**Lo más importante, en una frase cada cosa**

1. **El grave no lo quita ningún filtro escondido; lo quita el compresor de bus.** La cadena del máster es plana de 30 a 1000 Hz (medido). Pero el compresor tiene un detector muy rápido que «ve» sobre todo el bombo, y a amount 1 y 10:1 baja el grave 4,4 dB respecto a los medios. Con los ajustes del 13-11 (4:1, Makeup +6 dB y Air 0,75) llega a 8 dB. Hay tres arreglos medidos que devuelven entre 1,6 y 4,5 dB de grave.
2. **El Air del compresor sigue sumando agudos aunque el compresor esté apagado.** En el estado actual de la Pi (compresor a 0 y Air a 0,75) añade +6,3 dB a 10 kHz. La mezcla suena más fina y con menos grave en proporción, y el knob que parece apagado no lo está.
3. **La Pi 5 no tiene salida de reserva.** No tiene jack de auriculares. Si la Behringer se cae, Freon busca «Headphones», no lo encuentra e intenta el HDMI, que falla (error 524 en el registro). El resultado es silencio hasta que la Behringer vuelve.
4. **La Pi 5 está con buffer 2048**: 3 × 2048 muestras, es decir, 128 ms de retraso de salida. Además, el hilo de audio va fijo en el núcleo 0, que es donde caen todas las interrupciones del USB, de la tarjeta SD y de la wifi (unas 1000 por segundo, medido).
5. **El Juno gasta 10 veces o más que cualquier otro motor**: 13 % de un núcleo del Mac con 6 voces, frente a menos del 1,5 % de los demás. Sin sobremuestreo baja a 8,5 % (−35 %). En la Pi 5 está compilado con sobremuestreo 2×.
6. **Siguen abiertos varios puntos de la auditoría anterior**: la web abierta a toda la red sin clave, las grabaciones guardándose en el hilo de control y grabar desde la web mientras se guarda la anterior.

**La base sigue siendo muy sólida**
- ASan, UBSan y ThreadSanitizer salen limpios.
- 0 muestras rotas en el estrés y ningún `nan` en `--niveles`.
- El render con 1 y con 4 núcleos da un WAV idéntico bit a bit.
- La cola de MIDI ya está protegida y el reparto entre núcleos es correcto.

**Prioridades para el próximo concierto:** puntos 1–4 de la lista final. Son de esfuerzo bajo, salvo la salida de reserva, que es una decisión de hardware.

---

## 1. Construcción y código

### Lo que se ha medido
| Prueba | Resultado |
|---|---|
| `./build.sh` | Compila. Hay 4 avisos en código propio: `freon-machine.cpp:1624` (`note` sin usar), `:1628` (`kGrNote` sin usar) y `:2496` (`ap` y `due` capturados sin usar). |
| `--render` con 1 y 4 núcleos | WAV idénticos. Consumo medio: 5,5 % y 6,1 % de un núcleo del Mac. |
| `--niveles` | Ningún `nan`. Medias: Juno −37,2 dB, DX7 −22,3, Moog −29,7, 303 −29,0, Game Boy −29,4, Wavetable −29,1, Supersaw −29,1 y Modal −29,9 (el Juno bajo es decisión tuya). |
| `--estres 30` (buffer 128, 1 núcleo) | Media 62,6 %, pico de cálculo 99 %, salida máxima 0,487 y 0 muestras rotas. |
| `--estres 15` con ASan y UBSan | Sin errores, 0 muestras rotas. |
| `--estres 15` con tsan y 4 núcleos | 0 avisos. Esta prueba no pasa por los hilos de MIDI ni por la web. |

### Arquitectura: lo revisado y correcto
- **Hilo de audio** (`Audio::process`, `freon-machine.cpp:403`):
  - no reserva memoria, no se bloquea y no lee ni escribe archivos;
  - las órdenes llegan por `cmds` (un solo escritor: el hilo de control);
  - cada nota MIDI se aplica en su muestra exacta, con un bloque de retraso.
- **Cola `midi`:** ahora cada escritor pasa por `pushMidi()` con un mutex (`:251-252`). El hilo de audio solo lee, sin bloquearse. El fallo E3 de la auditoría anterior está resuelto.
- **Cola `ctlq`:** recibe de varios puertos MIDI, y en Linux cada puerto tiene su propio hilo. Aun así es segura, porque toda la función de llegada (`MidiHub::cb`) va dentro del mutex `h.mx` (`:2090`), así que nunca escriben dos a la vez.
- **Reparto entre núcleos** (`par.h`):
  - Cada tarea se coge con una sola operación atómica. La «generación» de 16 bits evita que un ayudante dormido coja una tarea vieja.
  - El hilo de audio nunca espera a una tarea que nadie ha empezado.
  - Los ayudantes esperan con WFE: el núcleo se duerme hasta el siguiente aviso, como mucho unos 100 µs.
  - Correcto. El render con 1 y 4 núcleos sale idéntico.
- **NaN y subnormales:**
  - FTZ en el hilo de audio y en cada ayudante;
  - protección por pista (`Strip::stageA`, `:182`), por envío (`:225`) y en el máster, antes y después del compresor (`:549-556`).
- **Vaciado del máster tras una avería** (E8 de la auditoría): medido, `MasterFx::reset()` tarda 0,12 ms en el Mac, el 2 % de un bloque. En la Pi 5 será del orden de 0,5 ms. Deja de ser un problema.

### Hallazgos
**1.1 Código muerto que escribe en la cola de MIDI sin el mutex.** *Menor · Código · mínimo*
`Control::lpd8` define una lambda `note` que hace `au.midi.push` directamente, saltándose `pushMidi()` (`freon-machine.cpp:1624`). Hoy no se usa: es el aviso de compilación. Pero si alguien la vuelve a usar, reaparece el fallo de los dos escritores (E3). Conviene borrarla, junto con `kGrNote` (`:1628`).

**1.2 El contador de «Dropouts» puede contar cortes que no lo son.** *Menor · Código + Medido*
`late` sube cada vez que un bloque tarda más de lo que dura (`:428`). Pero con 3 bloques de colchón (`bloques=3`), un bloque lento no siempre se oye. En el Mac, con `--sin-audio`, la web marcó «DROPOUTS 5» sin audio real. El número sirve de alarma, pero no distingue un corte real de un bloque lento.
- Propuesta: dos contadores separados, «bloques lentos» y «cortes» (estos, los de RtAudio).

**1.3 La web y el control.** Lo revisado en `server.h` está bien:
- cola de salida por cliente, con desconexión si no lee;
- límite de tamaño de los mensajes;
- comprobación de origen.

Siguen S1 y S4 (punto 3).

---

## 2. Eficiencia

Coste medido en un núcleo del Mac, en % de un bloque de 256 muestras a 48 kHz (5,33 ms). Son cifras para comparar entre sí: la Pi 5 es del orden de 2–3 veces más lenta por núcleo (**sospecha**: hay que medirla, ver punto 3.7).

| Qué | Coste | Comentario |
|---|---|---|
| Reposo (3 pistas, nada sonando) | 1,4 % | |
| **Juno, 6 voces** | **13,1 %** | **8,5 % sin sobremuestreo (`JUNO_OS=1`)** |
| Supersaw, 6 voces | 1,2 % | |
| 303 | 0,9 % | |
| Moog | 0,8 % | |
| Wavetable, 6 voces | 0,6 % | |
| DX7, 6 voces | 0,5 % | |
| Modal, 6 voces | 0,4 % | |
| Game Boy | 0,2 % | |
| Batería 808 / 909 / Chip / FM / Experimental (8 sonidos cada semicorchea) | 1,5 / 1,4 / 1,1 / 0,9 / 0,7 % | |
| Envíos (cada uno) | 0,1–0,3 % | El más caro, Plate: 0,3 % |
| Máster: Distorsión, Space Plate, Riser, Space Hall/Dark, Sweep | 0,2–0,4 % | |
| Máster: resto (Repeat, Gater, Lo-Fi, Throw, Comb, Spatializer, compresor + Air) | < 0,15 % cada uno | |
| Granular con 4 notas / Breaks | 0,3 / 0,06 % | |
| Bloque en el que se aplica una memoria entera (3674 claves) | 0,9 ms, el 17 %, frente a 0,17 ms en un bloque normal | Una sola vez, al cargar |

**2.1 El Juno es lo único caro de verdad.** *Importante en la Pi · Medido · bajo*
- **Qué gasta:** 6 voces del KR-106 cuestan más que todo lo demás junto. El `--estres` (18 voces de Juno) da de media un 62 % de un núcleo del Mac, y la mayor parte es el Juno.
- **Cómo se compila:** `build.sh:13` solo quita el sobremuestreo en la Pi 3 y la Pi 4, así que la Pi 5 usa `JUNO_OS=2` (`juno.h:15`).
- **Opciones, de menos a más trabajo:**
  1. Medir en la Pi 5 un Juno de 6 voces con y sin sobremuestreo (punto 3.7). Si hay margen, dejarlo así.
  2. Si no lo hay, compilar la Pi 5 con `-DJUNO_OS=1`: −35 % medido. Antes, escuchar a ciegas si se nota la diferencia en el aliasing (con el VCF del Juno-6 cerrado no debería notarse mucho).
  3. Un ajuste «Juno: calidad / ligero» en System, para no depender de la compilación.

**2.2 El motor anterior sigue calculando 4 s tras un cambio de motor.** *Menor · Medido*
Es F2 de la auditoría anterior (`params.h:226-246`). Medido: mientras dura, un Juno que se acaba de quitar cuesta 0,7 ms por bloque en el Mac. Al cambiar de motor con el Oxi (CC 3), ese pico se suma a lo que suene.
- Propuesta: que el motor anterior deje de calcularse en cuanto esté en silencio, no a los 4 s fijos. El Juno ya sabe cuándo está en reposo (`juno.h`, `idle_`).

**2.3 Los ayudantes gastan aunque no haya música.** *Menor · Medido en la Pi 5*
Con el Oxi conectado y sin tocar, cada uno de los 3 ayudantes está al 2,5 % y el hilo de audio al 3 %. Es la espera en bajo consumo: correcto y barato. Solo conviene saberlo para leer el `top`.

**2.4 Nada más merece trabajo hoy.** Efectos, envíos, batería y el resto de motores suman muy poco. Abaratar el Juno es lo único que mueve la aguja.

---

## 3. Estabilidad en directo

### Lo leído en la Pi 5 (sin tocar nada)
| Dato | Valor |
|---|---|
| Sistema | Raspberry Pi 5 Rev 1.1, núcleo 6.18.50 con PREEMPT (no RT), gobernador *performance* a 2,4 GHz |
| Temperatura y tensión | 46 °C. `throttled=0x0`. Alarma de tensión (`rpi_volt`) en 0. |
| Memoria y disco | 7,6 GB libres. SD al 8 %. |
| Tarjetas de audio | 0: USB Audio CODEC (Behringer, USB 1.1, **16 bits**); 1 y 2: HDMI; 3: OXI ONE. **No hay salida analógica.** |
| Freon | Buffer **2048**: periodo 2048 y colchón 6144 muestras = 128 ms. Sin entradas, porque busca «Clarett». Hilos de audio con tiempo real (RR/FIFO 80). |
| Servicio | `--audio "Headphones" --buffer 512`, aunque manda lo de `freon-sistema.txt` (`audio=USB Audio CODEC`, `buffer=2048`, `entrada=Clarett`) |
| Interrupciones | USB del Oxi (`xhci usb1`), USB de la Behringer (`usb3`), SD (`mmc0`) y wifi (`mmc1`, 1,86 millones en 30 min ≈ 1000/s): **todas en el núcleo 0** |
| Registro | En cada consulta de interfaces, errores del HDMI («error 524», «Invalid CTL pulse»): inofensivos, pero llenan el registro |
| Estado actual | Compresor a 0 con Air 0,75. Limitador del máster a −3. Limitadores de Kick y Snare a −3. Clip «Clean». |

**3.1 Sin salida de reserva en la Pi 5.** *Crítica · Medido y Código · hardware y bajo*
- **Qué pasa:** si la interfaz no vuelve tras 3 intentos, se pasa a la de reserva (`onStall`, `freon-machine.cpp:2506-2513`). La reserva busca «Headphones» y «bcm2835» (`:2375`), que solo existen en la Pi 3 y la Pi 4. En la Pi 5 no hay nada que encontrar. Se queda con el dispositivo «default» de ALSA, que sin la Behringer es el HDMI: no abre (error 524) o suena por un HDMI que no está conectado. Freon pasa al audio falso, es decir, silencio.
- **Lo que sí funciona** (comprobado en el código): cuando la Behringer vuelve, `onAudioCheck` la reconoce por su nombre en `/proc/asound/cards` y vuelve a ella sola.
- **En el concierto:** un tirón del cable USB o un fallo de la Behringer deja silencio hasta que vuelve. No hay plan B.
- **Opciones:**
  1. Una segunda interfaz USB barata, siempre conectada y elegida como reserva. Se puede elegir por nombre, como ya se hace con `autoOut`.
  2. Una tarjeta de sonido I2S sobre la Pi (tipo HiFiBerry DAC+), que no depende del USB. Es la reserva más robusta.
  3. Mientras tanto, en el código:
     - que la reserva no intente nunca el HDMI;
     - que la barra de avisos diga claramente «SIN SALIDA DE RESERVA» en la Pi 5;
     - quitar `--audio "Headphones"` del servicio.

**3.2 El hilo de audio comparte el núcleo 0 con todas las interrupciones.** *Importante · Medido (interrupciones) + Sospecha (efecto) · mínimo*
- **Qué pasa:** `process()` fija el hilo de audio al núcleo 0 (`freon-machine.cpp:405`) y los ayudantes van a los núcleos 1–3. En la Pi 5, el USB, la SD y la wifi interrumpen en el núcleo 0, y la wifi lo hace unas 1000 veces por segundo. Con PREEMPT (sin RT), las interrupciones no se pueden aplazar: le roban tiempo justo al hilo que no puede llegar tarde.
- **En el concierto:** con buffer grande no se nota. Con 256 y mucha carga, es la causa típica de cortes que «no se explican por la CPU».
- **Propuesta (de menos a más trabajo):**
  - fijar el hilo de audio al núcleo 3 y los ayudantes a 0–2, pero tampoco al 0 si se puede, y usar 3 núcleos en vez de 4;
  - o mover las interrupciones al núcleo 0 y dejar el 1 para el audio (`irqaffinity`);
  - o, en la línea de comandos del arranque, `isolcpus` para el núcleo del audio.

  Hay que medirlo con `--estres` y con `cyclictest` en la Pi.

**3.3 Buffer 2048 y mínimos.** *Importante · Medido · mínimo*
- **Qué hay:** 128 ms de retraso de salida no son tocables en directo (el Oxi y el audio se desfasan; los pads del LPD8 llegan tarde).
- **Mínimos:** `minBuffer()` (`:2228`) solo protege la Pi 3. En la Pi 5 se puede elegir 64, y con una interfaz USB 1.1 ese es justo el valor que bloqueó la Clarett en la Pi 3.
- **Propuesta:**
  - probar 256 y luego 512 con la Behringer, como pide `MEJORAS.md`, y quedarse con el más bajo que aguante una hora sin cortes;
  - poner el mínimo a 256 para cualquier interfaz USB 1.1, no solo en la Pi 3.

**3.4 Reconexión de USB y MIDI.** *Revisado en el código*
- **MIDI:** al desconectar se cierra el puerto y se sueltan las notas (`K_ALLOFF`, `:2144`). Al volver, se reabre. El reloj pasa a otro puerto si el primero se calla 1 s.
- **Audio:** el vigilante reabre si el audio deja de llamar 1 s. Tras 3 intentos pasa a la reserva (ver 3.1).
- **Reapertura:** la de la wifi ya solo ocurre al entrar o salir de la wifi propia (E2, resuelto).
- **Pendiente en la Pi 5:** la prueba de desenchufar la Behringer y el Oxi con música (punto 3.7).

**3.5 Memorias y estado corrupto.** *Menor · Código + Medido*
- **Cargar una memoria:** cuesta 0,9 ms en el bloque en que se aplica, en el Mac (17 % de un bloque de 256). En la Pi 5 serán unos 2–3 ms. Con buffer 256 cabe, pero se suma a lo que suena. **Sospecha:** con carga alta, un posible clic al cargar en mitad de un tema.
  - Propuesta: aplicar las órdenes de una memoria en 2–4 bloques, como ya se hace al vaciar los envíos.
- **Una memoria cortada:** `memLoad` reutiliza `load()`. Si el archivo está cortado, busca `memorias/X.json.bak`, que no existe. Entonces aplica lo que pudo leer, y lo que falta conserva el valor que tenía (`:1013-1017`). El resultado es una mezcla de dos estados sin aviso.
  - Propuesta: si la memoria está incompleta, no cargarla y avisar en la web.
- **El estado principal:** se guarda bien (archivo aparte, `fsync`, renombrado y `.bak`).

**3.6 Lo que queda de la auditoría anterior**
| N.º | Qué | Estado |
|---|---|---|
| E1 | Corriente y USB de la Pi 3 | En la Pi 5: `throttled=0x0` y alarma de tensión en 0. Vigilar con más aparatos conectados. |
| E2 | Reapertura del audio por la wifi | **Resuelto** (solo al entrar o salir de la wifi propia) |
| E3 | Dos escritores en la cola de MIDI | **Resuelto** (`pushMidi` con mutex). Queda el código muerto de 1.1. |
| E4 | Aviso de tensión baja | **Resuelto** (hwmon `rpi_volt`). El de cortes, ver 1.2. |
| E5 | Las grabaciones se guardan en el hilo de control | **Abierto.** `finishRec()` → `saveWav` (`:845-859`, llamado en `:1815`). Hasta 8 MB parados en la SD: 1–2 s sin knobs ni pads. |
| E6 | Registro con retraso | **Abierto en parte.** No hay `setvbuf`, aunque los mensajes importantes ya hacen `fflush`. |
| E7 | Un motor averiado se queda mudo | Abierto. Sigue sin reiniciarse el motor. Riesgo bajo. |
| E8 | Vaciado del máster en el audio | **Ya no es un problema:** medido, 0,12 ms. |
| E9 | Grabar desde la web mientras se guarda | **Abierto.** La web no comprueba `recState` (`:1452`); la controladora sí (`:1686`). |
| E10 | Ajustes de sistema sin `fsync` | **Abierto** (`sysSave`, `:2222-2225`) |
| C1 | Saturación final siempre puesta | **Resuelto:** interruptor Color/Clean. Medido: Color quita 0,3 dB de grave, inapreciable. |
| S1 | Web sin clave en toda la red | **Abierto.** La Pi arranca con `--host 0.0.0.0`. |
| S4 | Conexiones que nunca se cierran | **Abierto** (`server.h:86`) |

**3.7 Pruebas que solo se pueden hacer en la Pi** (no se hicieron para no molestar a la Freon que estaba en marcha)
1. Medir el estrés sin la Freon del servicio en marcha: `./bin/freon-machine --estres 30 --nucleos 4` y con `--nucleos 3`. Repetirlo con el hilo de audio en otro núcleo (3.2).
2. Medir el coste del Juno de 6 voces con `JUNO_OS=2` y con `JUNO_OS=1` (2.1).
3. Buffer 256 y 512 con la Behringer durante una hora: cortes, `late` y CPU máxima (3.3).
4. Desenchufar la Behringer con música: comprobar el silencio y la vuelta sola (3.1). Desenchufar el Oxi: comprobar las notas soltadas y que el reloj vuelve.
5. Cambiar de wifi casa → propia con música.
6. `cyclictest -m -p 80 -t4 -a` 10 minutos, con la wifi activa, para ver la latencia de cada núcleo (3.2).

---

## 4. Calidad de los instrumentos frente a los originales

### 4.1 El grave (lo que más preocupa)

**Medido con el banco de medidas.** Los niveles son la energía en dB por bandas: bajo 100 Hz, de 100 Hz a 1 kHz y sobre 5 kHz. Se usa el estado actual de la Pi 5, con bombo 808 en negras y el Moog de la pista 4 a 124 bpm.

**a) La cadena no tiene filtros de graves ocultos.** Un seno por la entrada 1 sale igual de 30 a 1000 Hz (±0,0 dB) en todos los casos sin compresor. Revisado en el código:
- **Paso alto de continua:** solo dentro de cada motor, en su sitio:
  - Moog, a 3,8 Hz (`moog.h:140`);
  - 303, a 44 y 24 Hz, como el original (Open303);
  - Game Boy, a 35 Hz, como el DMG;
  - Supersaw, a 60 Hz, ajustable;
  - distorsiones, a 18 Hz y solo si están puestas.
- **Paso altos en el máster:** solo cuando se encienden el Riser, el filtro DJ o el Space (con su Low cut).
- **El 808 de Plaits:** tiene la fundamental en 50 Hz y más del 95 % de su energía bajo 100 Hz. Su paso bajo de tono está en unos 200 Hz con Tone 0,5, sin efecto sobre la fundamental.

**b) Qué cambia el grave en la mezcla**

| Caso | Grave (<100) | Medios (100-1k) | Agudos (>5k) | Grave − medios | Reducción media del compresor |
|---|---|---|---|---|---|
| Neutro (sin compresor, Air ni limitadores) | −18,4 | −26,6 | −75,7 | **8,2 dB** | — |
| Estado de la Pi 5 tal cual (Air 0,75 con el compresor a 0) | −18,4 | −26,6 | **−73,7** | 8,2 | — |
| + compresor amount 1, 10:1 (ataque 4,8 ms) | −20,4 | −24,2 | −70,9 | **3,8 dB** | −8,6 dB |
| + compresor 10:1, ataque 30 ms | −17,6 | −23,0 | −70,1 | 5,4 | −6,9 |
| + compresor 0,5, 2:1, ataque 30 ms | −16,5 | −24,4 | −71,5 | 7,9 | −2,2 |
| Estado de la Pi 3 del 13-11 (compresor 1, 4:1, Makeup +6, Air 0,75) | −18,5 | **−18,7** | −66,3 | **0,2 dB** | −4,6 |
| Clip «Color» en vez de «Clean» | −18,7 | −26,7 | −73,7 | 8,0 | — |
| Limitador del canal Kick a −3 | igual que neutro | | | | No actúa: el pico del bombo llega a −6,7 dBFS |

**Conclusiones**
- **El compresor de bus es el culpable.**
  - **Detector:** es una media de 5 ms del pico (`fx.h:200`), más corta que un ciclo de 50 Hz (20 ms). Así sigue la onda del bombo, comprime cada golpe en su cuerpo y deja pasar el clic de los primeros milisegundos.
  - **Recuperación de ganancia:** la automática (`autoMk = 24 × 0,9 × 0,4 = +8,6 dB`, `fx.h:199`) sube todo lo que hay entre golpes.
  - **Resultado:** el grave del bombo baja 2 dB y todo lo demás sube 2–3 dB. **Pierdes 4,4 dB de grave en proporción con 10:1, y 8 dB con los ajustes del 13-11.** Es exactamente «el bombo y el Moog con poco grave».
- **El Air se aplica siempre que su knob no esté a 0, aunque el compresor esté a 0** (`fx.h:185-188`): +6,3 dB a 10 kHz (medido con un seno) y +2 dB de agudos en la mezcla. No quita grave, pero inclina la balanza.
- **La saturación final y los limitadores no son el problema** con este nivel de mezcla. El limitador del máster a −3 no actuó en la prueba (picos de −6 dB). Con la mezcla completa más fuerte sí actuaría, porque es el pico del bombo el que dispara el limitador. Conviene mirar su lectura de reducción en la web durante un tema.

**c) Arreglos del compresor, medidos con la misma mezcla** (grave − medios; el neutro es 8,1 dB)

| Variante | Grave − medios |
|---|---|
| Actual: 10:1, 4,8 ms, detector de 5 ms | 3,6 dB |
| + paso alto de 90 Hz solo en el detector | 5,2 dB (+1,6) |
| + detector de 20 ms | 4,4 dB (+0,8) |
| + paso alto de 90 Hz y ataque de 30 ms | 6,9 dB (+3,3) |
| «Glue»: 2:1, amount 0,4, 30 ms, paso alto de 90 Hz | **8,1 dB (igual que sin compresor)** |

**Propuesta para el compresor** (esfuerzo bajo; todo en `BusComp`, `fx.h:176-214`):
1. Paso alto de 90–120 Hz en el detector, sin tocar la señal. Es lo que hacen los compresores de bus de mesa (el «SC HPF» de SSL y similares).
2. Ataque por defecto de 20–30 ms y un mínimo de 3 ms.
3. Air independiente: que con el compresor a 0 no actúe, o moverlo a un «EQ de máster» propio y visible (lo pide también `MEJORAS.md`).
4. Punto de partida «Glue»: 2:1, amount 0,3–0,4 y 1–3 dB de reducción, que es lo que se espera en un máster.

**d) El Moog y la resonancia** (motor solo, A1 = 55 Hz, preset «Bass»): el grave apenas se pierde al subir Resonance (grave − total pasa de −0,2 dB con Resonance 0 a −1,9 dB con 0,9). La compensación del 70 % (`moog.h:136`) funciona. Pero ahora el nivel sube 6,7 dB de Resonance 0 a 0,9 (ver 5.2). **El Moog no es la causa del poco grave.** Tu Moog de la pista 4 tiene Cutoff 1038 Hz, Contour 0,6 y Drive 0,45: es brillante a propósito.

**e) El 808 y el Snap** (ver 5.1): con tus ajustes (Snap 1, Tone 0,61) el cuerpo está bien. Pero con Snap a 0 el bombo pierde 15 dB de golpe.

### 4.2 Motor por motor
| Motor | Base | Carácter frente al original | Lo que falta o difiere |
|---|---|---|---|
| **Juno** | KR-106 (medido de Junos reales), modo Juno-6 | Muy fiel: DCO, IR3109 con las curvas del Juno-6, HPF continuo, chorus BBD I/II/I+II y Drift entre voces | Solo modo Juno-6 (sin el VCF/ADSR del 106 ni su HPF por pasos). Sin arpegiador ni Hold, porque el Oxi los hace. Es el más caro (2.1). |
| **Moog** | 3 osciladores PolyBLEP + ladder de Huovilainen a 2× | Buena base: mezclador que satura, ladder con saturación por etapa, osc 3 como LFO y glide | Sin el **bucle de realimentación** (la salida al mezclador por la entrada externa, el truco clásico del Minimoog para engordar el grave). Deriva de afinación muy pequeña (±0,12 %, `moog.h:108-110`). La resonancia sube el nivel (5.2). Sin rueda de modulación (mezcla de osc 3 y ruido). |
| **303** | Open303 | La referencia en software. Pasos altos de 44 y 24 Hz como el original, accent por velocidad y slide al solapar notas, más extras Devilfish | Bien. Falta escuchar el brillo tras el arreglo de los valores de partida (pendiente en `MEJORAS.md`). |
| **DX7** | MSFA (el origen de Dexed) | Fiel; mono como el original | Los knobs solo cambian las notas nuevas (como al editar un DX7). Sin estéreo ni chorus (el original tampoco tenía). |
| **808 / 909** | Plaits (bombo, caja y hihat) + clap, hats, ride y cencerro propios | El bombo 808 de Plaits modela el circuito: fundamental de 50 Hz y cola larga, bien | Los hats y el ride del 909 son de ruido, no las muestras de 6 bits del original: el carácter cambia. El Snap del 808 pesa demasiado (5.1). |
| **Game Boy** | Propio | Pulso con duty y sweep, onda de 4 bits, ruido de 7/15 bits, paso alto DMG de 35 Hz y arpegio LSDJ | Bien. |
| **Wavetable** | Propio, 6 tablas de 16 cuadros | Correcto pero básico frente a PPG o Microwave | Pocas tablas y sin cargar las tuyas. |
| **Supersaw** | Propio, 7 sierras | Al estilo JP-8000, con modo cuerdas | 6 voces. Sin el «Mix» no lineal del JP. |
| **Modal** | Propio, 8 modos | Ligero y sencillo | Lejos de Rings y Elements (pendiente de que decidas si se queda). |
| **Envíos** | FDN Hall, plate de Dattorro, Black hole y delays | Calidad buena para su coste (0,1–0,3 % cada uno) | — |
| **Máster** | Al estilo Octatrack | Completo | El Space y el Delay al 100 % quitan el seco del todo (decidido). Cuidado con usar el Space al 100 % sobre el bombo: el grave desaparece de la mezcla mientras dure. |

**4.3 Salida de 16 bits sin dither.** *Menor · Medido en la Pi*
La Behringer trabaja en 16 bits (`S16_LE`) y RtAudio convierte de 32 bits flotantes a 16 sin dither. En colas de reverb muy bajas puede oírse un grano. Propuesta: dither TPDF en la salida cuando la interfaz sea de 16 bits. Cuesta casi nada.

---

## 5. Lógica de parámetros

**5.1 El Snap del bombo 808 cambia demasiado el volumen.** *Importante · Medido · bajo*
El Snap va a la «FM de ataque» de Plaits (`drums.h:531`). Medido (Tone 0,61):
- Snap 1: −13,2 dB;
- Snap 0: −28,0 dB.

Son 15 dB menos de cuerpo, no solo de clic. Un knob que se llama «golpe» no debería hacer desaparecer el bombo.
- Propuesta: compensar el nivel según el Snap, medido como se hizo con el Tone (`drums.h:530`), o limitar su recorrido a 0,4–1.

**5.2 La resonancia del Moog sube el volumen.** *Menor · Medido · bajo*
Total del Moog: −28,8 dB con Resonance 0, −25,1 con 0,45 y −22,1 con 0,9. La compensación (`moog.h:136`) está normalizada a 0,45. Por debajo pierde 3,7 dB y por encima gana 3 dB.
- Propuesta: compensar menos por encima de 0,45 para que quede en ±1 dB. Es lo esperable en un Minimoog (allí pierde algo de volumen).

**5.3 El compresor de bus mezcla dos cosas en un knob.** *Menor · Código*
- **Amount:** es a la vez el umbral (`−amount × 24`) y la recuperación automática del 40 %.
- **Ratio:** tiene tres pasos (2, 4 y 10:1).
- **Makeup:** se suma encima.

Con Amount 1 y Makeup +6, la mezcla entra al limitador hasta 14 dB más fuerte. Ver 4.1 c para el punto de partida propuesto.

**5.4 Valores de fábrica que conviene revisar.** *Menor*
- **`m.clip`:** de fábrica es «Color» (1, `freon-machine.cpp:125`), pero tú usas «Clean». Si un estado se pierde, vuelve la saturación.
- **`m.bcAir`:** de fábrica es 0, bien. En tu estado está a 0,75 con el compresor apagado (4.1).
- **Limitador del máster:** −1,5 de fábrica y −3 en tu estado. Con −3 se pierden 1,5 dB de techo sin necesidad, porque el limitador tiene anticipación y es exacto.

**5.5 Capa de 16 knobs (`kLayer`, `params.h:190-199`) y controladoras**
- **Coherente:** arriba Cutoff, Reso, Env, A–D y Volume; abajo las envolventes. Volume siempre en el knob 8 (Game Boy: Level; Modal: su Volume).
- **MC-24:** cada fila va a su pista, y el 8.º knob es el Volume de esa pista (`:1667-1670`). Coincide con `CONTROLADORAS.md`. El comentario de `:1505` («y el crossfader») ya no corresponde: el CC 15 queda para el Oxi o para otras controladoras.
- **LPD8:** los 4 programas coinciden con `CONTROLADORAS.md` (knob 1 de Breaks = qué break y knob 8 = nivel; knob 8 del Máster = crossfader). Los pads del sampler y las notas del granular salen ya por el hilo de MIDI, sin retraso.
- **Oxi:** CC 3 y 4 solo actúan si el número cambia, así que una modulación no corta notas. Bien.
- **Batería:** Filter y Reso en los knobs 7 y 8 en los 5 kits (`drumSlots`, `:109-118`). La regla se cumple.

---

## 6. Interfaz web

Medido en el navegador (sin errores de consola):

| Pantalla | Alto de cada pestaña (Instruments / Live / Mixer / Settings) | Notas |
|---|---|---|
| Móvil, 375 × 812 | 5098 / 4147 / 4162 / 3328 px (5–6 pantallas) | **Settings se sale de ancho** (428 px en una pantalla de 375): hay que desplazar de lado |
| 7", 800 × 480 | 3146 / 2582 / 3079 / 2088 px (4–6,5 pantallas) | La cabecera ocupa 176 px de 480 (37 %) |

- **Botones:** de 22 a 44 px de alto. El mínimo cómodo para el dedo es 40–44 px.
- **Knobs:** desde 30 px.
- **Texto:** muchos tamaños de 9–11 px.
- **En el móvil** se ve la ayuda del teclado del ordenador («A W S E D… notes»), que allí no sirve.

**Propuestas, por orden** (esfuerzo bajo a medio):
1. **Vista «Directo» compacta.** Una pantalla sin desplazar con:
   - las 8 combinaciones en botones grandes y el crossfader;
   - los 8 efectos del máster;
   - mute de cada canal;
   - CPU, cortes y tempo.

   Es la única pantalla que hace falta en el escenario.
2. **Cabecera:** que se pliegue, o barra fija de una línea con estado, CPU y avisos.
3. **Settings:** que quepa en 375 px.
4. **Dedo:** botones de al menos 40 px y texto de al menos 12 px en las vistas de directo.
5. **Ayuda del teclado:** ocultarla en pantallas táctiles.
6. **Avisos:** la barra de avisos ya existe. Que incluya «sin salida de reserva» (3.1) y «buffer alto» (3.3).

---

## 7. Mejoras para la actuación

### 7.1 Modo «Vintage» al estilo Sequential (Prophet-5 rev4)
**Valoración:** buena idea, barata y de bajo riesgo. Da vida a los sintes sin tocar su núcleo. En el Moog es poco trabajo. El Juno ya tiene un `Drift` (`params.h:33`) que hace parte de esto.

**Qué hace el Vintage del Prophet-5 rev4:** un knob de 1 a 4 que aumenta a la vez varias cosas:
- la deriva lenta de la afinación;
- la diferencia fija entre voces: afinación, corte del filtro y tiempos de envolvente;
- un poco de «barro» en la forma de onda.

**Diseño para el Moog** (un knob «Vintage» de 0 a 1 en la página 2 de `moog.h`):
- **Deriva lenta:** un paseo aleatorio a 0,1–0,5 Hz por oscilador, de 0 a ±8 cents. Sustituye al `drift_` actual (±0,12 %, fijo). Las cuentas se hacen una vez por bloque, no por muestra.
- **Por nota** (al disparar, en `noteOn`), un número aleatorio fijo hasta la nota siguiente:
  - afinación de osc 2 y 3: ±0–5 cents;
  - corte: ±0–6 %;
  - tiempos de envolvente: ±0–10 %;
  - ancho del pulso: ±0–3 %.
- **Calentamiento:** con Vintage alto, la afinación se va 2–3 cents hacia abajo los primeros segundos tras un cambio grande de nota, como un VCO frío. Opcional.
- **Coste:** prácticamente cero (3 números aleatorios por nota y un filtro por bloque).

**Diseño para el Juno:**
- El knob Vintage lleva el `Drift` del KR-106 (`SetDriftAmount`) y añade diferencia por voz en corte y envolvente, si el KR-106 lo permite por voz (hay que mirar su `Voice`).
- Si no lo permite, basta con el `Drift` y una diferencia de corte por voz aplicada al disparar la nota.

**Esfuerzo:** bajo (Moog) y medio (Juno). Va en el estado como un parámetro más (sube `kStateVer`) y en el instrumento del Oxi (`--oxi`).

### 7.2 Granular con modo ms y modo sync
**Valoración:** muy útil para el directo y sin riesgo de estabilidad. El granular actual (`live.h:318-374`) ya tiene todo lo necesario salvo el tempo. Lo único que hay que tocar fuera de él es pasarle el tempo y la posición (`freon-machine.cpp:218`, igual que ya se hace con los breaks).

**Diseño:**
- **Nuevo parámetro `Time`:** «ms» o «Sync».
- **En modo Sync:**
  - **Size en divisiones:** 1/64, 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4 y 1/2. El tamaño en muestras sale del tempo del Oxi (`bpm`), como en el Repeat del máster.
  - **Density en granos por división:** 1/32, 1/16, 1/8T, 1/8 y 1/4. Cada grano arranca justo en la rejilla, usando la posición del reloj (`pos`, que ya existe), no un acumulador libre. Con Spray a 0 el resultado es rítmico; al subir el Spray se desordena alrededor de la rejilla.
  - **Position en tiempos hacia atrás:** de 1/16 a 4 compases. Es un «beat repeat granular»: los granos leen lo que sonó hace exactamente N tiempos. Límite: el anillo es de 8 s, así que 4 compases solo caben por encima de 120 bpm. A menos tempo se acota y se avisa.
  - **Movimiento (`Scan`):** la posición recorre el anillo a tempo, de 1/4 a 8 compases, hacia delante o hacia atrás, con forma de rampa o triángulo.
- **En modo ms:** como ahora.
- **Al cambiar de modo:** se mantiene el valor más parecido.
- **Controladora:** en el LPD8, programa 3, los knobs 2 (tamaño) y 3 (densidad) pasan a ir por pasos en modo Sync. Es lo mismo que ya hace el Repeat del máster con sus divisiones.
- **Coste:** igual que ahora (0,3 % con 4 notas). Lo único nuevo es una división por bloque.

**Esfuerzo:** medio, un día con pruebas.

### 7.3 Otras ideas de cara al directo (solo como propuesta)
- **Bucle de realimentación del Moog** (salida → mezclador): el grave «gordo» del Minimoog. Esfuerzo bajo.
- **EQ de máster sutil y graves en mono por debajo de 120 Hz** (ya decidido en `MEJORAS.md`). El Spatializer ya pone los graves en mono, pero solo cuando está encendido.
- **Lectura LUFS y de reducción en la vista de directo**, para ver si el compresor o el limitador trabajan de más.

---

## Lista de mejoras por prioridad

**Prioridad 1 · antes del próximo concierto (impacto alto, esfuerzo bajo)**
1. **Compresor de bus** (4.1 c, 5.3):
   - paso alto de 90–120 Hz en el detector;
   - ataque por defecto de 20–30 ms;
   - Air que no actúe con el compresor a 0, o como EQ aparte;
   - punto de partida «Glue» a 2:1.

   Hoy, sin tocar código: compresor a 0 **y Air a 0**, o 2:1, amount 0,4 y ataque 30 ms.
2. **Salida de reserva en la Pi 5** (3.1):
   - decidir el hardware (segunda interfaz USB o DAC I2S);
   - en el código, no intentar el HDMI y avisar en la web;
   - quitar `--audio "Headphones"` del servicio.
3. **Buffer** (3.3): bajar de 2048 a 256 o 512 tras la prueba de una hora, y poner el mínimo a 256 con interfaces USB 1.1.
4. **Hilo de audio fuera del núcleo 0** (3.2): una línea en `:405`, medida antes y después con `--estres` y `cyclictest` en la Pi.
5. **Snap del 808** (5.1): compensar su nivel.
6. **Borrar el código muerto** que escribe en la cola de MIDI sin mutex y quitar los 4 avisos de compilación (1.1).

**Prioridad 2 · impacto medio**
7. **Juno:** medir en la Pi 5 y, si hace falta, `JUNO_OS=1` (−35 %) o un ajuste ligero/calidad (2.1). Que el motor anterior deje de calcularse en silencio (2.2).
8. **Grabaciones:** guardarlas en un hilo aparte (E5) y no grabar desde la web mientras se guarda (E9).
9. **Web:**
   - vista de directo compacta;
   - Settings que quepa en el móvil;
   - botones de al menos 40 px;
   - cabecera plegable (punto 6).
10. **Seguridad:** PIN en la web o solo en la wifi propia (S1), cierre de conexiones inactivas (S4) y `fsync` de los ajustes de sistema (E10).
11. **Memorias:** aplicarlas en varios bloques y no cargar una que esté incompleta (3.5).
12. **Contador de cortes:** separar «bloques lentos» de «cortes» (1.2).

**Prioridad 3 · sonido y funciones nuevas**
13. **Granular con modo Sync** (7.2).
14. **Vintage** en el Moog y después en el Juno (7.1).
15. **Resonancia del Moog** a ±1 dB (5.2) y bucle de realimentación (7.3).
16. **Dither** en salidas de 16 bits (4.3).
17. **Valores de fábrica:** Clip «Clean» y limitador a −1,5 (5.4).
18. **El resto de 4.2**, cuando decidas sobre Modal y Wavetable.

**Pruebas pendientes en la Pi 5:** ver 3.7.
