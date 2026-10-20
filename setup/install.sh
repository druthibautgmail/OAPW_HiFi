#!/bin/bash
# install.sh - OAPW HiFi Audio Pipeline Setup
# Führe dieses Skript im Hauptverzeichnis des Projekts aus: sudo ./setup/install.sh

set -e

if [ "$EUID" -ne 0 ]; then
  echo "Fehler: Bitte mit sudo ausführen (sudo ./setup/install.sh)"
  exit 1
fi

# Echten Benutzer und Projektpfad ermitteln (wichtig für systemd)
REAL_USER=${SUDO_USER:-$(whoami)}
PROJECT_DIR=$(dirname $(dirname $(readlink -f $0)))

echo "=== OAPW HiFi ALSA Loopback Setup ==="

echo "[1/6] Deaktiviere PipeWire global für exklusiven ALSA-Zugriff..."
systemctl --global disable pipewire pipewire-pulse wireplumber || true
# Versuche laufende Instanzen zu stoppen (Fehler ignorieren, falls nicht aktiv)
systemctl --global stop pipewire pipewire.socket pipewire-pulse pipewire-pulse.socket wireplumber 2>/dev/null || true

echo "[2/6] Deaktiviere WLAN dauerhaft (Optimierung für Multicast/Kazoo über LAN)..."
if ! grep -q "dtoverlay=disable-wifi" /boot/firmware/config.txt; then
    echo "dtoverlay=disable-wifi" >> /boot/firmware/config.txt
    echo "  -> dtoverlay in /boot/firmware/config.txt eingetragen."
else
    echo "  -> WLAN ist bereits deaktiviert."
fi

echo "[3/6] Richte ALSA Loopback (snd_aloop) für den Autostart ein..."
if ! grep -q "snd_aloop" /etc/modules; then
    echo "snd_aloop" >> /etc/modules
    echo "  -> snd_aloop zu /etc/modules hinzugefügt."
fi
modprobe snd_aloop || true

echo "[4/6] Installiere mpd und minidlna..."
apt update
apt install -y mpd minidlna

echo "[5/6] Erstelle systemd-Dienst für OAPW_Player..."
cat > /etc/systemd/system/oapw_player.service <<EOF
[Unit]
Description=OAPW DSP Player (ALSA Loopback)
After=sound.target mpd.service

[Service]
Type=simple
User=$REAL_USER
WorkingDirectory=$PROJECT_DIR
ExecStart=$PROJECT_DIR/OAPW_Player
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable oapw_player
echo "  -> oapw_player.service erstellt und aktiviert."

echo "[6/6] Abschluss..."
echo "Installation der Basis-Infrastruktur abgeschlossen."
echo "Bitte die Konfigurationsdateien (mpd.conf, minidlna.conf) aus setup/configs/ ins System kopieren."
echo "Danach den Raspberry Pi neu starten: sudo reboot"
