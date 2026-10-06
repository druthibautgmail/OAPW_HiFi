# OAPW – Open Ambiophonics Processing Wrapper (HiFi Edition)

OAPW ist eine in C++17 geschriebene, hochoptimierte Audio-Processing-Engine für macOS und Raspberry Pi. Sie implementiert **Ralph Glasgals Recursive Ambiophonic Crosstalk Elimination (RACE)** Algorithmus, um aus regulären Stereo-Aufnahmen ein dreidimensionales, immersives Klangfeld über zwei eng beieinander stehende Lautsprecher zu erzeugen.

Diese **HiFi Edition (v2.0)** wurde von Grund auf für den **Raspberry Pi 5** und hochwertige I2S-DACs (wie den Allo Katana) neu geschrieben. Sie umgeht systembedingte Puffer-Probleme durch direkte ALSA-Hardware-Adressierung und hebt die Signalverarbeitung auf audiophiles Niveau.

## ✨ Hauptmerkmale der HiFi Architektur

*   **Audiophile DSP-Präzision:** Interne Verarbeitung durchgehend in **64-Bit Fließkomma (`double`)** bei einer nativen Sampling-Rate von **192 kHz**.
*   **Direct ALSA Output:** Bitperfekte Ausgabe in **32-Bit Integer (`S32_LE`)** direkt an die ALSA-Hardwareschnittstelle. Der DAC fungiert als *Clock Master*, Puffer-Jitter und Latenzprobleme asynchroner Pipes sind eliminiert.
*   **Biquad-Kaskaden:** Die alten IIR-Filter wurden durch extrem stabile, kaskadierte Biquad-Filter (4th Order, Linkwitz-Riley) ersetzt, um bei 192 kHz numerische Stabilität (keine NaNs) zu garantieren.
*   **Parametrischer EQ:** Integrierter 3-Band PEQ (Biquad).
*   **Live Web-Interface:** Eingebetteter HTTP-Server (Port 8080) zur Echtzeit-Steuerung der DSP-Parameter, inklusive Ambiophonics-Geometrie-Rechner und **64-Band Real-Time Analyzer (FFT)**.
*   **Hardware-Optimierung:** Nutzt ARM NEON SIMD-Befehlssätze (`-ffast-math`, `-ftree-vectorize`) für maximale Effizienz auf dem Cortex-A76 (Raspberry Pi 5).

---

## 🏗️ Signalfluss & Architektur

Das folgende Diagramm veranschaulicht den Weg des Audiosignals von der asynchronen Pipe-Quelle (z.B. Shairport-Sync) bis zur I2S-Ausgabe am DAC:

```mermaid
flowchart TD
    %% Define styling
    classDef hardware fill:#2d3436,stroke:#b2bec3,stroke-width:2px,color:#fff
    classDef dsp fill:#0984e3,stroke:#74b9ff,stroke-width:2px,color:#fff
    classDef web fill:#00b894,stroke:#55efc4,stroke-width:2px,color:#fff
    classDef pipe fill:#e17055,stroke:#ffeaa7,stroke-width:2px,color:#fff

    subgraph Source [1. Audio Quelle]
        A[Shairport Sync / myMPD] -->|16-Bit PCM | B[(FIFO Pipe<br>/tmp/oapw_stream)]:::pipe
    end

    subgraph OAPW [2. OAPW_Player Audio-Thread]
        B --> C[Format-Wandlung<br>16-Bit Int ➔ 64-Bit Float]
        
        subgraph DSP [DSP Engine @ 192 kHz / 64-Bit double]
            C --> D{RACE aktiv?}
            D -- Ja --> E[Frequenzweiche<br>4th Order Biquad LPF/HPF]
            E --> F[RACE Algorithmus<br>Cross-Talk Cancellation]
            F --> G[Parametrischer EQ<br>3-Band Biquad]
            D -- Nein --> G
            G --> H[Volume & Hard-Clipping]
        end
        
        H --> I[Format-Wandlung<br>64-Bit Float ➔ 32-Bit Int]
        H -.->|Mono-Mix Abgriff| J[Spectrum Ringpuffer]
    end

    subgraph WebUI [3. Web Server & UI]
        J -.->|Lock-free Read| K[Hann Window & FFT]:::web
        K --> L[Analyzer 64-Bands<br>Browser via Port 8080]:::web
    end

    subgraph Hardware [4. ALSA & DAC]
        I -->|snd_pcm_writei<br>Blockiert Thread bei vollem Puffer| M[ALSA Treiber<br>Clock Master / S32_LE]:::hardware
        M -->|I2S Bus| N[HiFi DAC z.B. Allo Katana<br>hw:X,Y]:::hardware
        N --> O(((HiFi Anlage)))
    end
    
    class C,D,E,F,G,H,I dsp
