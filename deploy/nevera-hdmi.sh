#!/bin/sh
# La nevera (visuales de Fuga de Freón) a pantalla completa por el HDMI de la Pi, sin escritorio.
# Lo arranca el servicio freon-nevera (ver deploy/instalar-nevera-hdmi.sh); no hace falta lanzarlo a mano.
# - Espera a que la Freon conteste antes de abrir la página.
# - El navegador no toca el audio: así nunca puede quitarle la Scarlett a la Freon.
# - Prioridad baja: si falta CPU, cede la imagen, nunca el sonido (el audio de la Freon va en tiempo real).
for i in $(seq 1 120); do curl -s -o /dev/null -m 2 http://localhost:8080/ && break; sleep 1; done
mkdir -p "$HOME/.config/freon-nevera"
exec cage -- chromium --ozone-platform=wayland --kiosk --no-first-run --noerrdialogs --disable-session-crashed-bubble \
  --password-store=basic --user-data-dir="$HOME/.config/freon-nevera" --ignore-gpu-blocklist --disable-features=Translate \
  --mute-audio --alsa-output-device=null --alsa-input-device=null --check-for-update-interval=31536000 \
  "http://localhost:8080/nevera.html?auto&pi"
