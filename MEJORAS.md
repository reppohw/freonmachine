# Freon Machine · mejoras pendientes

## Hecho el 5-11-2026 (tras la primera prueba de Jorge)
- [x] Pickup: el knob ya no se suelta al girar rápido; cada controladora lleva su posición. Relativo: si la controladora manda absoluto, se ignora y avisa.
- [x] Mutes de la batería quitados (web y CC 102–109). Orden de knobs igual en todos los kits: Level, Pitch, Decay, timbre, ataque, Sweep, forma, Velocity.
- [x] Hi-hats 808/909 y ride 909 con voz propia, claps rehechos (palmadas + cola). Kit FM con parámetros claros: FM, Ratio, Sweep, FM decay, Noise.
- [x] Tempo del Oxi estable: ventana de hasta 8 tiempos y reacción a cambios en medio tiempo.
- [x] 8 combinaciones de efectos de máster.
- [x] Sub (y sintes): se sueltan las notas con Stop del Oxi y al perder el foco la ventana.
- [x] Filter y Reso contiguos en todos los kits (knobs 7 y 8). Regla fija.
- [x] DX7 bajado 5 dB, medido contra el Juno.
- [x] Tabla PWM del Wavetable: estaba entera rota (NaN) y dejaba mudos sus sonidos.
- [ ] Revisar niveles entre presets: Modal Bell, Marimba y Bowed plate salen muy altos; Juno Bass y Pluck, bajos. Pendiente de que Jorge decida.
- [x] Mezclador de partida limpio: envíos bajados, sin grupos, todo al máster, sin distorsión, EQ ni sidechain.
- [x] Combinaciones editables en vivo; crossfader corto Dry–Wet (CC 95). Las escenas A/B se han quitado (decisión del 5-11-2026: el crossfader va de seco a efecto).
- [x] Tempo del Oxi: medido el reloj real (pulsos de 0,1 a 39 ms) y cambiado el cálculo a una recta sobre hasta 8 tiempos; antes un pulso pegado borraba la medida y parecía que no leía.
- [x] Instrumentos para el Oxi (.oxiindef y CSV) generados desde el código; CC 9 motor o kit, CC 10 preset; el Oxi siempre en absoluto.
- [ ] Comprobar que la OXI App importa los instrumentos y que los nombres caben en su pantalla.
- [x] 303: al arrancar, los parámetros en su valor por defecto (accent, resonancia, env, decay...) no llegaban a Open303 hasta mover el knob. Corregido y en la Pi.
- [ ] 303: tras esa corrección suena más brillante. Escucharlo.
- [x] Pi 3: se frenaba por temperatura (79,5 °C, throttled=0x20000). Los ayudantes esperan en bajo consumo y sueltan los núcleos tras los envíos; reverbs más ligeras.
- [ ] Pi 3: con Juno, 303, Moog y 4 envíos va al 54–68 %. Un solo motor no se puede repartir entre núcleos: si hace falta, limitar voces del Juno.
- [x] Igualados motores y presets de fábrica a −22 dB medidos (`--niveles`); los presets de usuario no se tocan. Los sonidos muy cortos quedan por debajo, porque Volume no pasa de 1.
- [x] Limitador del máster con 1,5 ms de anticipación. Fuzz con antiderivada: 10–14 dB menos de aliasing.
- [x] De la auditoría: guardado cada 10 s como máximo con CC continuos, clave dañada no impide arrancar, limpieza completa tras un NaN, rejilla ancha en ráfagas MIDI, Moog sin cálculos repetidos.
- [x] MC-24: fila de arriba = 7 primeros knobs de la capa y crossfader. Motor y preset pasan de CC 9/10 a CC 3/4 (chocaban con el MC-24); hay que reimportar los instrumentos del Oxi.
- [ ] MC-24: decidir cómo elegir sinte, porque solo manda por un canal.
- [x] Web: cargar un preset ya no lanza una nota de prueba.
- [x] CPU en la Pi 3 (Oxi tocando): Juno 18 puntos, Summing 5, sidechain 2, kit FM 2, entradas 2, envíos 1–2. Juno apagado sin voces y 4 voces en la Pi 3; Summing a 0 y entradas 1-2 en mute.
- [x] Un LFO del Oxi manda ~150 CC/s: la web recibe solo el último valor cada 40 ms y el estado se guarda como mucho cada minuto.
- [ ] El contador de cortes no ve los fallos: el audio está abierto sin aviso de xrun.
- [ ] Sub colgado: Jorge tiene que confirmar qué pasó exactamente y si vuelve a ocurrir.
- [ ] Saber qué controladoras usa Jorge, para elegir el modo de knobs por defecto.

## Probar con Jorge (4-11-2026, tercer paquete)
- [ ] Escuchar las reverbs y delays nuevos, los efectos de máster, Comb, Sweep, combinaciones y Summing. Probado por medidas, no a oído.
- [x] Probado en Pi 3 Model B v1.2 (Raspberry Pi OS Lite 64 bits, 5-11-2026): compila sin avisos en unos 5,5 min, 4 núcleos con tiempo real y arranque automático. Render típico: 45 % con 1 núcleo, 22 % con 4, idéntico bit a bit.
- [ ] Pi 3, peor caso (18 voces de Juno y todo moviéndose): 117 % de media con 4 núcleos, no cabe. Hay que limitar voces en ella; la prueba va bloque tras bloque y el freno de tiempo real del sistema la distorsiona, así que sus picos no valen.
- [x] Linux: Freon abría sus propios puertos MIDI y se multiplicaban sin parar (memoria y CPU). Corregido; el jack de la Pi se abre en estéreo; los cortes de audio quedan en el registro.
- [x] Sonido sucio tipo bitcrusher en la Pi 3: desapareció al corregir lo anterior (Jorge lo confirmó el 5-11-2026). No se aisló si venía de los puertos MIDI o de los 8 canales.
- [ ] Pi 3: seguir tocándola con el Oxi, probar una interfaz USB y ajustar el buffer (hoy 512).
- [ ] Pi 3: 69 °C sin tocar. Vigilar en sesiones largas; poner disipador o reducir la espera activa de los núcleos si sube.
- [ ] Decidir el modo de los faders: el crossfader Dry–Wet por CC 95 usa recogida, como los knobs. Puede que un fader físico pida ir directo.

## Por revisar con Jorge
- [ ] Tabla A–D de los 10 motores (al final de LEEME.md y en cada motor de la web): confirmar o cambiar.
- [ ] Mapa MIDI de controladoras (LEEME.md, «Controladoras»): está pensado para 16 knobs por instrumento y un mezclador en el canal 16; ajustarlo al hardware que se elija.
- [ ] Batería en la controladora: hoy son 64 CC (8 knobs × 8 sonidos). Con una controladora de 16 knobs harían falta páginas (por ejemplo, 2 parámetros por sonido y página); decidir cuando haya hardware.
- [ ] Niveles entre motores: igualados midiendo una nota sostenida. Escuchar si algún motor queda alto o bajo en la práctica.

## Hardware de control (por decidir)
- [ ] Plan de Jorge: 4 controladoras de 16 knobs (batería y las 3 pistas de sinte), 1 para sampler, granular, voz y efectos de máster, y 1 de mezclador con 3–4 knobs por canal más faders.
- [ ] Opciones barajadas: Launch Control (comercial, un LED por knob), Intech Grid (EN16, BU16, PBF4, EF44: modulares, sin soldar, un LED RGB por encoder, programables) o construcción propia (Raspberry Pi Pico + encoders con anillo de LEDs y placa propia encargada montada).
- [ ] Siguiente paso propuesto: probar un solo EN16 de Intech, o un prototipo de 4 encoders con anillo en protoboard, antes de comprar el resto.

## Página 1 de los sintes sin huecos (pedido 4-11-2026; hecho salvo Modal, que solo tiene 12 parámetros)
- [ ] Regla: ningún knob vacío en la página 1. Si un motor no tiene el equivalente de un hueco (filtro, envolvente…), se rellena con otro parámetro suyo, aunque rompa el orden común.
- [ ] Game Boy: hoy tiene 7 huecos (Reso, Env y casi todas las envolventes). Rellenar con duty, wavetable, sweep, arpegio, velocidad del arpegio, pan…
- [ ] West Coast: quitar un parámetro y meter todos los demás en la página 1 (17 → 16). Candidato a quitar: Mod fine (por confirmar).
- [ ] Modal: todo en la página 1 (12 parámetros, incluidos Model y Exciter).
- [ ] DX: capa propia de FM (opción 2, elegida por Jorge). Arriba: Ratio 1–4, Level 2–4 y Feedback. Abajo: Mod A/D/S, Volume y Amp A/D/S/R. Página 2: Detune y Velocity. Algoritmo en los botones.
- [ ] Reparto de las Launch Control 3 de la sección de instrumentos: ver CONTROLADORAS.md (propuesta pendiente de revisar con Jorge).

## Sintes
- [ ] Preset 5 de fábrica del Slim Phatty (pendiente de que Jorge pase los valores o una grabación).

## Ideas de Jorge para más adelante
- [x] Pantalla «System» en Settings: audio, buffer, frecuencia, núcleos, salidas, MIDI, apagar y reiniciar, con vuelta atrás (6-11-2026). Faltan red y latencia de cada interfaz.
- [x] Web: el scroll con dos dedos mueve la página; los knobs solo cambian arrastrando, o con ⌥ + scroll (5-11-2026).
- [ ] Mezclador: en la tira de cada sinte, un knob que sea el Volume del propio motor que tenga la pista (no un Gain nuevo), aparte del fader (pedido 5-11-2026). Así se guarda con el preset. Al cambiar de motor debe pasar a mandar el Volume del motor nuevo.

## Ideas para más adelante (de la auditoría)
- [ ] CC de 14 bits o NRPN para cutoff y niveles (más resolución que 128 pasos).
- [ ] Parámetros con nombre en vez de número dentro del estado guardado (añadir o reordenar parámetros sin perder nada).
- [ ] Probar en la Pi 5 real: consumo, latencia con la interfaz elegida y arranque automático.

## Sonido (en marcha)
- [x] Juno, Moog y 303 con núcleos probados (KR-106 en modo Juno-6, Huovilainen y Open303). Pendiente: que Jorge los compare a ciegas con sus aparatos.
- [x] Batería: 808 y 909 con Plaits, kit FM al estilo Model:Cycles (Color, Shape, Sweep, Contour), Chip y Experimental.
- [x] Fuera West Coast y Organ.
- [x] DX7 de verdad (MSFA): biblioteca con 8 sonidos de serie y bancos .syx de la carpeta dx7/, 16 knobs que retocan el sonido.
- [x] Efectos de máster al estilo Octatrack: varios parámetros por efecto, Comb, Sweep, 8 combinaciones y crossfader Dry–Wet.
- [x] Suma analógica (knob Summing).
- [x] Reverbs (Dattorro y FDN con difusión) y delays estéreo.
- [ ] Modal → Rings / Elements de Mutable (pendiente de que Jorge decida si se queda).
- [ ] Batería muestreada de la DRM de Jorge (más adelante).
- [ ] Saturación (Airwindows), Wavetable y Supersaw (Surge XT).
- [ ] Mezcla más sencilla: quitar el sidechain y lo que no se use.
- [x] CPU: reparto entre núcleos (`src/par.h`, `--nucleos`, `--estres`). Falta medirlo en la Pi.
- [ ] Juno: mostrar el Cutoff en Hz (curva del Juno-6) en la pantalla del controlador.

## Resuelto (4-11-2026, segundo paquete)
- [x] Nombre nuevo: Freon Machine (recoge sola el estado del groovebox de la carpeta de al lado).
- [x] Mute del bombo que se quedaba fijo: los botones ahora funcionan igual si la controladora los manda como interruptor (127 y luego 0) o como pulsador. Pulsación corta = se queda; mantener = solo mientras se mantiene. Además, se ignora lo que vuelve por un bucle MIDI.
- [x] Solos en modo latch (se suman) o X-OR (exclusivo): botón en Mixer y nota 62 del canal del mezclador.
- [x] 303: Tune continuo de −3 a +3 octavas.
- [x] Juno: HPF continuo, como el deslizador del Juno-6.
- [x] Continuos: Pitch de la batería, Ratio 1–4 del DX, Mod ratio del West Coast, Tune de breaks y granular, Bits de sampler y breaks.
- [x] Pantalla en el orden de la controladora: capa de 16 knobs en dos filas de 8 con su CC, luego la segunda página y los botones. Batería: columnas por CC en el orden de la pantalla.
- [x] Sub como módulo aparte con su propio canal MIDI (9 por defecto) para la nota grave del secuenciador de acordes del Oxi.
- [x] EQ del mezclador en orden High, Mid, Low.

## Resuelto (4-11-2026, tras la auditoría)
- [x] Sin pantalla: todo se controla por MIDI (motor, kit, presets y su guardado con shift, snapshots, pad, grabación, breaks, granular, voz, efectos de máster, mutes, solos, cue, grupos, apagado).
- [x] MIDI de vuelta a las controladoras: valores, botones y luces de estado; todo al conectar una controladora; al Oxi no se le devuelve nada; sin eco hacia el potenciómetro que se está moviendo; máximo ~3000 mensajes/s.
- [x] Knobs en modo recogida (pickup), relativo (dos variantes) o absoluto.
- [x] Capa de 16 knobs en los 10 motores (arriba Cutoff, Reso, Env, A–D, Volume; abajo ADSR de filtro y de amplitud); el resto en CC 40–63.
- [x] Batería: Pitch, Decay, Snap, Filter (paso bajo / paso alto), Reso, Sweep, Level, Velocity y Pan; sin Drive ni Tone. Kit FM con Ratio, FM amount, FM decay, Feedback y Noise.
- [x] Velocidad: unos 20 dB entre tocar flojo y fuerte; abre el filtro en los motores que lo tienen; knob propio en el 303; en la batería cambia volumen, golpe, brillo y caída de tono.
- [x] Cambio de motor: sin sonido de prueba; las notas del motor anterior se apagan en 10 ms y no resucitan.
- [x] Repeat del máster: Amount = mezcla seco/repetición; segundo knob = tamaño (1 compás a 1/32); sin clic en cada vuelta.
- [x] Críticos de la auditoría: MIDI que vuelve tras desconectar un cable; valores imposibles rechazados (probado con 60 000 mensajes al azar); guardado seguro con copia .bak; un móvil dormido ya no bloquea nada; el audio se reabre solo si la interfaz se cae.
- [x] Interfaz de audio por nombre (`--audio "Scarlett"`), espera al arrancar (`--esperar-audio`), prioridad de tiempo real y memoria bloqueada en la Pi, servicio de arranque e instalación (`deploy/`).
- [x] Clics: faders, pan y envíos con rampa; redisparo de la batería y robo de voces del sampler con fundido; filtro DJ al cruzar el centro; delays de cinta que se deslizan al cambiar el tiempo.
- [x] Reloj MIDI exacto tras Start y Song Position; notas MIDI colocadas en su muestra exacta dentro del búfer.
- [x] Menos consumo: envolventes de la batería sin exp() por muestra, Modal y Voz más ligeros.
- [x] El kit DRM ya no aparece (confirmado en el código; lo que se veía era el programa anterior, que seguía abierto).
