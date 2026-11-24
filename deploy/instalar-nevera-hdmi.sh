#!/bin/sh
# Instala la nevera por HDMI: al encender la Pi, los visuales salen solos por el HDMI (el más cercano a la corriente).
# Se ejecuta una vez en la Pi, desde la carpeta del proyecto:  ./deploy/instalar-nevera-hdmi.sh
# Para quitarlo:  ./deploy/instalar-nevera-hdmi.sh quitar
# Se maneja desde la web de la Freon (Settings › Visuals): ajustes del show, empezar, final.
set -e
USER_NAME="$(id -un)"
FREON_DIR="$(cd "$(dirname "$0")/.." && pwd)"
echo "Te pedirá la contraseña de la Pi (la de entrar como $USER_NAME)."
if [ "${1:-}" = "quitar" ]; then
  sudo systemctl disable --now freon-nevera 2>/dev/null || true
  sudo rm -f /etc/systemd/system/freon-nevera.service; sudo systemctl daemon-reload
  sudo systemctl enable --now getty@tty1 2>/dev/null || true
  echo "Quitada. La Freon sigue igual."; exit 0
fi
echo "== Navegador y pantalla completa (si faltan)"
command -v cage >/dev/null && command -v chromium >/dev/null && command -v wlr-randr >/dev/null || sudo apt-get install -y --no-install-recommends chromium cage wlr-randr >/dev/null
chmod +x "$FREON_DIR/deploy/nevera-hdmi.sh"
echo "== Servicio freon-nevera"
sudo tee /etc/systemd/system/freon-nevera.service >/dev/null <<UNIT
[Unit]
Description=La nevera por HDMI (visuales de Fuga de Freón)
After=freon-machine.service systemd-user-sessions.service
Wants=freon-machine.service
Conflicts=getty@tty1.service

[Service]
User=$USER_NAME
PAMName=login
TTYPath=/dev/tty1
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes
StandardInput=tty-fail
StandardOutput=journal
StandardError=journal
UtmpIdentifier=tty1
UtmpMode=user
Environment=XDG_SESSION_TYPE=wayland
ExecStart=$FREON_DIR/deploy/nevera-hdmi.sh
Restart=always
RestartSec=5
# la imagen nunca le quita nada al sonido
Nice=10
CPUWeight=20
IOWeight=20
MemoryMax=3G
OOMScoreAdjust=500

[Install]
WantedBy=multi-user.target
UNIT
sudo systemctl daemon-reload
sudo systemctl disable getty@tty1 >/dev/null 2>&1 || true
sudo systemctl enable freon-nevera >/dev/null 2>&1
sudo systemctl restart freon-nevera
echo "Listo. La nevera sale por el HDMI y se maneja desde la web de la Freon: Settings › Visuals."
