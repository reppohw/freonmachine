#!/bin/sh
# Instala Freon Machine en una Raspberry Pi para que arranque solo al encenderla, sin pantalla.
# Uso (desde la carpeta del proyecto):  ./deploy/install-pi.sh "Scarlett" "--host 0.0.0.0 --buffer 256"
#   el primer texto es parte del nombre de tu interfaz de audio (mira ./bin/freon-machine --list);
#   el segundo, opcional, son opciones de arranque (--host 0.0.0.0 abre la web a tu red)
set -e
cd "$(dirname "$0")/.."
DIR="$(pwd)"; USER_NAME="$(id -un)"; AUDIO="${1:-}"
echo "== Paquetes necesarios"
sudo apt-get update -qq && sudo apt-get install -y -qq g++ libasound2-dev
echo "== Compilando"
./build.sh
echo "== Permisos de tiempo real para $USER_NAME"
sudo usermod -aG audio "$USER_NAME"
printf '@audio - rtprio 95\n@audio - memlock unlimited\n' | sudo tee /etc/security/limits.d/90-freon-machine.conf >/dev/null
echo "== Procesador a máxima velocidad (sin bajones de frecuencia en pleno directo)"
printf '[Unit]\nDescription=CPU en modo performance\n[Service]\nType=oneshot\nExecStart=/bin/sh -c "for c in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do echo performance > $c; done"\n[Install]\nWantedBy=multi-user.target\n' | sudo tee /etc/systemd/system/cpu-performance.service >/dev/null
echo "== Apagado desde el mezclador MIDI (nota 127, 3 s) sin contraseña"
printf '%s ALL=(root) NOPASSWD: /sbin/poweroff\n' "$USER_NAME" | sudo tee /etc/sudoers.d/freon-machine-poweroff >/dev/null
sudo chmod 440 /etc/sudoers.d/freon-machine-poweroff
echo "== Servicio de arranque"
ARGS="--esperar-audio 60 --apagado-midi ${2:-}"
[ -n "$AUDIO" ] && ARGS="--audio \"$AUDIO\" $ARGS"
sed -e "s|@DIR@|$DIR|g" -e "s|@USER@|$USER_NAME|g" -e "s|@ARGS@|$ARGS|g" deploy/freon-machine.service | sudo tee /etc/systemd/system/freon-machine.service >/dev/null
sudo systemctl daemon-reload
sudo systemctl enable --now cpu-performance.service freon-machine.service
echo
echo "Listo. Freon Machine arranca solo al encender la Pi."
echo "  Ver lo que dice:      journalctl -u freon-machine -f"
echo "  Reiniciarlo:          sudo systemctl restart freon-machine"
echo "  Pararlo:              sudo systemctl stop freon-machine"
echo "  Para configurar desde el móvil, edita el servicio y añade --host 0.0.0.0 (sudo systemctl edit --full freon-machine)."
echo "Reinicia la Pi una vez para que valgan los permisos de tiempo real: sudo reboot"
