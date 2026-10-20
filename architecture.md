# Architektur und Signalfluss (OAPW_HiFi_AL)

Dieses Dokument beschreibt die technischen Hintergründe und den Signalweg der OAPW Audio-Pipeline. Das primäre Ziel dieses Setups ist es, eine absolut latenzfreie, bitperfekte Echtzeit-Audioverarbeitung (RACE) zu garantieren und dabei modernen Streaming-Komfort (Linn Kazoo, UPnP) beizubehalten.

Um das Signal nicht durch unnötige Software-Schichten zu verfälschen, umgeht diese Architektur moderne Desktop-Soundserver wie **PulseAudio** oder **PipeWire** vollständig und setzt ausschließlich auf natives **ALSA** (Advanced Linux Sound Architecture).

## 📊 Der Signalweg im Detail

```text
[ Linn Kazoo / iOS ] 
        │ (OpenHome / UPnP Control)
        ▼
[ Pete Manchester MediaPlayer (Java) ] ──(liest von)── [ MiniDLNA Server ]
        │ (Übersetzt in mpd-Befehle)
        ▼
[ Music Player Daemon (mpd) ]
        │ (Decodiert FLAC/WAV, SoX Resampling auf 192kHz/32Bit)
        ▼
[ ALSA Loopback (hw:Loopback,0,0) ] ◄── Virtuelles Kabel (snd_aloop)
[ ALSA Loopback (hw:Loopback,1,0) ]
        │
        ▼
[ OAPW_Player (C++ DSP Engine) ]
        │ (RACE Ambiophonics Verarbeitung in Echtzeit)
        ▼
[ Hardware DAC (z.B. Allo Katana I2S) ]
        │
        ▼
[ High-End Verstärker & Lautsprecher ]
```

🧩 Die Kernkomponenten

1. Control Layer & Bibliothek (Netzwerk-Ebene)

Das Signal wird physisch nicht über das Netzwerk gestreamt. MiniDLNA stellt lediglich den Index der lokalen Musikbibliothek bereit. Linn Kazoo agiert als Fernbedienung (Control Point) und kommuniziert mit dem Pete Manchester MediaPlayer-Dienst. Dieser Dienst ist ein OpenHome-Wrapper, der als Dolmetscher fungiert: Er nimmt die schönen, modernen Kazoo-Befehle entgegen und übersetzt sie im Hintergrund in rudimentäre Befehle für den mpd.

2. Decoder & Resampler (mpd)

Der Music Player Daemon (mpd) ist das Herzstück der eigentlichen Wiedergabe. Er holt sich die lokale FLAC- oder WAV-Datei und decodiert sie. Da digitale Signalprozessoren (DSP) am besten mit konstanten Abtastraten arbeiten, zwingen wir mpd über den hochwertigen internen SoX-Resampler, jedes Audiosignal hochpräzise auf 192kHz bei 32-Bit Tiefe hochzurechnen (Upsampling), bevor es weitergereicht wird.

3. Die Brücke: ALSA Loopback (snd_aloop)

Das größte Problem unter Linux ist es, den Ton aus einem Player (mpd) abzufangen und in eine eigene Software (OAPW_Player) zu leiten, ohne dafür PipeWire zu nutzen. Die Lösung ist das Kernel-Modul snd_aloop. Es erzeugt eine virtuelle Soundkarte mit zwei Enden:

mpd spielt die Musik in das Ende hw:Loopback,0,0 ein.

Der OAPW_Player greift den Stream am anderen Ende hw:Loopback,1,0 bitgenau wieder ab.

4. DSP Engine: OAPW_Player

Dein eigener C++ Code läuft als systemd-Hintergrunddienst (oapw_player.service). Er liest permanent die 192kHz/32Bit-Frames aus dem Loopback, wendet die OAPW (Open Ambiophonic Processor Wrapper) RACE-Algorithmen an, um das akustische Übersprechen (Crosstalk) zwischen den Stereo-Lautsprechern zu eliminieren, und schreibt die veränderten Frames direkt in den Hardware-DAC.

5. Hardware DAC

Da PipeWire deaktiviert wurde, hat der OAPW_Player exklusiven Hardware-Zugriff ("Exclusive Lock") auf den I2S DAC (z.B. Allo Katana oder Sharkoon USB). Dies verhindert jegliches Einmischen des Betriebssystems in die finale Signalausgabe.

🚫 Warum PipeWire / PulseAudio deaktiviert wurden

Moderne Soundserver sind dafür gebaut, Systemklänge, Browser-Audio und Musik zu mischen. Das erfordert Resampling im Hintergrund, oft mit niedriger Qualität, und erzeugt Latenzen. Durch das globale Deaktivieren von PipeWire im Installationsskript zwingen wir den Raspberry Pi, Audio ausschließlich über unsere kontrollierte mpd -> Loopback -> DSP -> DAC Kette zu routen.

🧩 Die Kernkomponenten

1. Control Layer & Bibliothek (Netzwerk-Ebene)

Das Signal wird physisch nicht über das Netzwerk gestreamt. MiniDLNA stellt lediglich den Index der lokalen Musikbibliothek bereit. Linn Kazoo agiert als Fernbedienung (Control Point) und kommuniziert mit dem Pete Manchester MediaPlayer-Dienst. Dieser Dienst ist ein OpenHome-Wrapper, der als Dolmetscher fungiert: Er nimmt die schönen, modernen Kazoo-Befehle entgegen und übersetzt sie im Hintergrund in rudimentäre Befehle für den mpd.

2. Decoder & Resampler (mpd)

Der Music Player Daemon (mpd) ist das Herzstück der eigentlichen Wiedergabe. Er holt sich die lokale FLAC- oder WAV-Datei und decodiert sie. Da digitale Signalprozessoren (DSP) am besten mit konstanten Abtastraten arbeiten, zwingen wir mpd über den hochwertigen internen SoX-Resampler, jedes Audiosignal hochpräzise auf 192kHz bei 32-Bit Tiefe hochzurechnen (Upsampling), bevor es weitergereicht wird.

3. Die Brücke: ALSA Loopback (snd_aloop)

Das größte Problem unter Linux ist es, den Ton aus einem Player (mpd) abzufangen und in eine eigene Software (OAPW_Player) zu leiten, ohne dafür PipeWire zu nutzen. Die Lösung ist das Kernel-Modul snd_aloop. Es erzeugt eine virtuelle Soundkarte mit zwei Enden:

mpd spielt die Musik in das Ende hw:Loopback,0,0 ein.

Der OAPW_Player greift den Stream am anderen Ende hw:Loopback,1,0 bitgenau wieder ab.

4. DSP Engine: OAPW_Player

Dein eigener C++ Code läuft als systemd-Hintergrunddienst (oapw_player.service). Er liest permanent die 192kHz/32Bit-Frames aus dem Loopback, wendet die OAPW (Open Ambiophonic Processor Wrapper) RACE-Algorithmen an, um das akustische Übersprechen (Crosstalk) zwischen den Stereo-Lautsprechern zu eliminieren, und schreibt die veränderten Frames direkt in den Hardware-DAC.

5. Hardware DAC

Da PipeWire deaktiviert wurde, hat der OAPW_Player exklusiven Hardware-Zugriff ("Exclusive Lock") auf den I2S DAC (z.B. Allo Katana oder Sharkoon USB). Dies verhindert jegliches Einmischen des Betriebssystems in die finale Signalausgabe.

🚫 Warum PipeWire / PulseAudio deaktiviert wurden

Moderne Soundserver sind dafür gebaut, Systemklänge, Browser-Audio und Musik zu mischen. Das erfordert Resampling im Hintergrund, oft mit niedriger Qualität, und erzeugt Latenzen. Durch das globale Deaktivieren von PipeWire im Installationsskript zwingen wir den Raspberry Pi, Audio ausschließlich über unsere kontrollierte mpd -> Loopback -> DSP -> DAC Kette zu routen.
