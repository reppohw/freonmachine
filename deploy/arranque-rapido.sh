#!/bin/sh
# Arranque lo más corto posible y sin sorpresas en directo. Se ejecuta una vez en la Pi, desde la carpeta del proyecto:
#   ./deploy/arranque-rapido.sh
# Para deshacerlo: ./deploy/arranque-rapido.sh deshacer
#
# 1. Freon arranca en cuanto el sistema básico está listo (no espera a «sound.target»): si la interfaz aún no ha
#    aparecido, Freon la espera unos segundos y, si no, suena por la salida de la Pi y pasa a ella cuando llega.
# 2. Arranque silencioso y sin logo. (Sin «turbo» al arrancar: con un cargador justo provocaría tensión baja.)
# 3. Fuera lo que no se usa: Bluetooth, cloud-init y discos externos (udisks2).
# 4. Fuera tareas automáticas que podrían arrancar en mitad de un concierto: actualizaciones de paquetes,
#    mantenimiento de discos y del manual. (Las actualizaciones se pueden hacer a mano cuando quieras.)
set -e
CFG=/boot/firmware/config.txt; CMD=/boot/firmware/cmdline.txt
SERV="bluetooth.service hciuart.service udisks2.service e2scrub_reap.service cloud-init-local.service cloud-init-main.service cloud-init-network.service cloud-config.service cloud-final.service rpi-eeprom-update.service"
TIMERS="apt-daily.timer apt-daily-upgrade.timer man-db.timer e2scrub_all.timer dpkg-db-backup.timer"
echo "Te pedirá la contraseña de la Pi (la de entrar como $(id -un))."

if [ "$1" = "deshacer" ]; then
  sudo sed -i -e "s/^After=basic.target$/After=sound.target/" /etc/systemd/system/freon-machine.service
  sudo sed -i -e '/# Freon: arranque rápido/,/# Freon: fin/d' "$CFG"
  sudo sed -i -e 's/ quiet loglevel=3 logo.nologo//' "$CMD"
  for s in $SERV $TIMERS; do sudo systemctl enable "$s" >/dev/null 2>&1 || true; done
  sudo systemctl daemon-reload; echo "Deshecho. Reinicia la Pi."; exit 0
fi

echo "== Freon arranca en cuanto el sistema básico está listo"
sudo sed -i -e "s/^After=sound.target$/After=basic.target/" /etc/systemd/system/freon-machine.service
sudo systemctl daemon-reload

echo "== Arranque silencioso y sin logo"
[ -f "$CFG.antes-freon" ] || sudo cp "$CFG" "$CFG.antes-freon"
[ -f "$CMD.antes-freon" ] || sudo cp "$CMD" "$CMD.antes-freon"
if ! grep -q "# Freon: arranque rápido" "$CFG"; then
  printf '\n# Freon: arranque rápido\n[all]\ndisable_splash=1\nboot_delay=0\ndtoverlay=disable-bt\n# Freon: fin\n' | sudo tee -a "$CFG" >/dev/null
fi
grep -q " quiet" "$CMD" || sudo sed -i -e '1 s/$/ quiet loglevel=3 logo.nologo/' "$CMD"

echo "== Fuera servicios que no se usan y tareas automáticas"
for s in $SERV $TIMERS; do sudo systemctl disable --now "$s" >/dev/null 2>&1 || true; done

echo
echo "Listo. Reinicia la Pi para probarlo: sudo reboot"
echo "Copia de los archivos de arranque: $CFG.antes-freon y $CMD.antes-freon"
