# Auditoría de Freon Machine para el directo

Fecha: 10-11-2026. Sustituye a la auditoría del 4-11-2026 (sigue en el historial de GitHub). La lista breve de noviembre está en `AUDITORIA-2026-11.md`. Casi todo lo que señalaba ya está arreglado.

**Alcance:**
- Código propio de `src/`, la web (`server.h`) y los scripts de `deploy/`.
- La Raspberry Pi 3, solo leyendo: registro, temperatura, tensión, USB, disco y carga de Freon.
- No se ha cambiado código ni ningún ajuste de la Pi.

**Orden de prioridades:** estabilidad, calidad de sonido, eficiencia en la Pi 3 y seguridad.

**Cómo leer cada hallazgo:**
- **Medido:** lo he visto ocurrir, en el Mac, en la Pi o con un programa de prueba.
- **Comprobado en el código:** se deduce sin dudas leyendo las líneas citadas, pero no lo he visto ocurrir.
- **Sospecha:** depende de algo que no he podido comprobar.

**Esfuerzo del arreglo:**
- *mínimo:* una línea;
- *bajo:* una tarde;
- *medio:* un día con pruebas;
- *alto:* varios días.

---

## Resumen: los 5 hallazgos más importantes

| N.º | Hallazgo | Gravedad | Cómo se sabe |
|---|---|---|---|
| E1 | La corriente y el USB de la Pi fallan: tensión baja y caída de los tres aparatos USB a la vez | Crítico | Medido en el registro |
| E2 | Un cambio de wifi (o una caída y vuelta de la wifi) corta el audio: Freon lo reabre sola | Importante | Medido en el registro; el resto, comprobado en el código |
| E3 | Los pads del LPD8 y el Oxi escriben a la vez en la misma cola de MIDI: se pueden perder notas o repetirse notas viejas | Importante | Medido con una prueba aislada; comprobado en el código |
| C1 | El máster satura siempre un poco: 4,6 % de distorsión en los picos y nunca pasa de −3,1 dBFS | Importante | Medido |
| E4 | La web no avisa de tensión baja ni de cortes de audio: los dos indicadores están ciegos | Importante | Medido en la Pi |

La base es sólida:
- El hilo de audio no reserva memoria, no se bloquea y no lee ni escribe archivos.
- Las protecciones contra valores imposibles cubren cada pista, los envíos y el máster.
- El estado se guarda de forma segura.
- Las pruebas de estrés con los detectores de fallos de memoria (ASan y UBSan) y de carreras entre núcleos (ThreadSanitizer) salen limpias.

Los riesgos reales para un concierto están en:
- la corriente y el USB de la Pi;
- la wifi;
- el camino de los pads del LPD8.

---

## Qué he medido

**En el Mac** (`./build.sh`, versiones debug y tsan):

| Prueba | Resultado |
|---|---|
| Compilación | Un aviso en código propio: `src/freon-machine.cpp:2277` («`due` no se usa», solo en el Mac). Los demás avisos son de `third_party/`. |
| `--render` con 1 y 4 núcleos | WAV idénticos bit a bit. Consumo medio: 5,3 % (1 núcleo) y 3,6 % (4 núcleos). |
| `--niveles` | Ningún `nan`. Niveles en la tabla de C2. |
| `--estres 30` normal | Muestras rotas: 0. Salida máxima: 0,699. |
| `--estres 30` con ASan y UBSan | Sin errores. Muestras rotas: 0. |
| `--estres 30` con ThreadSanitizer | 0 avisos. Esta prueba no pasa por los hilos MIDI, así que no podía ver E3. |
| Prueba aislada de la cola de MIDI (`Spsc`), copia exacta | **1 escritor:** 2 000 000 enviados y 2 000 000 recibidos. **2 escritores a la vez:** 4 000 000 enviados y entre 5,3 y 6,8 millones recibidos. La cola repite mensajes viejos. |
| Distorsión de `softClip` (seno de 1 kHz) | −12 dBFS: 0,46 %. −6 dBFS: 1,8 %. −1,5 dBFS: 4,6 %. 0 dBFS: 6,2 %. |
| Limitador del máster a −1,5 dB, con golpes de +32 dB | Pico de salida exactamente en el umbral (+0,00 dB). |
| Air del compresor de bus | +4,7 dB a 12 kHz al 50 % y +9,3 dB al 100 %. |

**En la Pi 3** (Raspberry Pi OS de 64 bits, núcleo 6.18 con PREEMPT, gobernador en *performance*, 1,2 GHz):

| Dato | Valor |
|---|---|
| Temperatura | 45–48 °C, sin música |
| `get_throttled` desde este arranque | 0x0 (sin avisos) |
| Arranque anterior | «Undervoltage detected!», tres aparatos USB desconectados en 7 s y 3 595 errores `urb status -32`, hasta 1 024 por segundo |
| Disco | 2,8 GB usados de 59 (5 %) |
| Memoria libre | 658 MB |
| Freon ahora | Sin la Clarett conectada, suena por la salida de auriculares de la Pi (la de reserva). Buffer 512, 4 núcleos. |
| CPU de Freon sin tocar nada | 10,8–11,9 %, pico 12–14 %, 0 cortes contados |
| CPU con música (dato previo, `MEJORAS.md`) | 54–68 % |
| Puertos abiertos | 22 (SSH) y 8080 (web), los dos para toda la red |

---

## 1. Estabilidad

### E1. La corriente y el USB de la Pi no aguantan
- **Qué pasa:** al final del arranque anterior, el registro muestra en 7 segundos:
  - la caída de los tres aparatos USB (1-1.3, 1-1.4 y 1-1.5);
  - más de 3 500 errores `urb status -32`, hasta 1 024 por segundo;
  - y el aviso del sistema «Undervoltage detected!».

  Es lo que pasa cuando la Pi no recibe corriente suficiente para todo lo que alimenta por USB. Encaja con lo que ya sabías (falta de corriente con 4 aparatos y el MC-24 colgado con `urb status -32`).
- **En el concierto:**
  - la interfaz se desconecta y el audio se corta o suena «crusheado»;
  - el MC-24 o el Oxi dejan de responder;
  - en el peor caso, la tarjeta SD se corrompe si la tensión cae del todo.

  Freon reabre el audio sola, pero eso tarda segundos.
- **Gravedad:** crítico.
- **Esfuerzo:** hardware, no código.
  - Hub USB alimentado para la Clarett, el MC-24 y el LPD8.
  - Fuente oficial de 5,1 V y 2,5 A para la Pi.
  - Cables USB cortos.
- **Dónde:** registro del sistema (`journalctl -b -1 -k`). En el código no hay nada que arreglar, salvo que la web lo muestre (E4).
- **Cómo se sabe:** medido.

### E2. Un cambio de wifi corta el audio, aunque no haga falta
- **Qué pasa:** Freon vigila la dirección de la wifi cada 2 s. Si cambia, reabre el audio 4 s después. La idea es arreglar el sonido «crusheado» que aparecía al cambiar de wifi. Pero esta comprobación salta con más cosas de las que debería:
  - **Al arrancar:** en el arranque de hoy reabrió el audio a los 37 s, solo porque la wifi se conectó (medido en el registro: «Reabro el audio (aviso de cambio de wifi)»).
  - **Si la wifi se cae y vuelve**, aunque sea con la misma dirección. Al caerse, la dirección queda vacía; al volver, cuenta como cambio.
  - **En modo «casa», en una sala sin tu wifi:** a los 90 s del arranque la Pi crea su propia wifi, cambia la dirección y el audio se reabre.
  - **Además**, `freon-wifi` manda su propio aviso (SIGUSR1) al cambiar de red.
- **En el concierto:** un corte de audio de un instante (en la Pi, unos 60 ms con la salida de auriculares; con la Clarett, más). Puede llegar en mitad de un tema, sin tocar nada, si la wifi de la sala va y viene.
- **Gravedad:** importante.
- **Esfuerzo:** bajo. Reabrir solo cuando el cambio lo pide `freon-wifi` (ese aviso ya existe), y no por cualquier cambio de dirección. Alternativa: un ajuste en la web para apagar esta vigilancia durante el concierto.
- **Dónde:** `src/freon-machine.cpp:2277-2286` (vigilancia de la dirección), `:1606` (reapertura) y `:2287` (`onReopen`).
- **Cómo se sabe:** la reapertura al arrancar está medida en el registro. Lo de la caída y vuelta de la wifi está comprobado en el código.
- **Mientras tanto:** antes del concierto, elige «propia» en Ajustes → Network. La Pi crea su propia wifi desde el arranque y ya no cambia.

### E3. Dos escritores en la cola de MIDI: notas perdidas o repetidas
- **Qué pasa:** la cola que lleva las notas al audio está hecha para un solo escritor. Sin embargo, escriben en ella dos hilos a la vez:
  - el que recibe el MIDI del Oxi y del MC-24;
  - el hilo de control, que traduce los pads del LPD8 (sampler y granular) y los mensajes MIDI de prueba de la web.

  He copiado la cola tal cual y la he probado con dos escritores. Mensajes enviados: 4 millones. Mensajes recibidos: entre 5,3 y 6,8 millones, porque la cola repite mensajes viejos. Con un solo escritor no falla nunca.
- **En el concierto:** muy de vez en cuando, si pulsas un pad del LPD8 justo cuando llega MIDI del Oxi (el reloj llega 48 veces por segundo):
  - una nota que no suena o que se queda colgada;
  - o, en el peor caso, una ráfaga de miles de notas antiguas: golpes de batería, acordes y pads, todo de golpe.
- **Gravedad:** importante. Es raro, pero si pasa es grave.
- **Esfuerzo:** bajo. Que la traducción de los pads del LPD8 se haga donde llega su MIDI y vaya por el mismo escritor que el Oxi. Así también desaparece el retraso de C2.
- **Dónde:**
  - la cola: `src/freon-machine.cpp:69`;
  - el escritor del Oxi: `:1926`;
  - el segundo escritor: `:1442` (pads del LPD8) y `:1247` (MIDI de prueba de la web).
- **Cómo se sabe:** medido con la prueba aislada; comprobado en el código que los dos hilos escriben en la misma cola.

### E4. La web no avisa de tensión baja ni de cortes de audio
- **Qué pasa:**
  - **Tensión baja:** Freon la lee en `/sys/devices/platform/soc/soc:firmware/get_throttled`, pero ese archivo no existe en este sistema. La web siempre muestra «-» (medido: `sysst 48 - 0 …`). El dato está disponible en `/sys/class/hwmon/hwmon1/in0_lcrit_alarm` (hoy, 0).
  - **Cortes de audio:** el contador se queda en 0 aunque haya cortes. Ya estaba apuntado en `MEJORAS.md`.
  - **Mensajes perdidos:** la cola cuenta los que pierde, pero nadie muestra ese número.
- **En el concierto:** no te enteras de que la Pi va justa de corriente ni de que el audio corta, hasta que se oye. Y después no hay forma de saber qué pasó.
- **Gravedad:** importante.
- **Esfuerzo:** bajo (tensión y mensajes perdidos) y medio (cortes de audio: hay que preguntar a ALSA, porque RtAudio no lo dice).
- **Dónde:**
  - tensión: `src/freon-machine.cpp:2040`;
  - cortes: `:2144`;
  - mensajes perdidos: `:70`.
- **Cómo se sabe:** medido en la Pi.

### E5. Algunos trabajos dejan quieto el control durante un rato
- **Qué pasa:** el hilo de control lleva los knobs, los LEDs, la web y, hoy, los pads del LPD8. A ratos hace trabajos lentos:
  - guarda el estado forzando la escritura en la tarjeta (50 KB);
  - guarda cada grabación, hasta unos 8 MB en WAV de 24 bits;
  - reabre el audio. Si el USB de la Clarett queda en el estado bloqueado que ya viviste con buffer 128, esa reapertura puede quedarse esperando para siempre;
  - busca aparatos MIDI cada 2 s.

  El audio sigue sonando durante todo esto.
- **En el concierto:**
  - **lo normal:** al acabar una grabación, los knobs y los pads del LPD8 no responden durante uno o dos segundos;
  - **lo raro:** si la reapertura se queda colgada, el control se congela para siempre y la web deja de responder.
- **Gravedad:** importante si el USB se cuelga; si no, menor.
- **Esfuerzo:** medio. Guardar las grabaciones y reabrir el audio en un hilo aparte.
- **Dónde:**
  - guardado del estado: `src/freon-machine.cpp:1144-1154`;
  - grabación: `:783-797` (`saveWav` en `:790`);
  - reapertura: `:2155`;
  - búsqueda de aparatos MIDI: `:1941`.
- **Cómo se sabe:** comprobado en el código. Que la reapertura se cuelgue es sospecha: no lo he reproducido.

### E6. El registro de Freon llega tarde y se pierde si se va la luz
- **Qué pasa:** cuando Freon corre como servicio, sus mensajes se guardan en memoria y llegan al registro a trozos. Por ejemplo, el arranque de hoy aparece con tres líneas en el mismo instante (21,26 s), aunque se escribieron en momentos distintos. Si se va la luz, se pierden los últimos mensajes. Justo esos dirían qué falló.
- **En el concierto:** no se nota. Se nota después, al intentar averiguar qué pasó.
- **Gravedad:** menor.
- **Esfuerzo:** mínimo. Una línea al principio de `main`: `setvbuf(stdout, nullptr, _IOLBF, 0)`.
- **Dónde:** `src/freon-machine.cpp:2068`.
- **Cómo se sabe:** medido en el registro de la Pi.

### E7. Un motor averiado se queda mudo hasta cambiar de motor
- **Qué pasa:** si un motor produce un valor imposible (como ya pasó con la tabla PWM del Wavetable), la protección de la pista lo silencia. Pero el motor no se reinicia, así que esa pista sigue muda bloque tras bloque.
- **En el concierto:** una pista desaparece y no vuelve hasta cambiar de motor en esa pista.
- **Gravedad:** menor. No se conoce hoy ninguna vía que lo provoque.
- **Esfuerzo:** medio. Cuando una pista se silencia por avería, reiniciar su motor.
- **Dónde:** `src/freon-machine.cpp:164` y `src/params.h:232`.
- **Cómo se sabe:** comprobado en el código.

### E8. Tras una avería del máster, la limpieza puede causar un corte
- **Qué pasa:** si el máster recibe algo imposible, se silencia ese bloque y se vacía la memoria de los efectos de máster: unos 6 MB de golpe dentro del audio. En los envíos ya se evita repartiendo el vaciado entre varios bloques.
- **En el concierto:** justo después de la avería, un corte breve más. Solo pasa si ya ha habido un fallo.
- **Gravedad:** menor.
- **Esfuerzo:** medio. Repartir el vaciado, como en los envíos.
- **Dónde:** `src/fx.h:512-519`, llamado desde `src/freon-machine.cpp:526`.
- **Cómo se sabe:** comprobado en el código. El tamaño es un cálculo; no he medido el corte.

### E9. Empezar a grabar desde la web mientras se guarda la anterior
- **Qué pasa:** la controladora comprueba que no haya otra grabación guardándose, pero la web no. Si coincide, la grabación nueva se corta sola y la guardada puede salir con el principio estropeado.
- **Gravedad:** menor.
- **Esfuerzo:** mínimo.
- **Dónde:** `src/freon-machine.cpp:1271`. La comprobación de la controladora está en `:1504`.
- **Cómo se sabe:** comprobado en el código.

### E10. Guardado de los ajustes de sistema
- **Qué pasa:**
  - **Estado de la música:** se guarda bien. Primero en un archivo aparte, que se fuerza al disco. Después se cambia de golpe y la versión anterior queda como `.bak`. Al leer, se comprueba que el archivo esté completo.
  - **Ajustes de sistema** (`freon-sistema.txt`: interfaz, buffer, núcleos…): se guardan sin forzar la escritura en la tarjeta.

  Con el sistema de archivos de la Pi es muy improbable perderlos.
- **En el concierto:** si se fuera la luz justo al cambiar un ajuste, al volver podría arrancar con los ajustes de fábrica (salida de auriculares de la Pi, buffer mínimo).
- **Gravedad:** menor.
- **Esfuerzo:** mínimo.
- **Dónde:** `src/freon-machine.cpp:2032-2035`.
- **Cómo se sabe:** comprobado en el código.

**Revisado y sin problemas:**
- **Hilo de audio:** sin reservas de memoria, bloqueos ni lectura o escritura de archivos. Las listas de notas del granular y de la voz se reservan al arrancar y nunca crecen.
- **Subnormales:** se ponen a cero en el hilo de audio y en cada ayudante, también en la Pi de 64 bits.
- **Protecciones:**
  - por pista: entrada imposible o más de +42 dB;
  - por envío: el envío se vacía;
  - en el máster: antes del compresor y después del limitador.
- **Compresor de bus:** se reinicia solo si se rompe.
- **Colas:** son grandes (16 384 órdenes y 4 096 mensajes MIDI) y no se llenan en uso normal.
- **Reparto entre núcleos:** el hilo de audio nunca depende de un ayudante. El render con 1 y 4 núcleos sale idéntico bit a bit.
- **MIDI:** al desenchufar un aparato se cierra su puerto y se sueltan las notas; al volver a enchufarlo, se reabre.
- **Arranque:** una clave dañada en el estado ya no impide arrancar.

---

## 2. Calidad de sonido

### C1. El máster satura siempre, aunque no haga falta
- **Qué pasa:** después del limitador, cada muestra pasa por `softClip`, una curva de saturación suave. Esta curva no es lineal en ninguna zona: empieza a doblar desde muy abajo. Además, el limitador ya garantiza que nada pase de −1,5 dB. Medido:

  | Nivel de entrada | Distorsión |
  |---|---|
  | −12 dBFS | 0,46 % |
  | −6 dBFS | 1,8 % |
  | −1,5 dBFS (el techo del limitador) | 4,6 % |

  En la prueba de estrés, la salida no pasó nunca de 0,699, es decir, −3,1 dBFS.
- **En el concierto:** el máster suena siempre algo más denso y «saturado». En los picos los armónicos son claramente audibles, y se pierden 1,6 dB de nivel útil. Puede que te guste como color, pero no es neutro, y hoy no se puede apagar. Las salidas directas y la pre-escucha pasan por la misma curva.
- **Gravedad:** importante, porque afecta a todo lo que suena.
- **Esfuerzo:** bajo. Dos opciones:
  - una red de seguridad que no toque nada por debajo de −1 dBFS (lineal hasta ahí y curva solo por encima);
  - o dejar la saturación como efecto opcional del máster, con su knob.

  Hay que decidirlo escuchando una comparación A/B.
- **Dónde:**
  - máster: `src/freon-machine.cpp:534`;
  - pre-escucha: `:549`;
  - salidas directas: `:558`;
  - la curva: `src/dsp.h:16-19`.
- **Cómo se sabe:** medido.

### C2. Los pads del LPD8 llegan con hasta 10 ms de retraso irregular
- **Qué pasa:** el MIDI del Oxi va directo al audio, con el instante en que llegó. En cambio, los pads del LPD8 pasan primero por el hilo de control:
  - ese hilo comprueba la web y las colas cada 10 ms como mucho;
  - cuando reenvía la nota al audio, le pone la hora del reenvío, no la de llegada.
- **En el concierto:** tocando el sampler o el granular con los dedos, los golpes llegan entre 0 y 10 ms tarde, cada uno con un retraso distinto. Con E5, en momentos puntuales, hasta 1–2 s.
- **Gravedad:** importante si tocas ritmos con el LPD8; menor si solo lanzas sonidos sueltos.
- **Esfuerzo:** bajo. Es el mismo arreglo que E3.
- **Dónde:** `src/freon-machine.cpp:1921`, `:1584` (`srv.poll(10)`) y `:1442`.
- **Cómo se sabe:** comprobado en el código; no lo he medido con el aparato.

### C3. Diferencias de nivel entre motores y kits (decidido; solo para recordarlo)
- **Qué pasa:** medido con `--niveles`, con los valores de partida de cada motor:

  | Motor | Nivel medio |
  |---|---|
  | Juno | −36,5 dB |
  | DX7 | −21,4 dB |
  | Resto de motores | entre −28 y −29 dB |

  Es lo que decidiste el 7-11-2026: Juno bajo a propósito y Volume ajustado a oído por pista.
- **Kits de batería:**
  - el Chip suena 6 dB por debajo de los demás (decidido);
  - en el kit Experimental, el HH cerrado sale a −31 dB, frente a −21 dB en los otros kits.
- **En el concierto:** si el Oxi cambia de motor con CC 3 y un preset de fábrica, el salto de volumen puede llegar a 15 dB.
- **Gravedad:** menor.
- **Esfuerzo:** bajo, si decides cambiarlo.
- **Dónde:** `src/params.h:114` y `:231`.
- **Cómo se sabe:** medido.

**Revisado y sin problemas:**
- **Limitador del máster:** exacto. Con golpes de +32 dB, el pico queda en el umbral (+0,00 dB).
- **Air del compresor de bus:** hace lo prometido (+9,3 dB a 12 kHz al 100 %).
- **Clipping digital:** no puede haberlo, porque nada sale por encima de 1,0.
- **Valores sueltos:** ni `--niveles` ni el estrés producen ningún `nan`.

Un aviso sobre el compresor de bus: con Amount, Makeup y Air al máximo entra hasta +25 dB al limitador. Se oiría como un bombeo fuerte. No es un fallo, pero conviene no llevarlo a tope.

---

## 3. Eficiencia en la Pi 3

### F1. El registro del sistema se inunda cuando el MC-24 falla
- **Qué pasa:** cuando el USB falla, el sistema escribe hasta 1 024 líneas por segundo de `urb status -32`. El registro es persistente, así que todo eso va a la tarjeta SD.
- **En el concierto:** más CPU y escrituras en la SD justo cuando el USB ya va mal.
- **Gravedad:** menor. Se arregla con E1.
- **Esfuerzo:** bajo, si además se quiere limitar el ritmo del registro (por ejemplo, `RateLimitBurst` en journald).
- **Dónde:** `deploy/wifi-propia.sh:105-108`, donde se configura el registro.
- **Cómo se sabe:** medido.

### F2. Los efectos de máster y el motor anterior van en el núcleo principal
- **Qué pasa:**
  - **Efectos de máster:** Space, Throw, Repeat y el resto se calculan en el núcleo del audio, sin repartir.
  - **Cambio de motor:** el motor anterior se sigue calculando 4 s más, en la misma tarea que el nuevo.
- **En el concierto:** al lanzar una combinación de efectos, o al cambiar de motor con notas sonando, sube el pico de CPU de ese núcleo. Con un 54–68 % de media hay margen, pero es justo en esos momentos.
- **Gravedad:** menor.
- **Esfuerzo:** medio (motor anterior) y alto (efectos de máster).
- **Dónde:** `src/freon-machine.cpp:523` y `src/params.h:228`.
- **Cómo se sabe:** comprobado en el código; el coste no está medido en la Pi.

### F3. El audio HDMI llena el registro de errores
- **Qué pasa:** cada vez que Freon busca interfaces de audio, la salida HDMI de la Pi da los errores «Invalid CTL pulse» y «error 524». Es inofensivo, pero llena el registro y tapa los mensajes que importan.
- **Gravedad:** menor.
- **Esfuerzo:** mínimo. Quitar el audio HDMI en `config.txt`. Es un cambio del sistema: hazlo tú o pídemelo.
- **Cómo se sabe:** medido.

**Revisado y sin problemas:**
- **CPU en reposo:** 11 %, con buffer 512 y 4 núcleos.
- **Temperatura:** 45–48 °C.
- **Frecuencia:** fija a 1,2 GHz.
- **Memoria y disco:** sobran (658 MB libres, 5 % de la SD ocupado).
- **Ayudantes:** esperan en bajo consumo.

---

## 4. Seguridad

### S1. La web está abierta a toda la red, sin contraseña
- **Qué pasa:** Freon escucha en todas las redes (`--host 0.0.0.0`, puerto 8080). Cualquiera conectado a la misma wifi puede, sin contraseña:
  - cambiar todo el sonido;
  - reiniciar Freon;
  - cambiar los ajustes de sistema;
  - pasar la Pi a otra wifi (y dejarte sin web).

  Lo que sí está protegido: otras páginas web no pueden controlar Freon desde un navegador, gracias a la comprobación de origen.
- **En el concierto:**
  - con la wifi propia de la Pi (WPA2 con clave), solo entra quien tenga la clave;
  - en modo «casa», conectada a la wifi de una sala, cualquiera de esa red podría tocarlo, por error o a propósito.
- **Gravedad:** importante, si se usa la wifi de una sala.
- **Esfuerzo:** medio. Un PIN en la web, o atender solo en la wifi propia y en casa.
- **Dónde:** `src/server.h:63-70` y `:154`; el servicio, en `deploy/install-pi.sh:22`.
- **Cómo se sabe:** medido (puerto abierto a toda la red).

### S2. SSH acepta contraseña
- **Qué pasa:**
  - el puerto 22 está abierto a toda la red;
  - no hay ninguna línea que impida entrar con contraseña;
  - el usuario `fugadefreon` puede hacer de administrador (`sudo`).

  Tú ya entras con llave.
- **Gravedad:** menor en la wifi propia; importante en una red ajena si la contraseña es débil.
- **Esfuerzo:** mínimo. `PasswordAuthentication no` en la configuración de SSH. Es un cambio del sistema: hazlo tú o pídemelo.
- **Cómo se sabe:** medido (puerto abierto; no se encuentra esa línea en la configuración).

### S3. Permisos para la wifi
- **Qué pasa:**
  - **Bien limitado:** `sudoers` solo deja ejecutar `freon-wifi` y `poweroff`, y Freon solo le pasa «propia» o «casa».
  - **Más amplio de lo necesario:** la regla de polkit da al usuario todo el gestor de red (`org.freedesktop.NetworkManager.*`). Si alguien tomara el control de Freon, podría cambiar cualquier ajuste de red.
- **Gravedad:** menor.
- **Esfuerzo:** bajo.
- **Dónde:** `deploy/ajustes-pi.sh:32-35`.
- **Cómo se sabe:** comprobado en el script. En la Pi la regla no se puede leer sin `sudo`.

### S4. Conexiones a la web que nunca se cierran
- **Qué pasa:** la web admite como mucho 16 conexiones y no cierra las que se quedan calladas. Por ejemplo, unos cuantos móviles con la pestaña abierta y dormidos, o un aparato que se conecta y no dice nada, pueden ocupar los 16 huecos. Entonces la web deja de responder. El audio no se entera.
- **Gravedad:** menor.
- **Esfuerzo:** bajo. Cerrar las conexiones que lleven un rato sin hablar.
- **Dónde:** `src/server.h:86`.
- **Cómo se sabe:** comprobado en el código.

**Revisado y sin problemas:**
- los nombres de archivo de samples y breaks no pueden salir de sus carpetas;
- los mensajes demasiado grandes cierran la conexión;
- todos los valores pasan por `rangeOf`;
- la clave de la wifi propia solo la puede leer el administrador.

---

## Antes del próximo concierto

1. **Corriente (E1):**
   - pon un hub USB alimentado para la Clarett, el MC-24 y el LPD8;
   - usa la fuente oficial de la Pi;
   - toca 30 minutos y comprueba que `vcgencmd get_throttled` sigue en `0x0`.
2. **Wifi (E2):** antes de salir de casa, elige «propia» en Ajustes → Network y no cambies de wifi durante el concierto. Así no habrá reaperturas de audio ni nadie de la sala podrá entrar en la web (S1).
3. **Ensayo largo:** una hora con el set real, la Clarett y buffer 512. Después:
   - mira en el registro si hay «Reabro el audio», «Aviso» o `urb status`;
   - mira la CPU máxima en Ajustes.
4. **Si el MC-24 se cuelga:** desenchúfalo y vuelve a enchufarlo. Freon lo recupera sola.
5. **Arreglos pequeños que propongo hacer antes** (código, cuando me digas):
   - E2: reabrir el audio solo cuando lo pide `freon-wifi`;
   - E3 y C2: los pads del LPD8 por el camino del Oxi;
   - E4: aviso de tensión baja en la web;
   - E6: registro sin retraso;
   - E9: no grabar desde la web mientras se guarda la anterior;
   - el aviso de compilación.
6. **C1, para decidir tú:** escuchar el máster con y sin la saturación final, en una comparación A/B, y quedarte con lo que prefieras.
7. **Lleva un cable de red.** Si la wifi falla, puedes entrar a la web conectando la Pi al portátil, y el audio sigue sonando sin web.
