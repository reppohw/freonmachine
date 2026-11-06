# Freon Machine — contexto para Claude Code

Freon Machine es la caja de directo de Jorge Alarcón, músico electrónico y editor de diálogos. Es un motor de sonido en C++ (sintes, batería, sampler, granular, voz, mezcla y efectos) que secuencia un **Oxi One** por MIDI. El destino final es una **Raspberry Pi 5 sin pantalla**: todo se toca desde controladoras MIDI con LEDs, y la web (`ui/index.html`) sirve solo para configurar desde el móvil o el portátil. Se prueba en un MacBook Air (Apple Silicon) y en una Pi 3 Model B v1.2.

## Regla fundamental: es para tocar en directo
Freon Machine tiene que ser **eficiente y muy estable**. Esto pesa más que cualquier función nueva: nada puede cortar el audio, colgarse ni perder datos en el escenario. Ante la duda, gana la opción más segura y que gaste menos CPU; si una mejora pone en riesgo la estabilidad, no entra. Jorge deja en manos de Claude las decisiones técnicas para conseguirlo (por ejemplo, cómo repartir el trabajo entre núcleos).

## Cómo trabajar con Jorge
- Escribe siempre en **español**, claro y sin jerga. Jorge no es programador, pero sí un profesional del sonido con muy buen oído.
- Cada versión nueva va con los comandos para compilar y arrancar. En este proyecto son `./build.sh` y luego `./bin/freon-machine --entrada <n>`.
- Lo primero son la **estabilidad** (es para tocar en directo) y la **calidad de sonido**. Para los núcleos de sonido se prefiere código probado (Mutable Instruments, Open303, KR-106, MSFA, Airwindows…) a escribir uno propio.
- Le gusta lo simple: cada cosa hace una función. No añadas funciones que no ha pedido.
- Lo pendiente y lo decidido están en `MEJORAS.md`. El reparto de controladoras está en `CONTROLADORAS.md` y la documentación de uso en `LEEME.md`. Mantenlos al día.

## Compilar y probar
- `./build.sh`: versión normal en `bin/freon-machine`. `./build.sh debug`: versión con ASan y UBSan en `bin/freon-machine-debug`.
- `./bin/freon-machine --render prueba.wav`: prueba sin tarjeta de sonido. Recorre kits y motores y dice el consumo medio de CPU.
- `./bin/freon-machine --sin-audio --port 8099`: servidor sin audio para probar la web. Al WebSocket se le pueden mandar mensajes MIDI simulados con `midi <estado> <d1> <d2>`.
- `./bin/freon-machine --estres 30 --nucleos 4`: peor caso con todo moviéndose; dice el pico de cálculo y si hay muestras rotas.
- `./bin/freon-machine --niveles`: nivel de cada motor y preset (nota y acorde), con su pico. Un `nan` es un fallo grave.
- `./bin/freon-machine --oxi oxi-instrumentos`: regenera los instrumentos del Oxi desde las tablas del motor. Hazlo si cambian parámetros o CC.
- `./build.sh tsan`: versión con ThreadSanitizer en `bin/freon-machine-tsan`, para buscar carreras entre núcleos.
- `./bin/freon-machine --list`: interfaces de audio y puertos MIDI.
- Antes de entregar: compilar sin avisos en el código propio, hacer el render, comprobar que el render con `--nucleos 1` y `--nucleos 4` da un WAV idéntico, el estrés con debug y tsan (sin errores) y, si se toca la web, abrirla sin errores de consola.

## Estructura
- `src/freon-machine.cpp`:
  - hilo de audio (`Audio`) y hilo de control (`Control`), que lleva el estado, la web, el mapa MIDI de controladoras, la devolución de valores y LEDs, y el guardado seguro;
  - `MidiHub` y `main`.
- `src/params.h`:
  - especificaciones de los 8 motores (`kEng`), presets de fábrica y capa de 16 knobs (`kLayer`);
  - `MeloEngines`, que contiene los motores de una pista;
  - la web se construye a partir de aquí.
- Motores:
  - `juno.h` (KR-106, modo Juno-6);
  - `dx.h` + `dx7_glue.*` (MSFA);
  - `moog.h` (ladder de Huovilainen);
  - `voice303.h` + `open303_glue.*` (Open303);
  - `gameboy.h`;
  - `synths2.h` (Wavetable, Supersaw, Modal, y el código ya sin uso de West Coast y Organ).
- `src/drums.h` + `plaits_glue.*`: kits 808/909 (Plaits), FM al estilo Model:Cycles (propio), Chip y Experimental.
- `src/live.h`, `src/voice.h`: sampler, breaks, granular y voz. `src/fx.h`: mezcla, envíos (FDN, plate de Dattorro, delays), efectos de máster y suma analógica. `src/par.h`: reparto del audio entre núcleos. `src/server.h`: HTTP + WebSocket.
- `third_party/`: RtAudio, RtMidi, `open303/`, `kr106/` (GPL v3), `mi/` (Plaits + stmlib, MIT) y `msfa/` (Apache 2.0). Los cambios propios están marcados con «Freon Machine».

## Reglas de la batería
- **Filter y Reso siempre en knobs contiguos**, en todos los kits y en cualquier versión futura (hoy, knobs 7 y 8). Misma función en la misma posición en todos los kits.

## Reglas técnicas
- **Hilo de audio:** sin reservas de memoria, sin bloqueos y sin E/S. La comunicación va por colas SPSC (`cmds`, `midi`, `ctlq`).
- **Núcleos:** cada tarea de `renderSource`/`renderSend` escribe solo en lo suyo. El hilo de audio coge tareas como uno más y nunca depende de que un ayudante llegue a tiempo.
- **Código de terceros que define macros cortas** (Open303, Plaits, MSFA): se compila en su propio `*_glue.cpp`, sin incluir sus cabeceras en el resto.
- **Estado:** `freon-estado.json`, versionado con `kStateVer`. Si cambian índices de parámetros, sube la versión y migra en `Control::load()`.
- **Entradas:** todos los valores que llegan (web, MIDI, archivo) se validan con `Control::rangeOf()`.
- **Sonido:** subnormales a cero en el hilo de audio, y los niveles de motores y kits igualados midiendo, no a oído.

## Lo siguiente (ver MEJORAS.md)
1. Que Jorge escuche lo nuevo: reverbs, delays, efectos de máster, combinaciones y Summing.
2. Tocar en la Pi 3 con MIDI e interfaz USB y ajustar el buffer. Ya compila y arranca sola (usuario `fugadefreon`, host `freonmachine`). Falta probar la Pi 5.
3. Modal → Rings/Elements (pendiente de que Jorge decida), saturación (Airwindows) y quizá quitar más motores.
4. Launch Control 3 en modo DAW: pantalla con el parámetro, el valor y una barra, y LEDs de verde a rojo (ver CONTROLADORAS.md). Lo decidirá Jorge más adelante.
