#!/bin/sh
# Wifi propia «Freon-Machine» con hostapd (WPA2 clásico, el único que el chip de la Pi maneja bien con móviles).
# Se ejecuta una vez en la Pi, desde la carpeta del proyecto:  ./deploy/wifi-propia.sh
# Usa la clave que ya pusiste con ajustes-pi.sh (o te la pregunta si no la encuentra).
#
# Cómo funciona: al encender, la Pi espera hasta 90 s a conectarse a una wifi conocida. Si no lo consigue,
# crea «Freon-Machine»: te conectas desde el móvil y abres http://10.42.0.1:8080. En casa no hace nada.
set -e
USER_NAME="$(id -un)"
CLAVE="${1:-}"
[ ${#CLAVE} -ge 8 ] || CLAVE="$(nmcli -s -g 802-11-wireless-security.psk connection show Freon-Machine 2>/dev/null || true)"
while [ ${#CLAVE} -lt 8 ]; do printf "Clave para la wifi de Freon (8 caracteres o más): "; read -r CLAVE; done
echo "Te pedirá la contraseña de la Pi (la de entrar como $USER_NAME)."

echo "== Instalo hostapd"
sudo apt-get install -y hostapd >/dev/null
sudo systemctl disable --now hostapd >/dev/null 2>&1 || true
sudo systemctl unmask hostapd >/dev/null 2>&1 || true

echo "== Configuración de la wifi"
sudo tee /etc/hostapd/freon.conf >/dev/null <<EOF
interface=wlan0
driver=nl80211
ssid=Freon-Machine
country_code=ES
hw_mode=g
channel=6
ieee80211n=1
wmm_enabled=1
auth_algs=1
wpa=2
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
wpa_passphrase=$CLAVE
EOF
sudo chmod 600 /etc/hostapd/freon.conf

echo "== Programa que la enciende si no hay wifi conocida"
sudo tee /usr/local/sbin/freon-wifi >/dev/null <<'EOF'
#!/bin/sh
# freon-wifi            al arrancar: espera a una wifi conocida y si no la hay crea «Freon-Machine»
# freon-wifi propia     crea «Freon-Machine» ya
# freon-wifi casa       vuelve a la wifi normal
modo="${1:-auto}"
propia() {
  nmcli device set wlan0 managed no 2>/dev/null; sleep 1
  pkill -f "hostapd /etc/hostapd/freon.conf" 2>/dev/null; [ -f /run/freon-wifi-dnsmasq.pid ] && kill "$(cat /run/freon-wifi-dnsmasq.pid)" 2>/dev/null
  ip addr flush dev wlan0; ip link set wlan0 up; ip addr add 10.42.0.1/24 dev wlan0
  hostapd -B /etc/hostapd/freon.conf || { logger -t freon-wifi "hostapd no arranca"; casa; exit 1; }
  dnsmasq --conf-file=/dev/null --interface=wlan0 --bind-interfaces --except-interface=lo --dhcp-range=10.42.0.10,10.42.0.100,255.255.255.0,12h \
    --dhcp-option=3 --address=/freonmachine.local/10.42.0.1 --pid-file=/run/freon-wifi-dnsmasq.pid --log-facility=-
  logger -t freon-wifi "wifi propia Freon-Machine encendida (10.42.0.1)"
}
casa() {
  pkill -f "hostapd /etc/hostapd/freon.conf" 2>/dev/null; [ -f /run/freon-wifi-dnsmasq.pid ] && kill "$(cat /run/freon-wifi-dnsmasq.pid)" 2>/dev/null
  ip addr flush dev wlan0; nmcli device set wlan0 managed yes; logger -t freon-wifi "vuelta a la wifi normal"
}
case "$modo" in
  propia) propia ;;
  casa) casa ;;
  *) for i in $(seq 1 45); do nmcli -t -f DEVICE,STATE device 2>/dev/null | grep -q '^wlan0:connected$' && exit 0; sleep 2; done
     logger -t freon-wifi "no hay wifi conocida: creo la propia"; propia ;;
esac
EOF
sudo chmod 755 /usr/local/sbin/freon-wifi

sudo tee /etc/systemd/system/freon-wifi.service >/dev/null <<'EOF'
[Unit]
Description=Freon Machine: wifi propia si no hay una conocida
After=NetworkManager.service
Wants=NetworkManager.service

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/freon-wifi

[Install]
WantedBy=multi-user.target
EOF
sudo systemctl daemon-reload
sudo systemctl enable freon-wifi.service >/dev/null 2>&1

echo "== Freon (y tú) podéis cambiar de wifi sin contraseña"
echo "$USER_NAME ALL=(root) NOPASSWD: /usr/local/sbin/freon-wifi" | sudo tee /etc/sudoers.d/freon-wifi >/dev/null
sudo chmod 440 /etc/sudoers.d/freon-wifi

echo "== La wifi propia del gestor de red ya no se usa"
sudo nmcli connection delete Freon-Machine >/dev/null 2>&1 || true

echo
echo "Listo. Sin wifi conocida, al minuto y medio de encender aparece «Freon-Machine»: http://10.42.0.1:8080"
