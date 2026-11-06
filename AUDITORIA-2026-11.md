# Auditoría de Freon Machine · noviembre de 2026

Auditoría breve para decidir con Jorge. **No se ha aplicado ningún arreglo, y el código sigue igual que la copia de seguridad.**

Alcance: código propio de `src/`, leyendo `freon-machine.cpp`, `fx.h`, `moog.h`, `juno.h`, `voice303.h`, `par.h`, `server.h`, `dsp.h` y parte de `params.h`. No se ha revisado `third_party/`, `live.h`, `voice.h`, `drums.h`, `synths2.h` ni `gameboy.h`.

**Cómo leer «Comprobado»:** ningún hallazgo se ha reproducido ejecutando el programa, y ningún coste se ha medido en la Pi.
- **Verificado en el código:** el fallo se deduce leyendo las líneas citadas; falta verlo ocurrir.
- **Probable:** depende de algo que no se ha comprobado.

La única medida real es la que ya existía: 54–68 % de CPU en la Pi con la mezcla de Jorge y buffer 512.

## Resumen

| N.º | Hallazgo | Gravedad | Comprobado |
|---|---|---|---|
| 1 | Al desconectar la interfaz USB, el audio puede no reabrirse | Alta | Probable |
| 2 | Con CC moviéndose sin parar, el estado no se guarda | Media-alta | Verificado en el código |
| 3 | Un archivo de estado dañado puede impedir arrancar | Media | Verificado en el código |
| 4 | Tras un NaN, el máster puede quedarse mudo hasta reiniciar | Media | Parcial |
| 5 | Una ráfaga de MIDI anula el reparto entre núcleos | Media | Verificado en el código; impacto sin medir |
| 6 | Grabaciones: carrera al arrancar y memoria que no se libera | Baja | Verificado en el código |
| 7 | CPU: el motor anterior sigue calculando 4 s tras cambiar | Media | Verificado en el código; coste sin medir |
| 8 | CPU: Space y Throw van en el núcleo principal | Media | Verificado en el código; coste sin medir |
| 9 | CPU: el Moog repite cálculos caros en cada muestra | Media | Verificado en el código; coste sin medir |
| 10 | CPU: Sub siempre calculando y arcoseno del Summing | Baja | Verificado en el código; coste sin medir |

## Estabilidad

### 1. Al desconectar la interfaz USB, el audio puede no reabrirse
- **Dónde:** `src/freon-machine.cpp:1497-1501`; también `:1946` y `:1951`.
- **Gravedad:** alta.
- **Comprobado:** probable. Lo verificado es que el vigilante solo mira si el contador de llamadas se mueve. Falta saber qué hace RtAudio cuando la interfaz desaparece.
- **En el escenario:** se suelta el cable USB y no vuelve el sonido. Si RtAudio sigue llamando al audio a pesar del error de escritura, el contador avanza, el vigilante cree que todo va bien y nunca reabre. Además, esas llamadas podrían ir sin pausa y con prioridad de tiempo real, ocupando un núcleo. Otro riesgo: si `stopStream()` (`:1951`) se queda esperando, se congela el hilo de control y con él los knobs y la web.
- **Solución propuesta:** que el vigilante compare las llamadas con las esperadas según el tiempo y el buffer. Muchas más de las debidas también contarían como avería. Hacer la reapertura de forma que un bloqueo no congele el control.
- **Prueba para mañana:** con una interfaz USB, tirar del cable mientras suena y volver a conectarlo. Esta es la prueba más importante de la lista.

### 2. Con CC moviéndose sin parar, el estado no se guarda
- **Dónde:** `src/freon-machine.cpp:1534`; el plazo se reinicia en `:756`, `:1170` y `:1481`.
- **Gravedad:** media-alta.
- **Comprobado:** verificado en el código.
- **En el escenario:** solo se guarda tras 0,8 s sin cambios, y cada cambio reinicia la espera. Si una línea de modulación del Oxi mueve un parámetro sin descanso, no se guarda nada mientras dure. Un apagón o desenchufar al final se lleva todo lo tocado desde que empezó la modulación.
- **Solución propuesta:** añadir un tope, por ejemplo guardar como mucho cada 15–30 s aunque sigan llegando cambios. Conviene medir antes cuánto tarda `fsync` en la tarjeta (`:1109`): mientras tanto se paran los knobs, aunque no el audio. Si tarda mucho, escribir el archivo en un hilo aparte.

### 3. Un archivo de estado dañado puede impedir arrancar
- **Dónde:** `src/freon-machine.cpp:880`, llamado desde `:963` (`valid()` va antes de comprobar que la clave existe).
- **Gravedad:** media: poco probable, pero grave si ocurre.
- **Comprobado:** verificado en el código.
- **En el escenario:** una clave recortada como `"d.l"` hace que `k.substr(5)` lance una excepción, y nadie la captura en `load()`. El programa se cierra, el servicio lo reinicia y vuelve a fallar en bucle. La máquina no arranca hasta arreglar el archivo a mano, y el `.bak` no se usa porque el archivo parece completo.
- **Solución propuesta:** comprobar la longitud antes del `substr`, envolver la lectura en un `try/catch` y pasar al `.bak` si falla.

### 4. Tras un NaN, el máster puede quedarse mudo hasta reiniciar
- **Dónde:** `src/freon-machine.cpp:513-514` y `:158`; `src/fx.h:183`, `:241` y `:412-419`.
- **Gravedad:** media: improbable, pero sería silencio total.
- **Comprobado:** parcial. Verificado que la limpieza no alcanza a estas partes. No se conoce ninguna vía actual que meta un NaN ahí, aunque ya hubo uno real en la tabla PWM del Wavetable (`MEJORAS.md`).
- **En el escenario:**
  - La avería del máster limpia `mfx`, pero no el filtro `mdj` ni la distorsión `mdist`. Si el NaN queda dentro de ellos, cada bloque vuelve a salir en silencio.
  - En los envíos, `zero()` limpia las líneas de retardo pero no los filtros internos, así que ese envío seguiría mudo.
  - Un motor con NaN se silencia en `:158`, pero no se recupera hasta cambiar de motor.
  - Además, `MasterFx::reset()` vacía varios MB de golpe dentro del audio, justo lo que `fx.h:289` evita en los envíos, y puede causar un corte.
- **Solución propuesta:** una limpieza completa de filtros que solo se use en caso de avería, con el vaciado de búferes repartido en varios bloques. Si se limita a ese caso, no cambia el sonido normal.

### 5. Una ráfaga de MIDI anula el reparto entre núcleos
- **Dónde:** `src/freon-machine.cpp:380-384`, `:186` y `:409-410`.
- **Gravedad:** media.
- **Comprobado:** verificado en el código; impacto sin medir.
- **En el escenario:** cada instante con notas o reloj parte el bloque en trozos, ajustados a múltiplos de 8 muestras, y los trozos de menos de 32 se calculan en un solo núcleo. Muchas notas muy seguidas (ratchets, un bucle MIDI, un aparato enloquecido) pueden pasar una carga repartida en 4 núcleos a uno solo. Con la mezcla al 54–68 % repartida, eso puede no caber y oírse cortes. Cada trozo paga además su coste fijo. Los CC no afectan, porque van al hilo de control (`:1782`).
- **Solución propuesta:** si un bloque trae muchos eventos, agruparlos en una rejilla de 32 muestras o más. Con pocos eventos, seguir como ahora, para que el tempo normal no cambie.

### 6. Grabaciones: carrera al arrancar y memoria que no se libera
- **Dónde:** `src/freon-machine.cpp:329`, `:1220`, `:775`, `:765`, `:769` y `:1908`.
- **Gravedad:** baja.
- **Comprobado:** verificado en el código.
- **En el escenario:**
  - Desde la web se puede iniciar una grabación mientras la anterior se está guardando (la controladora sí lo comprueba, en `:1396`). La nueva se cortaría y la guardada podría salir con el principio estropeado.
  - Cada grabación queda en memoria para siempre: unos 11,5 MB por cada 30 s en estéreo. Harían falta unas 50 largas en la misma sesión para acercarse a agotar la Pi 3.
- **Solución propuesta:** ignorar `K_REC` si `recState` no es 0, y poner un tope o liberar las grabaciones que ningún pad esté usando, cuidando que el audio no las esté leyendo.

## Consumo de CPU

Ningún coste de esta sección está medido. Es lo que se ve caro leyendo el código. Juno y 303 no se han podido valorar, porque su coste está en `third_party/`, y es posible que pesen más que todo lo de aquí.

### 7. El motor anterior sigue calculando 4 s tras cambiar
- **Dónde:** `src/params.h:206` y `:213-222`.
- **Gravedad:** media.
- **Comprobado:** verificado en el código; coste sin medir.
- **En el escenario:** el motor anterior deja de oírse en 10 ms, pero sigue calculándose 4 s más, en la misma tarea que el nuevo. Como un motor no se reparte entre núcleos, cambiar de motor con notas sonando suma el coste de los dos justo en ese momento.
- **Solución propuesta:** dejar de calcularlo cuando ya no suene, o acortar esos 4 s si sus notas no «resucitan» al volver a él. Hay que comprobar esto último a oído.

### 8. Space y Throw van en el núcleo principal
- **Dónde:** `src/fx.h:472-499`, llamado desde `src/freon-machine.cpp:512`.
- **Gravedad:** media.
- **Comprobado:** verificado en el código; coste sin medir.
- **En el escenario:** los envíos se reparten entre núcleos, pero la reverb larga del máster no. Space, y también Riser (que le envía señal), añaden una reverb entera al núcleo principal. Ocurre al disparar una combinación, que suele ser el momento más cargado.
- **Solución propuesta:** calcular esa reverb como una tarea repartida más. La dificultad es que trabaja en medio de la cadena del máster, así que es un cambio delicado que hay que medir y comparar con el WAV de referencia.

### 9. El Moog repite cálculos caros en cada muestra
- **Dónde:** `src/moog.h:105`, `:113`, `:114`, `:117` y `:118` (`set()` en `:39-45`).
- **Gravedad:** media.
- **Comprobado:** verificado en el código; coste sin medir.
- **En el escenario:** en cada muestra hay varias potencias y exponenciales, aunque el tono y la velocidad no cambien entre notas. Como es un motor de una sola tarea, limita el pico.
- **Solución propuesta:** recalcular `mtof`, `pow` y `velGain` solo cuando cambie su entrada. Debería dar un WAV idéntico bit a bit, y hay que confirmarlo con el render. Es la mejora con menos riesgo de esta lista.

### 10. Sub siempre calculando y arcoseno del Summing
- **Dónde:** `src/freon-machine.cpp:126-137` y `:416` (Sub); `:477` y `:505`, con `src/fx.h:384` (Summing).
- **Gravedad:** baja.
- **Comprobado:** verificado en el código; coste sin medir.
- **En el escenario:** el Sub calcula una potencia y una tangente hiperbólica por muestra aunque esté en silencio, y `asin` corre por muestra con Summing activo. Ambos van en el núcleo principal. Cada uno cuesta poco, pero se suma a todos los bloques.
- **Solución propuesta:** salir enseguida si el Sub no suena. Para `asin`, usar una aproximación solo si la diferencia medida queda por debajo de −90 dB.

## Orden propuesto para mañana

1. Prueba del cable USB (n.º 1), con Jorge y una interfaz.
2. Medir en la Pi cuánto pesa cada motor y cada efecto con su mezcla real, para confirmar o reordenar los n.º 7 a 10.
3. Arreglos pequeños y seguros: n.º 2, 3 y 9.
