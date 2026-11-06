# Propuestas de sonido · noviembre de 2026

Cinco ideas para decidir escuchando, ordenadas por lo que más se ganaría, a mi juicio. **Nada de esto está aplicado ni probado.** Los consumos de CPU son estimaciones leyendo el código, no medidas en la Pi, y mañana conviene medirlos.

## 1. Sacar el sonido por una interfaz USB, no por el jack de la Pi
- **Qué se oiría:** menos ruido de fondo y mejor definición en toda la mezcla. El jack de la Pi 3 no está pensado para audio de calidad; ya está pendiente en `MEJORAS.md` probar una interfaz. Es probablemente la mayor mejora y no toca ni un motor.
- **CPU en la Pi:** poca (estimado). Permitiría además probar buffers más pequeños.
- **Riesgo:** bajo para el sonido. Añade un punto de fallo, el cable USB, así que antes hay que hacer la prueba de desconexión (hallazgo n.º 1 de `AUDITORIA-2026-11.md`).
- **Código y licencia:** no hace falta código nuevo, solo arrancar con `--audio`.

## 2. Igualar los niveles entre presets y motores
- **Qué se oiría:** al cambiar de preset no habría saltos de volumen. Según `MEJORAS.md`, Bell, Marimba y Bowed plate del Modal salen muy altos, y Juno Bass y Pluck, bajos. También hay que revisar el Supersaw. Esta noche no se ha pasado `--niveles`, así que faltan las cifras.
- **CPU en la Pi:** ninguna (estimado): solo cambian unos números de ganancia.
- **Riesgo:** bajo, pero cambia el nivel de sonidos que Jorge ya conoce. Se debe medir con `--niveles` y confirmar a oído. Los presets ya guardados por Jorge no deben tocarse sin avisar.
- **Código y licencia:** ajustes propios en `src/params.h`, sin código externo.

## 3. Limitador del máster con 1–2 ms de anticipación
- **Qué se oiría:** bombos y golpes más limpios cuando la mezcla va fuerte. Hoy el limitador reacciona después del pico (`src/fx.h:87`) y lo que se le escapa lo redondea el recorte suave final. Con anticipación, baja el volumen justo antes del golpe.
- **CPU en la Pi:** baja (estimado): menos de un 1 % de un núcleo.
- **Riesgo:** bajo a medio. Añade 1–2 ms de retraso a toda la salida, y hay que notarlo tocando con el Oxi. El carácter del máster cambia, así que debe compararse a ciegas con el actual.
- **Código y licencia:** técnica sencilla de pocas líneas, escrita para el proyecto. Si se prefiere código probado, buscar una biblioteca con licencia MIT y comprobar su licencia antes de usarla.

## 4. Distorsión con menos aliasing
- **Qué se oiría:** el Fuzz más limpio y musical en agudos y notas altas, sin esos tonos ásperos que no siguen a la nota. Hoy va a 2× con una interpolación simple (`src/fx.h:64`), y con Drive alto la ganancia llega a unas 50 veces. El Bitcrusher se queda igual: su suciedad es a propósito.
- **CPU en la Pi:** media (estimado). Un método llamado ADAA añade un logaritmo por muestra y canal. Pasar a 4× costaría más o menos el doble que ahora en cada canal con distorsión.
- **Riesgo:** medio. Parte de la agresividad actual puede venir de ese aliasing, y a Jorge podría gustarle. Mejor dejar elegir entre los dos y probar con la mezcla real, por el gasto en cada canal.
- **Código y licencia:** ADAA es un método publicado (Parker y otros, DAFx 2016) y se escribiría para el proyecto. Como alternativa, las saturaciones de Airwindows, con licencia MIT, ya previstas en `MEJORAS.md`.

## 5. Cambiar el Modal por Rings o Elements de Mutable Instruments
- **Qué se oiría:** cuerdas, placas y campanas más vivas y creíbles, con resonadores de eficacia ya demostrada en módulos reales. Sustituiría al Modal propio.
- **CPU en la Pi:** media a alta (estimado). Rings corre en un microcontrolador modesto, así que debería caber en la Pi 3, pero solo cuando el Modal esté en uso y hay que medirlo. Elements pesa bastante más.
- **Riesgo:** medio. Sus presets sonarán distinto y hay que rehacerlos. Los CC y las claves del estado deben seguir iguales. Jorge tiene pendiente decidir si el Modal se queda.
- **Código y licencia:** Mutable Instruments, licencia MIT. El proyecto ya usa Plaits y `stmlib` de la misma fuente, pero el código de Rings aún no está incluido.
