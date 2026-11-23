# Freon Machine · mejoras pendientes

## Batería 808: notas de Jorge (15-11-2026), hechas el 16-11-2026 (falta que las escuche)
- [x] Sin **Sweep**, sin **filtro** (Filter/Reso) y sin **Velocity**: cada golpe suena igual (como uno de 110, el nivel medido).
- [x] **Clap:** Pitch mueve su tono; Snap = fuerza de las manos frente a la sala (y de 2 a 5 palmadas). Decay: la cola es la sala en banda estrecha (antes era siseo y sonaba a hi-hat, 16-11), de 8 ms a 0,3 s.
- [x] **Hi-hats:** Pitch mueve también el filtro (ahora cambia el tono); Decay hasta unos 0,5 s cerrado y 3 s abierto (a la mitad, como antes).
- [x] **Un solo hi-hat:** cerrado y abierto son el mismo sonido con sus 2 notas; un solo Decay, y el abierto suena como si estuviera un 30 % más arriba (16-11).
- [x] **Snap:** por debajo del 30 % sigue ablandando el golpe (entrada de hasta 8 ms) en bombo, caja, rim y tom.
- [ ] Escucharlo con Jorge.

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
- [x] MC-24: cada fila controla un sinte (T2, T3, T4); Freon traduce por el nombre del puerto.
- [x] Web: cargar un preset ya no lanza una nota de prueba.
- [x] CPU en la Pi 3 (Oxi tocando): Juno 18 puntos, Summing 5, sidechain 2, kit FM 2, entradas 2, envíos 1–2. Juno apagado sin voces y 4 voces en la Pi 3; Summing a 0 y entradas 1-2 en mute.
- [x] Un LFO del Oxi manda ~150 CC/s: la web recibe solo el último valor cada 40 ms y el estado se guarda como mucho cada minuto.
- [ ] El contador de cortes no ve los fallos: el audio está abierto sin aviso de xrun.
- [x] Pi 3: buffer mínimo 256. Con 64 y 128 hubo cortes y el USB de la Clarett se bloqueó en el sistema (estado D en `usb_kill_urb`), que ni `kill -9` puede romper.
- [ ] Probar con buffer 256 mucho rato; si se repite el bloqueo, subir el mínimo a 512.
- [x] Batería igualada entre kits (`--niveles`): bombo −13, caja −15, clap −17, rim −20, tom −15, HH cerrado −21, HH abierto −18, perc −20 dB (ventana de 100 ms, ponderación K).
- [x] Prueba del cable USB de la Clarett (7-11-2026, Pi 3, buffer 512): desenchufada unos segundos, el audio se reabrió solo sin colgarse.
- [x] Prueba del cable USB del Oxi: Freon sigue viva, el audio no se para, y al volver regresan MIDI y reloj. El MC-24 ya no recibe MIDI de Freon, que lo atascaba.
- [x] Punto de partida: la mezcla de Jorge (7-11-2026): faders a 0 dB, nivel base de motores y Sub bajado, caja −3,7 dB, resto −7,5 dB, limitador −1,5, Summing a 0. Kit Chip −6 dB. Lo guardado conserva su sonido, salvo Game Boy (no tiene Volume, baja 6 dB) y el kit Chip.
- [x] Volume de partida por pista a oído: T2 DX 60, Wavetable 81, Supersaw 85; T3 Wavetable 80; T4 DX 63. Juno y Modal quedan bajos en mono, a propósito.
- [ ] Modal: Jorge probablemente lo quite; falta su valor en T2.
- [x] Sidechain por golpe: baja y sube en línea recta aunque el bombo siga sonando.
- [x] Sub: modo que sigue la nota grave de una pista, o su canal.
- [x] Mixer: faders y vúmetros a la misma altura, nombre debajo de cada fader.
- [ ] Configurar mejor las combinaciones de efectos de máster (pedido 7-11-2026).
- [ ] Arranque de la Pi más rápido (hoy unos 40 s) y wifi propia con ajustes sin terminal.
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
- [x] Motor **Phatty** (Slim Phatty) con el preset 5 de fábrica, GRINDMYBASS, sacado de su SysEx; nivel igualado al Moog (16-11-2026).
- [ ] Que Jorge compare GRINDMYBASS con su Slim Phatty.
- [x] Presets impares 1–15 del Slim Phatty en el Phatty (16-11). SYNC SEQ, SQUAREZ y COLD LEAD quedan 12–18 dB bajos: su filtro se cierra y el Volume ya está a tope.
- [x] Presets pares del Slim Phatty legibles (16-11): empiezan a mitad de byte y cada grupo de 18 bits va como [byte][byte][2 bits] en vez de [byte][2 bits][byte]. Comprobado con los 150 pares de los tres bancos. Huecos: presets 1–7 y 15 (fuera SYNC SEQ, SQUAREZ y COLD LEAD). DEEP AND DARK queda ~12 dB bajo (filtro muy cerrado, Volume a tope).
- [ ] Comprobar si en el Slim Phatty Mod Amount actúa sin rueda (aquí, con Mod wheel a 0, no hay modulación).

## Ideas de Jorge para más adelante
- [x] Compresor de bus en el máster (7-11-2026). Siguen: graves limpios y EQ de máster.
- [ ] Sonido final más cercano a un máster (decidido por Jorge el 7-11-2026). Muy importantes: compresor de bus en el máster, graves limpios (corte bajo 40 Hz, mono bajo 120 Hz, corte de graves en envíos) y EQ de máster sutil. Después: reverb que cede con el bombo, limitador con 1–2 dB de reducción y medidor LUFS. Sin saturación de cinta en el máster.
- [x] Summing quitado (7-11-2026).
- [x] Pantalla «System» en Settings: audio, buffer, frecuencia, núcleos, salidas, MIDI, apagar y reiniciar, con vuelta atrás (6-11-2026). Faltan red y latencia de cada interfaz.
- [x] Web: el scroll con dos dedos mueve la página; los knobs solo cambian arrastrando, o con ⌥ + scroll (5-11-2026).
- [x] Mixer: knob Volume del instrumento en Sub y sintes, aparte del fader. Game Boy no tiene Volume.
- [x] Sidechain rehecho: bombeo real, con subida lineal en Release (hasta 1 s). Antes solo bajaba 3,8 dB al 100 %.
- [x] Mixer: Volume junto al Limiter, enlazado con Instruments, y Trim en las tiras de batería.

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

## Home y uso (16-11-2026)
- [x] **Home:** tu punto de partida, la memoria «Home». Se guarda desde la web y se vuelve a ella con ⌂ Home (arriba, también en el móvil) o sin pantalla con el LPD8 (CC, pads 1 y 8 mantenidos 2 s, con luces). Probado en el Mac; en la Pi está pendiente, porque no respondía.
- [x] **Contador de uso** por parámetro (Settings › Most touched, `usos.json`), para elegir la primera vista de las controladoras.
- [x] (Ya existía) Batería en 3 canales (Kick, Snare, Drums rest) y entradas Input 1-4 como canales: el monotron va por una de ellas.
- [x] Grabación de lives, fase 1 (estéreo, «lo último», marcas, descarga): ver GRABACION.md. Probada en el Mac; falta 1 hora en la Pi.
- [x] Grabación: registro de la interpretación (live.freon), entradas y multipista (17-11). Ver GRABACION.md.
- [ ] Grabación: regenerar desde live.freon y exportar MIDI (Pro Tools/Ableton); streaming.

## Filtro ladder de código abierto (17-11-2026)
- [x] **Ladder «Improved»** (D'Angelo y Välimäki, ICASSP 2013; código de MoogLadders, licencia tipo ISC) como opción en el Moog y el Phatty (parámetro Ladder, grupo Filter). Classic sigue por defecto, así que nada cambia hasta elegirlo.
  - El original se desafina por encima de ~3 kHz a 96 kHz (integrador explícito). Ahora da 2 pasos por muestra y lleva una corrección de corte medida: autooscila en la nota pedida, con error < 1 % hasta 9,5 kHz; por encima se queda en ~10 kHz. Estable en un barrido forzado de 10 s.
  - Mismo nivel que Classic en todos los presets (±0,1 dB). La diferencia de sonido crece con la resonancia.
  - CPU en el Mac: ~2,8 % de un núcleo por voz (Classic ~1 %). **Pendiente medirlo en la Pi.**
- [ ] Que Jorge compare Classic e Improved a oído, sobre todo con resonancia alta; si gana Improved, pasa a ser el de fábrica.
- [ ] Calibrar las curvas del Phatty (corte, envolventes, resonancia) grabando su Slim Phatty real.

## Scarlett 4i4 (3.ª gen.) en la Pi (23-11-2026)
- [x] La Pi la reconoce como tarjeta de sonido nada más enchufarla (no hizo falta quitar «Easy Start»). Freon: máster en salidas 1-2, pre-escucha en 3-4, Input 1-4 = entradas 1-4 (la 4i4 da 6 canales de entrada: 4 físicos y 2 de loopback). Buffer 256 (5,3 ms), 48 kHz.
- [x] Prueba de 46 s con notas en las 4 pistas y multipista: 0 cortes, CPU media 5,3 % (pico 6,3 %), 40 °C.
- [ ] Jorge: comprobar qué oye por los auriculares de la 4i4 al pulsar cue (si siguen a 3-4 o a 1-2) y probar voz y monotron por las entradas.
- [ ] Probar buffer 128 (2,7 ms) con un live completo si hace falta menos latencia.

## Visuales: la nevera dentro de la Freon (23-11-2026)
Hecho (falta probarlo en la Pi con el proyector):
- [x] `ui/nevera.html` (la nevera v26 → v27), servida por la propia Freon en `http://<ip>:8080/nevera.html`. Se abre en cualquier navegador conectado a la Freon (el Mac por la wifi `Freon-Machine`, de momento) y va al proyector por HDMI. La página solo escucha: no manda órdenes.
- [x] La Freon le manda 60 veces por segundo el espectro del máster, calculado igual que lo medía el navegador (así el «oído» de la nevera, calibrado con lives reales, sirve tal cual y no hace falta tarjeta en el Mac), los golpes de batería y sampler, el reloj del Oxi y cada knob. Solo trabaja mientras hay una nevera conectada, y el hilo de audio solo copia el máster a un anillo (`src/vis.h`).
- [x] Arranque: el show lo da Jorge con Mayús+S desde el ordenador (le gusta que la nevera empiece a escribir antes del play). Con `?auto` la página se salta el formulario y deja la pantalla de espera; si se recarga a mitad, sigue en el mismo minuto.
- [x] Final automático: la subida de tempo del final (≥ 30 % sobre el tempo habitual o > 190 bpm, subiendo, pasado el 60 % del show) lanza el estrobo de despedida; el Stop del Oxi, el negro, el logo y el R.I.P. Un Stop sin subida no acaba nada: Jorge usa mucho el Stop como recurso.
- [x] Momentos grandes automáticos, siempre en el 1 del compás del Oxi: FU-GA-DE-FRE-ÓN, cantad conmigo, logo con estrobo (8 compases), logo de gas y cartelas SOMOS / FUGA / DE / FREÓN. Llegan tras los drops y, con la energía arriba, al empezar frases de 8 compases; cada vez más a menudo, sin repetir el mismo dos veces seguidas, y los que llevan el nombre solo después de que la nevera cuente de dónde sale. El coro automático del clímax ya no es una sola vez por show.
- [x] Mandos que se ven siempre (con o sin Oxi en marcha, desde MIDI o desde la web): el filtro de T1-T3 que suenan abre y cierra la imagen (cerrado, 45 % de brillo; abierto, 130 %) y la reso la hace vibrar; cada giro suelta gas o escarcha. Máster: paso bajo oscurece, paso alto deja líneas sueltas, Riser sube vapor y hace temblar, Space deja neblina, Delay dibuja un eco, Repeat tartamudea, Gater es estrobo, Drive sube el contraste. La tecla D enseña los mandos que llegan (23-11-2026: antes casi no se notaban y el cutoff de frecuencia se leía mal).
- [x] v29 (24-11-2026): de comedido a descontrol. Una curva de intensidad gobierna todas las reacciones nuevas (empieza al 10 % y crece hasta el 100 %): latido del fondo, zoom del bombo (de casi nada a 10 veces más), carámbanos (pocos y pequeños al principio), saltos de las letras, temblor del Riser, vibración de la reso, estrobo del Gater, chispas. Los momentos grandes y las escenas son cada vez más seguidos. En el último 15 % (DESCONTROL): fallos y fugas nuevas en cada compás, momentos cada 4 compases, las escenas se mezclan con cuchillos y cubitos. En el final (subida de tempo), el ASCII se ve detrás de las cartelas y se desmadra con el tempo: explosiones, cuchillos y cubitos en cada tiempo.
  - Arreglos: durante una escena ya no se gastan frases ni salen fotos; las frases de la historia no se rompen hasta la venganza; la lluvia de neveras sale desde el acto 3.
- [x] v28 (24-11-2026), tras la revisión con Jorge:
  - La reso ya no hace vibrar la pantalla por estar alta (la 303 y el Moog suelen ir así): vibra al *moverla*. El filtro de T1-T3 cambia el brillo entre 70 % y 125 %, y moverlo da un destello.
  - Las frases largas se encogen hasta caber (antes se cortaba su parte de arriba, p. ej. «Todavía tengo la cicatriz…» en el acto 4).
  - Letras vivas: la frase del Terminal salta con el bombo, ondula al mover la reso, tiembla y sube con el Riser, se descuelga con el Tape stop, se congela con el Freeze y cambia letras en los fallos; la caja arranca letras sueltas (actos 3-4). En los actos 3 y 4, en un drop o con la energía arriba, la frase se rompe y cae al llegar la siguiente.
  - Escenas sin Terminal (8 compases, a tempo; las lanza el director o la tecla V): lluvia de neveras que se apilan y se abren con el bombo; FUGA DE FREÓN en letras gigantes que caen sílaba a sílaba; cuchillos contra cubitos con marcador; y, en la venganza, la nevera persiguiendo a la banda (KAKS, JASJ, PHIBS, MOX, MIKI) a saltos y tirando cuchillos.
  - Más MIDI: cada nota de T1-T3 es un carámbano (o un cuchillo, tras la puñalada) en su altura, de grave a agudo; los toms hacen ondas; rim y perc, chispas; cada corte del break desplaza una franja; cada grano del granular, una chispa; el bombo da un golpe de zoom; si se tocan muchos mandos, chispas y algún comentario. La Freon manda ahora la última nota de cada sinte (hace falta reiniciarla una vez).
- [x] ASCII a tempo: cuchillos y copos aparecen en los tiempos, cubitos y gas en corcheas, el fondo y la nevera grande laten en cada tiempo, la escarcha brilla con los charles, cada 4 compases la imagen falla un instante y cada 16 cambian las fugas.
- [x] Reacciones: bombo y caja reales (no adivinados), cutoff y reso de los sintes (sobre todo la 303) abren la fuga de gas, Freeze y granular congelado congelan la imagen, Tape stop la frena, Riser cuenta como subida, Repeat hace fallar la imagen a tempo, Gater es un estrobo a semicorcheas, el filtro del máster apaga la imagen, los saltos del break la hacen fallar y el sampler suelta cubitos. La nevera comenta los cambios de motor y de kit, mutes y solos, y el Stop y el Start del Oxi.
- [ ] Probar en la Pi y con el proyector, y que Jorge la vea con un live grabado (reproducir `live.freon` mueve la nevera igual que en directo).
- [ ] Los datos del evento (sala, ciudad, nombres del público) se escriben en la pantalla de inicio de la nevera. Para una pantalla sin teclado, pasarlos a la web de la Freon.
- [ ] Voz: de momento sin reacción (Jorge no lo ve claro).

Ideas para más adelante (de Jorge, 23-11-2026):
- **Dibujar en la propia Pi** (Chromium en modo quiosco por el HDMI de la Pi, para llevar menos aparatos). Antes, medir con `--estres` que Chromium dibujando a pantalla completa no le quita tiempo al audio, y quizá limitarlo a 1-2 núcleos y a 1080p.
- **Que la nevera suene, como un canal más del mezclador.** Por plantear: su voz (síntesis de voz sin internet en la Pi, tipo espeak o Piper, leyendo sus frases), el zumbido del compresor, el tecleo del Terminal, o pitidos de electrodoméstico afinados a la tonalidad. Iría por su propio canal, con fader y mute.
- **Hablarle a la nevera** por un micro (una entrada de la Scarlett). Reconocer palabras sin internet en la Pi gasta mucha CPU (whisper.cpp): habría que medirlo, o limitarlo a unas pocas palabras clave.
