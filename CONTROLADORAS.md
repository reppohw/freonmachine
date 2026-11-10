# Freon Machine · reparto de controladoras (propuesta)

Sección de instrumentos: 4 Novation Launch Control 3, una por pista (batería, T2, T3 y T4).
Cada una manda por el canal MIDI de su pista. El resto (sampler, granular, voz, Sub, máster y
mezclador) se decide más adelante.

## Qué tiene la Launch Control 3

- 16 encoders sin fin, cada uno con un LED RGB.
- 8 botones con LED.
- Page ▲▼ (2 botones), Track ◀▶ (2), Shift, Mode y Function.
- Pantalla OLED, USB-C y MIDI DIN (In, Out y Out2/Thru).
- Hasta 7 modos Custom, que se programan con Novation Components.

## La pantalla: sí se puede (modo DAW)

Lo dice la guía de programación de Novation, «Launch Control 3 Programmer's Reference».

**Cómo se toma la controladora**
- Freon pone la controladora en modo DAW por SysEx (`F0 00 20 29 02 16 02 7F F7`) y desde ahí la controla entera: pantalla, colores de los LED y modo de los encoders.
- Así no hace falta preparar nada con Components.

**Pantalla**
- **Texto:** `F0 00 20 29 02 16 06 <pantalla> <campo> <texto> F7`.
- **Una pantalla por control:** cada encoder tiene la suya, que aparece sola al girarlo.
- **Pantalla fija:** la que se ve en reposo.
- **Formatos:**
  - 2 líneas: parámetro y valor.
  - 3 líneas: título, parámetro y valor.
  - 1 título más 8 nombres (los de una fila de encoders).
- **Caracteres especiales:** hay cuadro vacío (1Bh) y cuadro lleno (1Ch), con los que se dibuja una **barra que crece** dentro del texto del valor, por ejemplo `■■■■■□□□ 63%`.
- **Dibujo propio:** también se puede mandar un dibujo completo de 128 × 64 píxeles. Así cabe una barra de verdad, letras grandes o una curva de envolvente. Solo hay uno en memoria a la vez y hay que esperar la confirmación antes de mandar el siguiente; para refrescar un valor al girar basta.

**Encoders y LED**
- Encoders en relativo, por filas, con el centro en 64.
- LED por color de una paleta, o RGB exacto por SysEx. El brillo es global.

**Lo que se vería en cada caso**
- **En reposo (pantalla fija):** «T2 · Juno» / «Preset: Strings».
- **Al girar un encoder:** «T2 · Juno» / «Cutoff» / `■■■■■□□□ 1,3 kHz`.
- **Al cambiar de motor:** el nombre del motor nuevo, y los 8 nombres de una fila de knobs (formato 1 + 8 nombres).

**Pulsación de los encoders:** en la documentación de Novation no aparece; parece que no tienen. Lo que sí hay es «toque» (Shift + girar), que hace aparecer la pantalla del parámetro sin cambiarlo.

**Por comprobar con la controladora en la mano**
- **Números que no aparecen en la guía:** los de los botones (Page, Track, Mode, Function) y la paleta de colores. Las tablas no salen en el texto de la guía.
- **Caracteres por línea:** la guía no lo dice.
- **Cuatro iguales:** en modo DAW las cuatro mandan por los mismos canales, así que Freon las distingue por su puerto USB. Hará falta un paso de «emparejar» la primera vez: pulsas Function en una y eliges a qué pista va. Queda guardado.

## Sintes (T2, T3 y T4)

| Control | Qué hace |
|---|---|
| 16 encoders | Página 1: la capa de 16 knobs del motor, sin huecos |
| Page ▼ / Page ▲ | Página 2 (el resto de parámetros) / vuelta a la página 1 |
| Shift mantenido | Página 2 mientras se mantiene (para un toque rápido) |
| Track ◀ ▶ | Motor anterior / siguiente (10 motores) |
| 8 botones, fila «Presets» | Pulsar = cargar preset 1–8; Shift + pulsar = guardar |
| 8 botones, fila «Switches» | Interruptores y selectores del motor (onda, chorus, glide…; en el DX, los 8 algoritmos) |
| Mode | Cambia la fila de botones entre Presets y Switches |
| Function + girar un encoder | Ese parámetro vuelve a su valor de fábrica |
| Function + Track ◀ | Silencia todas las notas de la pista (pánico) |

**Luces:**
- **Color de los encoders (decidido):** el color dice dónde está aproximadamente el knob. Verde abajo, amarillo en el centro y rojo arriba, en degradado continuo (RGB exacto por SysEx). Al cambiar de motor, preset o página, los colores se ponen al día solos.
- **Encoders:** el color marca la fila (arriba, carácter; abajo, envolventes) y el brillo marca el valor. En la página 2 cambia el color, para saber dónde estás.
- **Botones en Presets:** el preset cargado, brillante; los demás guardados, tenues; los huecos vacíos, apagados.
- **Botones en Switches:** encendido o apagado según el estado.

**Pantalla (si Novation deja escribirla por MIDI):** el nombre del motor al cambiarlo, y el
parámetro y su valor al girar. Si no se puede, la pantalla muestra lo que tenga configurado
en Components.

### DX con capa propia de FM (opción 2, elegida)

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Arriba | Ratio 1 | Ratio 2 | Ratio 3 | Ratio 4 | Level 2 | Level 3 | Level 4 | Feedback |
| Abajo | Mod A | Mod D | Mod S | Volume | Amp A | Amp D | Amp S | Amp R |

- Página 2: Detune y Velocity.
- Algoritmo: en los botones (fila Switches, algoritmos 1–8).

## Batería (T1)

> Pendiente: quizá con otra controladora. Candidata: Behringer BCR32, con 32 encoders y anillo de LED en 4 filas, que dan 8 sonidos × 4 parámetros a la vez. Su firmware es abierto desde finales de octubre de 2026. Lo de abajo es la idea con una Launch Control 3, por si acaso.


Las 8 columnas son los 8 sonidos, en el orden de la pantalla: bombo, caja, HH cerrado,
HH abierto, clap, rim, tom y perc. Hay dos vistas, que se cambian con Mode.

### Vista «Kit»: 2 parámetros por sonido, por páginas

| Página (Page ▲▼) | Fila de arriba | Fila de abajo | Kit FM |
|---|---|---|---|
| 1 | Pitch | Decay | Pitch / Decay |
| 2 | Filter | Reso | Ratio / FM amount |
| 3 | Snap | Sweep | FM decay / Feedback |
| 4 | Level | Velocity | Noise / Level |
| 5 | Pan | — | Pan / Velocity |

Los botones (sin mutes ni solos, que en la batería no se usan): ver «Botones de la batería» más abajo.

### Vista «Foco»: un sonido con todo a la vista

Eliges un sonido y los 16 encoders pasan a ser suyos:

| Fila | Encoders |
|---|---|
| Arriba | Pitch, Decay, Snap, Filter, Reso, Sweep, Level, Velocity |
| Abajo | Pan, más los parámetros FM si es el kit FM (Ratio, FM amount, FM decay, Feedback, Noise) |

### Botones de la batería (propuesta, por elegir)

Mode va pasando por tres filas de botones. Cada fila tiene un color en los LED y su nombre en la pantalla.

1. **Elegir sonido:** pulsar el botón de un sonido lo pone en Foco, y la pantalla muestra su nombre y sus parámetros.
2. **Kits:** 8 kits guardados con todos los parámetros de los 8 sonidos. Pulsar carga uno y Shift + pulsar lo guarda. Es el equivalente a los presets de los sintes, para cambiar el color del kit entero en directo.
3. **Efectos mantenidos** (suenan mientras pulsas, como los de los breaks):
   1. Roll 1/16: ese sonido se repite a tempo.
   2. Roll 1/32.
   3. Roll en tresillos.
   4. Pitch down: el kit baja de tono mientras lo mantienes.
   5. Filtro: barrido del kit hacia paso bajo.
   6. Throw: la caja y el clap van al delay.
   7. Reverse en el kit: cada golpe suena al revés.
   8. Choke: corta los hihats y platos.

Con Shift + botón, en cualquier fila, se dispara el sonido para oírlo.

Las demás funciones de la batería:
- **Track ◀ ▶:** kit anterior o siguiente.
- **Function + girar un encoder:** ese parámetro vuelve a su valor de fábrica.
- **Shift + Function:** reset del kit entero (con confirmación: mantener 1 s).
- **Luces:** cada columna lleva su color de sonido. El LED del botón parpadea cuando suena el sonido.
- **Pantalla:** en reposo, el kit y la vista. Al girar, el sonido, el parámetro y la barra.

## Lo que necesita Freon Machine para esto

- **Estado por controladora:** página, vista (Kit o Foco), sonido en foco y fila de botones (Presets o Switches).
- **Valores al cambiar de página o de vista:** al pasar de página, de vista o de motor, mandar a la controladora los valores de lo que ahora controla cada encoder.
- **Mapa de botones:** el de Page, Track, Mode, Shift y Function, configurable por si la controladora manda otros números de CC.
- **Colores de los LED:** la paleta de Novation se elige por el valor del mensaje; hay que mirar su guía de programación.
- **Modo DAW:** Freon pone cada Launch Control 3 en modo DAW, escribe la pantalla (pantalla fija y una por encoder) y fija colores y modo relativo.
- **Emparejado:** cada Launch Control se asigna a una pista por su puerto USB, y queda guardado.
- **Otras controladoras:** las que no sean Launch Control 3 siguen funcionando con el mapa MIDI genérico, como ahora.

## Nakedboards MC-24 (conectado a la Pi)

Manda por el canal 3, potenciómetros absolutos: fila 1 CC 8–15, fila 2 CC 16–23, fila 3 CC 24–31. No hace falta programarlo: Freon reconoce su puerto y traduce cada fila.

- Fila 1 → T2, fila 2 → T3, fila 3 → T4, siguiendo el canal que tenga cada pista.
- Cada fila: Cutoff, Reso, Env, A, B, C, D y Volume.
- Último knob de cada fila (también la 3): Volume de su pista. El crossfader de las combinaciones está en el knob 8 del programa 4 del LPD8.

No debe recibir MIDI: lo que le llega atasca su USB (`urb status -32`) y deja de mandar. Por eso está en «Don’t send to» (`midi.skip`). Si ocurre, se recupera reenchufándolo.

Escribirle la configuración por MIDI desde la Pi no funcionó: lo dejó colgado hasta reenchufarlo y no guardó nada.

## Akai LPD8 mk2 (sampler, breaks, granular y efectos del máster)

Programado desde Freon Machine (copia del original en `controladoras/lpd8-original.syx.txt`, la de Freon en `controladoras/lpd8-freon.syx.txt`). Cada programa manda en su canal y Freon lo traduce (como el MC-24); los knobs van en modo absoluto. Pads: notas 36-43, sensibles a la fuerza, sin enclavar. Knobs: CC 70-77. Freon no le manda nada de vuelta.

| Programa (color) | Canal | Pads 1-8 | Knobs 1-8 |
|---|---|---|---|
| 1 Sampler (naranja) | 11 | disparan los samples 1-8 y eligen el que editan los knobs | afinación, inicio, final, ataque, release, filtro, resonancia, nivel |
| 2 Breaks (verde) | 12 | efectos mientras se pulsan: Repeat, Repeat 1/32, Reverse, Half time, Random, Octave down, Chop, Backwards | **qué break** (los de fábrica y luego los de la carpeta breaks/), afinación, longitud, aleatorio, stutter, filtro, resonancia, tamaño de repetición |
| 3 Granular (azul) | 13 | 1 enciende/apaga, 2 congela, 3-8 notas (do, re, mi♭, fa, sol, do') en el modo tocado | posición, tamaño, densidad, afinación, dispersión, estéreo, feedback, nivel |
| 4 Máster (amarillo) | 14 | eligen y activan las 8 combinaciones (toque = fija, otro toque en la misma = la quita, mantener = mientras se pulsa) | filtro DJ, repeat, gater, space, throw, pitch de la voz, formant de la voz, **crossfader** de las combinaciones (seco → combinación) |

Para cambiar de programa: mantener PROG SELECT y tocar el pad 1-4.
