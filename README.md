# OAPW_HiFi_AL
## Open Ambiophonics Processing Window – ALSA Loopback Edition

**OAPW_HiFi_AL** ist eine für den Raspberry Pi optimierte Echtzeit-Audiosignalverarbeitung in C++. Die Software implementiert den *Recursive Ambiophonic Crosstalk Elimination* (RACE)-Algorithmus, basierend auf den wegweisenden Arbeiten von Ralph Glasgal und dem Ambiophonics Institute.

Ziel ist die Wiedergabe einer außergewöhnlich breiten, räumlich präzisen und holografisch wirkenden Stereobühne über zwei Lautsprecher – insbesondere in Setups mit geringem Lautsprecherabstand.

Die ALSA-Loopback-Edition verwendet ein virtuelles ALSA-Audiogerät als Verbindung zwischen Audio-Player und DSP-Engine. Dadurch kann das Audiosignal systemintern abgegriffen, verarbeitet und anschließend an den Hardware-DAC ausgegeben werden.

---

## 1. Systemanforderungen und Hardware

### Rechner

- **Raspberry Pi 5:** Empfohlene Plattform für anspruchsvolle DSP-Verarbeitung mit hohen Abtastraten bis 192 kHz.
- **Raspberry Pi 4:** Alternative Plattform; die erreichbare maximale Abtastrate hängt von Konfiguration und Systemlast ab.
- **Raspberry Pi Compute Module:** Ebenfalls als Basis für dedizierte Audio-Systeme geeignet.

Der Raspberry Pi 3 wurde mit dieser Programmversion noch nicht abschließend auf seine Leistungsfähigkeit getestet.

### Audio-Hardware

Erforderlich ist ein kompatibler Audio-DAC, beispielsweise als I²S-DAC-HAT.

Mögliche Optionen sind:

- Allo Katana
- Allo Boss DAC
- DAC HAT Pro

Die konkrete Konfiguration des ALSA-Gerätenamens muss an die installierte Hardware angepasst werden.

### Stromversorgung

Für einen hochwertigen Audioaufbau empfiehlt sich eine sorgfältig ausgelegte, rauscharme Stromversorgung. Ein separates lineares Netzteil für den DAC kann helfen, Störeinflüsse zu reduzieren, sofern die konkrete Hardwarekonfiguration davon profitiert.

### WLAN und Bluetooth

WLAN und Bluetooth können bei einem dedizierten Audio-System deaktiviert werden, um Funkaktivität und unnötige Systemlast zu reduzieren. Ein daraus resultierender Klangvorteil ist allerdings nicht pauschal garantiert.

---

## 2. Software-Voraussetzungen

Vorausgesetzt wird ein aktuelles 64-Bit-Debian-System, beispielsweise:

- Debian Trixie
- Raspberry Pi OS 64-Bit

Benötigte Komponenten:

- GCC/G++ mit C++17-Unterstützung
- GNU Make
- ALSA-Entwicklerbibliotheken
- Linux-Kernelmodul `snd-aloop`

### 2.1 Pakete installieren

```bash
sudo apt-get update
sudo apt-get install build-essential libasound2-dev
```

### 2.2 WLAN und Bluetooth optional deaktivieren

Für eine dauerhafte Deaktivierung können – sofern vom jeweiligen System unterstützt – folgende Einträge in `/boot/firmware/config.txt` ergänzt werden:

```ini
dtoverlay=disable-wifi
dtoverlay=disable-bt
```

Bei älteren Installationen kann die Konfigurationsdatei unter `/boot/config.txt` liegen. Anschließend ist ein Neustart erforderlich.

Alternativ lassen sich die Funkfunktionen temporär deaktivieren:

```bash
sudo rfkill block wifi
sudo rfkill block bluetooth
```

### 2.3 ALSA-Loopback aktivieren

Das Kernelmodul `snd-aloop` stellt ein virtuelles ALSA-Audiogerät bereit. Es dient als internes Audiokabel zwischen dem Player und der OAPW-DSP-Engine.

Modul laden:

```bash
sudo modprobe snd-aloop
```

Automatisches Laden beim Systemstart aktivieren:

```bash
echo snd-aloop | sudo tee -a /etc/modules
```

Prüfen, ob das Modul geladen wurde:

```bash
lsmod | grep snd_aloop
aplay -l
arecord -l
```

Die verfügbaren Loopback-Geräte und deren Bezeichnungen können je nach ALSA-Konfiguration variieren.

---

## 3. Signalfluss – ALSA Loopback Pipeline

Der Signalweg verbindet den Audio-Player mit der OAPW-DSP-Engine und dem Hardware-DAC.

```text
┌───────────────────────────────┐
│ Audio-Player                  │
│ z. B. MPD oder myMPD           │
└───────────────┬───────────────┘
                │
                ▼
┌───────────────────────────────┐
│ ALSA Loopback                 │
│ Playback: hw:Loopback,0,0     │
└───────────────┬───────────────┘
                │ Virtuelles Audiokabel
                ▼
┌───────────────────────────────┐
│ OAPW_HiFi_AL                  │
│ Capture: hw:Loopback,1,0      │
│                               │
│ • RACE Crosstalk Elimination  │
│ • Equalizer / DSP             │
└───────────────┬───────────────┘
                │
                ▼
┌───────────────────────────────┐
│ Hardware-DAC                  │
│ z. B. Allo Katana             │
└───────────────┬───────────────┘
                │
                ▼
┌───────────────────────────────┐
│ Verstärker und Lautsprecher   │
└───────────────────────────────┘
```

### Verarbeitungsschritte

1. Der Audio-Player gibt das Stereo-Signal auf der Playback-Seite des ALSA-Loopback-Geräts aus.
2. `OAPW_Player` liest das Signal über die zugehörige Capture-Seite ein.
3. Die DSP-Engine verarbeitet die Audiodaten mit dem RACE-Algorithmus und den konfigurierten Equalizer-Einstellungen.
4. Das verarbeitete Signal wird an das konfigurierte ALSA-Ausgabegerät und damit an den Hardware-DAC übergeben.
5. Der DAC speist die nachfolgende Verstärker- und Lautsprecherkette.

**Hinweis:** Die genannten ALSA-Gerätenamen sind Beispiele. Die tatsächlichen Gerätenummern, Kanalzuordnungen, Abtastraten und Sample-Formate müssen mit der implementierten Konfiguration von `OAPW_Player` übereinstimmen. Die Loopback-Verbindung selbst garantiert keine verlustfreie Verarbeitung, wenn bei Formatkonvertierungen oder in der DSP-Kette Änderungen auftreten.

---

## 4. Kompilieren und Starten

Wechsle in das Projektverzeichnis:

```bash
cd ~/OAPW_HiFi_AL
```

Kompiliere das Programm entsprechend der im Repository vorgesehenen Build-Konfiguration:

```bash
make
```

Starte anschließend den Player:

```bash
./OAPW_Player
```

Falls das Programm in einem separaten Build-Verzeichnis erzeugt wird, muss der Startbefehl entsprechend angepasst werden.

### Web-GUI

Sofern die Web-GUI aktiviert ist, lässt sich die Benutzeroberfläche über einen Browser im selben Netzwerk aufrufen:

```text
http://<IP-Adresse-des-Raspberry-Pi>:8080
```

`<IP-Adresse-des-Raspberry-Pi>` ist durch die tatsächliche IP-Adresse des Geräts zu ersetzen.

Über die Web-GUI können – entsprechend den implementierten Funktionen – die RACE-Parameter, die Lautsprechergeometrie und die Equalizer-Einstellungen in Echtzeit angepasst werden.

### Konfiguration speichern und laden

Das Programm verwendet die Konfigurationsdatei `oapw_config.txt`, um Einstellungen zu speichern und beim nächsten Start wieder zu laden.

Der tatsächliche Speicherort und das Verhalten bei fehlender oder fehlerhafter Konfigurationsdatei richten sich nach der Implementierung der aktuellen Programmversion.

## 5. Automatischer Start als Hintergrunddienst (systemd)

Damit `OAPW_Player` nach einem Neustart des Raspberry Pi automatisch gestartet wird und bei einem unerwarteten Programmabbruch selbstständig neu startet, kann die Anwendung als `systemd`-Dienst eingerichtet werden.

### 5.1 Dienst-Datei erstellen

Erstelle die Dienst-Datei mit folgendem Befehl:

```bash
sudo nano /etc/systemd/system/oapw.service
```

Füge den folgenden Konfigurationsblock vollständig ein:

```ini
[Unit]
Description=OAPW HiFi ALSA Loopback Player
After=network.target sound.target

[Service]
Type=simple
User=<Dein Username>
WorkingDirectory=/home/<Dein Username>/OAPW_HiFi_AL
ExecStart=/home/<Dein Username>/OAPW_HiFi_AL/OAPW_Player
StandardOutput=journal
StandardError=journal
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

**Wichtig:** Passe `User`, `WorkingDirectory` und `ExecStart` an deinen tatsächlichen Benutzernamen und den Installationspfad an. Der ausführbaren Datei `OAPW_Player` muss das Ausführungsrecht erteilt sein.

Speichere die Datei in `nano` mit `Strg+O`, bestätige mit `Enter` und beende den Editor mit `Strg+X`.

### 5.2 Dienst aktivieren und starten

Lade zunächst die systemd-Konfiguration neu:

```bash
sudo systemctl daemon-reload
```

Aktiviere den automatischen Start bei jedem Systemstart:

```bash
sudo systemctl enable oapw.service
```

Starte den Player unmittelbar im Hintergrund:

```bash
sudo systemctl start oapw.service
```

Damit wird `OAPW_Player` als Hintergrunddienst ausgeführt. Ein manueller Programmstart ist im regulären Betrieb nicht mehr erforderlich.

### 5.3 Status und Protokolle überprüfen

Den aktuellen Zustand des Dienstes kannst du jederzeit abfragen:

```bash
sudo systemctl status oapw.service
```

Die letzten Protokolleinträge einschließlich möglicher Fehlermeldungen erhältst du mit:

```bash
sudo journalctl -u oapw.service -n 50 --no-pager
```

Um die Protokollausgabe live zu verfolgen:

```bash
sudo journalctl -u oapw.service -f
```

### 5.4 Dienst steuern

Für Wartungsarbeiten, Updates und Tests stehen folgende Befehle zur Verfügung:

**Dienst stoppen:**

```bash
sudo systemctl stop oapw.service
```

**Dienst erneut starten:**

```bash
sudo systemctl restart oapw.service
```

**Automatischen Start deaktivieren:**

```bash
sudo systemctl disable oapw.service
```

**Automatischen Start wieder aktivieren:**

```bash
sudo systemctl enable oapw.service
```

### Hinweise zum Betrieb

- `Restart=always` sorgt dafür, dass systemd den Prozess nach einem Programmende erneut startet. Auch nach einem regulären Prozessende wird der Dienst grundsätzlich wieder gestartet; ein explizites Stoppen über `systemctl stop` löst dagegen keinen automatischen Neustart aus.
- `RestartSec=5` legt eine Wartezeit von fünf Sekunden vor einem Neustart fest.
- Die Ausgabe des Programms wird im systemd-Journal gespeichert und kann zur Fehlerdiagnose herangezogen werden.
- Da `OAPW_Player` direkt auf ALSA-Geräte zugreift, muss sichergestellt sein, dass der Dienst die erforderlichen Zugriffsrechte besitzt und das gewünschte DAC-Gerät verfügbar ist.
- Bei Verwendung des ALSA-Loopback-Moduls muss `snd-aloop` beim Start verfügbar sein. Falls erforderlich, kann die Dienst-Konfiguration um eine passende Modul-Ladeabhängigkeit ergänzt werden.

Nach erfolgreicher Einrichtung startet `OAPW_Player` bei jedem Bootvorgang automatisch und wird bei unerwarteten Programmabbrüchen von systemd neu gestartet.



---

## 6. Hinweise zur Audioqualität und Performance

Für einen stabilen Betrieb bei hohen Abtastraten sind insbesondere folgende Faktoren relevant:

- Ausreichende CPU-Leistung für die gewählte Abtastrate und DSP-Konfiguration
- Stabile ALSA-Gerätezuordnung und korrekte Kanalbelegung
- Vermeidung von Underruns und Overruns
- Geeignete Puffergrößen und Scheduling-Einstellungen
- Korrekte Behandlung von Sample-Formaten und numerischer Präzision
- Kompatibilität zwischen Audio-Player, Loopback-Gerät, DSP-Engine und DAC

Die Unterstützung von bis zu 192 kHz und 32 Bit setzt voraus, dass die gesamte Verarbeitungskette einschließlich Hardware-Ausgabe und Build-Konfiguration diese Betriebsart tatsächlich unterstützt.

---

## 7. Danksagung

Ein besonderer Dank gilt **Prof. Dr. Dieter Steinhilber**, dessen wertvolle Hinweise, technische Anregungen und tiefgreifendes Verständnis für High-End Audiophilie maßgeblich zur Weiterentwicklung und Reifung dieses Programms beigetragen haben.

Ein weiterer Dank gilt **Dr. Michael Graw**, meinem ehemaligen Ambiophonie-Mentor, der mir in meiner aktiven Zeit als CTO der Drägerwerk AG in Lübeck die Augen und vor allem die Ohren für die Ambiophonie geöffnet hat. 

Ebenso gilt unser Dank **Ralph Glasgal** für sein Lebenswerk, seine wegweisenden Forschungen am Ambiophonics Institute und die Entwicklung der Ambiophonie, die dieses Softwareprojekt inspiriert und ermöglicht haben.

Dieses Programm wurde mit Unterstützung durch Gemini und in kleinen Teilen auch ChatGPT entwickelt. Anregungen für das Design der Filter, des GUI und des Kern-Algorithmus stammen aus verschiedensten Quellen in der Fachliteratur und der audiophilen Community, thanks and keep sharing! 

---

*OAPW_HiFi_AL – Open Ambiophonics Processing Wrapper, High-End ALSA Loopback Edition.*
