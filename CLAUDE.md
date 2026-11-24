# Freon Machine — contexto para Claude Code

Freon Machine es la caja de directo de Jorge Alarcón, músico electrónico y editor de diálogos. Es un motor de sonido en C++ (sintes, batería, sampler, granular, voz, mezcla y efectos) que secuencia un **Oxi One** por MIDI. El destino final es una **Raspberry Pi 5 sin pantalla**: todo se toca desde controladoras MIDI con LEDs, y la web (`ui/index.html`) sirve solo para configurar desde el móvil o el portátil. Se desarrolla en un MacBook Air (Apple Silicon) y se toca en una **Raspberry Pi 5** (8 GB, SD de 64 GB).

## Regla fundamental: es para tocar en directo
Freon Machine tiene que ser **eficiente y muy estable**. Esto pesa más que cualquier función nueva: nada puede cortar el audio, colgarse ni perder datos en el escenario. Ante la duda, gana la opción más segura y que gaste menos CPU; si una mejora pone en riesgo la estabilidad, no entra. Jorge deja en manos de Claude las decisiones técnicas para conseguirlo (por ejemplo, cómo repartir el trabajo entre núcleos).

## Cómo trabajar con Jorge
- Escribe siempre en **español**, claro y sin jerga. Jorge no es programador, pero sí un profesional del sonido con muy buen oído.
- Cada versión nueva va con los comandos para compilar y arrancar. En este proyecto son `./build.sh` y luego `./bin/freon-machine --entrada <n>`.
- Lo primero son la **estabilidad** (es para tocar en directo) y la **calidad de sonido**. Para los núcleos de sonido se prefiere código probado (Mutable Instruments, Open303, KR-106, MSFA, Airwindows…) a escribir uno propio.
- Le gusta lo simple: cada cosa hace una función. No añadas funciones que no ha pedido.
- Lo pendiente y lo decidido están en `MEJORAS.md`. El reparto de controladoras está en `CONTROLADORAS.md`, la grabación de lives en `GRABACION.md` y la documentación de uso en `LEEME.md`. Mantenlos al día.
- La interfaz web está en inglés; las respuestas a Jorge, en español.

## Copias
- Código en GitHub, repositorio privado `reppohw/freonmachine`, rama `main`. Sube cada versión probada, con un mensaje claro. No van al repositorio `bin/`, `freon-estado*.json` ni los WAV de `samples/` y `breaks/`.
- La Pi recibe una copia del código y lo compila allí; su estado no está en GitHub.

## Hardware en uso
- **Pi 5:** usuario `fugadefreon`, en la red de casa normalmente en `192.168.1.137` (a veces `.106` por cable). Sin internet propio: si Jorge está fuera, la Pi crea su wifi `Freon-Machine` (10.42.0.1).
  - SSH con la clave `~/.ssh/freon_pi`: `ssh -i ~/.ssh/freon_pi -o IdentitiesOnly=yes -o HostKeyAlias=192.168.1.137 fugadefreon@<ip>`.
  - Web en `http://<ip>:8080`.
  - Servicio `freon-machine` (systemd): se reinicia solo al matar el proceso (`pkill -x freon-machine`).
- **Interfaz:** Focusrite **Scarlett 4i4** (3.ª gen.) desde el 23-11-2026. Máster en salidas 1-2, pre-escucha en 3-4, Input 1-4 = entradas 1-4, buffer 256 (5,3 ms). Los ajustes de sistema están en `freon-sistema.txt` de la Pi y se cambian desde System en la web.
- **MIDI:** Oxi One (secuenciador), LPD8 mk2 (sampler, breaks, granular, máster) y Nakedboards MC-24. La Launch Control XL 3 y la BCR32 son ideas, aún sin comprar.
- El retraso de salida es buffer × bloques (ajuste «Safety blocks» en System).

## Desplegar en la Pi
1. `rsync -a src ui` a `~/freon-machine/` de la Pi.
2. Compilar allí: `./build.sh`, unos 90 s, o más con `nice` mientras suena. Compilar no afecta a lo que está sonando.
3. **Reiniciar Freon solo con el permiso de Jorge.** Puede estar tocando o grabando. Si solo cambia `ui/`, basta con recargar la página.
4. Antes de reiniciar, mirar que no haya una toma abierta: `journalctl -u freon-machine | grep Grabación`.
5. Las pruebas que suenan o graban en la Pi se avisan, y sus tomas de prueba se borran después.

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
  - especificaciones de los 9 motores (`kEng`), presets de fábrica y capa de 16 knobs (`kLayer`). La pantalla ofrece Juno, DX7, Moog, 303, Game Boy y Phatty; Wavetable, SuperSaw y Modal están ocultos (`kEngHidden`) y los envíos C y D también (`kSendsShown`);
  - `MeloEngines`, que contiene los motores de una pista;
  - la web se construye a partir de aquí.
- Motores:
  - `juno.h` (KR-106, modo Juno-6);
  - `dx.h` + `dx7_glue.*` (MSFA);
  - `moog.h` y `phatty.h` (ladder de Huovilainen, o «Improved» de D'Angelo-Välimäki en `dsp.h`, a elegir con el parámetro Ladder);
  - `voice303.h` + `open303_glue.*` (Open303);
  - `gameboy.h`;
  - `synths2.h` (Wavetable, Supersaw, Modal, y el código ya sin uso de West Coast y Organ).
- `src/drums.h` + `plaits_glue.*`: kits 808/909 (Plaits), FM al estilo Model:Cycles (propio), Chip y Experimental.
- `src/live.h`, `src/voice.h`: sampler, breaks (igualados a −14 LUFS al cargar), granular y voz.
- `src/recorder.h`: grabación de lives (máster, multipista, `live.freon`, «lo último») y piezas de la reproducción. El reproductor, la corrección y el render están en `Control` (`rp*`). Ver `GRABACION.md`. `src/fx.h`: mezcla, envíos (FDN, plate de Dattorro, delays), efectos de máster y suma analógica. `src/par.h`: reparto del audio entre núcleos. `src/server.h`: HTTP + WebSocket. `src/vis.h`: análisis del máster para los visuales (`ui/nevera.html`, la nevera de Fuga de Freón): solo con una nevera conectada («vis 1»); el hilo de audio solo copia el máster al anillo `vring`. La nevera de la Pi (`nevera.html?auto&pi`, servicio `freon-nevera`, `deploy/nevera-hdmi.sh`) se maneja desde Settings › Visuals: la web manda «nev …» y la Freon lo pasa a las neveras; ella devuelve «nevst {…}».
- `third_party/`: RtAudio, RtMidi, `open303/`, `kr106/` (GPL v3), `mi/` (Plaits + stmlib, MIT) y `msfa/` (Apache 2.0). Los cambios propios están marcados con «Freon Machine».

## Nombres en pantalla
- Las pistas se ven como en el Oxi de Jorge: los sintes son **T1-T3** (por dentro `t2`-`t4`) y la batería **T4** (por dentro `t1`). Solo cambian los nombres en pantalla; las claves internas y el estado no cambian.

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
1. Que Jorge escuche:
   - Classic contra Improved, para decidir el filtro por defecto;
   - los auriculares y las entradas de la Scarlett;
   - el gesto de Home en el LPD8.
2. Sesión de escucha de baterías: quedarse con 2 o 3 kits y reducir sus parámetros, al estilo de la 808.
3. PIN para la web, para borrar y apagar (pendiente de que Jorge diga sí).
4. Launch Control XL 3 cuando la compre: mezclador en primera vista y Rec en un botón (nota 56 del canal del mezclador).
5. Grabación: exportar MIDI para Pro Tools y Ableton; render más rápido que el tiempo real si hace falta.
