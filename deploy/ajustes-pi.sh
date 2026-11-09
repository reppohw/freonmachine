#!/bin/sh
# Ajustes de la Raspberry para el directo. Se ejecuta una vez, desde la carpeta del proyecto:
#   ./deploy/ajustes-pi.sh            (te pregunta la clave)
#   ./deploy/ajustes-pi.sh "clave"    (o se la das directamente)
# La clave es la de la wifi propia que crea la Pi cuando no encuentra tu wifi (8 caracteres o más).
#
# 1. Arranque más rápido: Freon no espera a la red (para sonar no la necesita); fuera cloud-init y la espera de red.
# 2. Wifi propia «Freon-Machine»: si la Pi no encuentra una wifi conocida, crea la suya. Te conectas desde el móvil y
#    abres http://10.42.0.1:8080. En casa, con tu wifi a la vista, se conecta a ella como siempre.
# 3. Permiso para que Freon gestione la wifi desde su web (pantalla de red), sin terminal ni contraseña.
#
# Si algo saliera mal y la Pi no apareciera en la red, conéctala por cable de red al router.
set -e
cd "$(dirname "$0")/.."
CLAVE="${1:-}"
USER_NAME="$(id -un)"
while [ ${#CLAVE} -lt 8 ]; do printf "Clave para la wifi de Freon (8 caracteres o más): "; read -r CLAVE; done
echo "Ahora te pedirá la contraseña de la Pi (la de entrar como $USER_NAME)."

echo "== Freon arranca sin esperar a la red"
sudo sed -i -e "s/^After=.*/After=sound.target/" /etc/systemd/system/freon-machine.service
sudo systemctl daemon-reload

echo "== Fuera cloud-init y la espera de red"
sudo touch /etc/cloud/cloud-init.disabled
sudo systemctl disable NetworkManager-wait-online.service >/dev/null 2>&1 || true

echo "== Wifi propia «Freon-Machine» (solo si no hay una wifi conocida)"
sudo nmcli connection delete Freon-Machine >/dev/null 2>&1 || true
sudo nmcli connection add type wifi ifname wlan0 con-name Freon-Machine autoconnect yes connection.autoconnect-priority -100 \
  ssid Freon-Machine 802-11-wireless.mode ap 802-11-wireless.band bg ipv4.method shared \
  wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$CLAVE" \
  802-11-wireless.channel 6 802-11-wireless-security.pmf disable 802-11-wireless-security.proto rsn \
  802-11-wireless-security.pairwise ccmp 802-11-wireless-security.group ccmp >/dev/null   # el chip de la Pi no admite WPA-PSK-SHA256: WPA2 clásico

echo "== Permiso para gestionar la wifi desde la web de Freon"
printf '%s\n' \
  'polkit.addRule(function(action, subject) {' \
  "  if (subject.user == \"$USER_NAME\" && action.id.indexOf(\"org.freedesktop.NetworkManager.\") == 0) return polkit.Result.YES;" \
  '});' | sudo tee /etc/polkit-1/rules.d/50-freon-wifi.rules >/dev/null

echo
echo "Listo. Reinicia la Pi para probar el arranque: sudo reboot"
echo "Wifi propia: red «Freon-Machine», web en http://10.42.0.1:8080 (solo cuando no encuentra tu wifi)."
