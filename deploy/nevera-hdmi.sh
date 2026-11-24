#!/bin/sh
# La nevera (visuales de Fuga de Freón) a pantalla completa por el HDMI de la Pi, sin escritorio.
# Lo arranca el servicio freon-nevera (ver deploy/instalar-nevera-hdmi.sh); no hace falta lanzarlo a mano.
# - Vale cualquier proyector o tele, en cualquiera de los dos HDMI, enchufado antes o después de encender:
#   usa la resolución de la pantalla (como mucho 1920x1200) y solo la última pantalla conectada.
# - Sin pantalla enchufada no arranca nada (no gasta CPU); al enchufarla, sale sola. Si se desenchufa a mitad,
#   se apaga, y al volver a enchufarla sigue el show donde iba.
# - Espera a que la Freon conteste antes de abrir la página.
# - El navegador no toca el audio: así nunca puede quitarle la Scarlett a la Freon.
# - Prioridad baja: si falta CPU, cede la imagen, nunca el sonido (el audio de la Freon va en tiempo real).
DIR="$(cd "$(dirname "$0")" && pwd)"
hdmi() { cat /sys/class/drm/card*-HDMI-A-*/status 2>/dev/null | grep -qx connected; }
if [ "${1:-}" != "dentro" ]; then
  until hdmi; do sleep 2; done
  for i in $(seq 1 120); do curl -s -o /dev/null -m 2 http://localhost:8080/ && break; sleep 1; done
  cage -d -m last -- "$DIR/nevera-hdmi.sh" dentro &
  pid=$!; off=0
  while kill -0 "$pid" 2>/dev/null; do
    if hdmi; then off=0; else off=$((off + 1)); [ "$off" -ge 5 ] && kill "$pid"; fi   # 10 s sin pantalla: se apaga
    sleep 2
  done
  wait "$pid"; exit 0   # el servicio la vuelve a lanzar y espera otra pantalla
fi
"$DIR/nevera-resolucion.sh" &
mkdir -p "$HOME/.config/freon-nevera"
exec chromium --ozone-platform=wayland --kiosk --no-first-run --noerrdialogs --disable-session-crashed-bubble \
  --password-store=basic --user-data-dir="$HOME/.config/freon-nevera" --ignore-gpu-blocklist --disable-features=Translate \
  --mute-audio --alsa-output-device=null --alsa-input-device=null --check-for-update-interval=31536000 \
  "http://localhost:8080/nevera.html?auto&pi"
