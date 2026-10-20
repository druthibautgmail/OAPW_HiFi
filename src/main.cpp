#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <clocale>

#include "httplib.h"
#include "LockFreeRingBuffer.h"
#include "AlsaCapture.h"
#include "AlsaPlayback.h"
#include "RACEDspEngine.h"

// Globale Flag für das saubere Beenden durch SIGINT (Strg+C) oder systemctl stop
std::atomic<bool> g_running{true};

// Dateiname für die Konfiguration
const std::string CONFIG_FILE = "oapw_config.txt";

void signal_handler(int signum) {
    std::cout << "\nSignal " << signum << " empfangen. Beende OAPW_Player_HiFi..." << std::endl;
    g_running.store(false, std::memory_order_release);
}

// --- Konfiguration Speichern & Laden ---
void saveConfig(RACEDspEngine* engine) {
    std::ofstream file(CONFIG_FILE);
    if (file.is_open()) {
        file << "vol=" << engine->getVolume() << "\n";
        file << "delay=" << engine->getDelayUs() << "\n";
        file << "att=" << engine->getAttenuationDb() << "\n";
        file << "center=" << engine->getCenterP() << "\n";
        file << "freq=" << engine->getCrossoverFreq() << "\n";
        file << "race=" << (engine->getRaceEnabled() ? "1" : "0") << "\n";
        file << "filters=" << (engine->getFiltersEnabled() ? "1" : "0") << "\n";
        file << "eqActive=" << (engine->getEqEnabled() ? "1" : "0") << "\n";
        for(int b=0; b<3; b++) {
            file << "eq" << b << "f=" << engine->getEqFreq(b) << "\n";
            file << "eq" << b << "q=" << engine->getEqQ(b) << "\n";
            file << "eq" << b << "g=" << engine->getEqGain(b) << "\n";
        }
        file.close();
        std::cout << "[Config] Einstellungen in " << CONFIG_FILE << " gespeichert." << std::endl;
    }
}

void loadConfig(RACEDspEngine* engine) {
    std::ifstream file(CONFIG_FILE);
    if (!file.is_open()) {
        std::cout << "[Config] Keine bestehende Config gefunden. Nutze Defaults." << std::endl;
        return;
    }
    std::string line;
    double delay = 68.0, att = -2.3, center = 0.1; 
    
    while (std::getline(file, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        double val = 0.0;
        try { val = std::stod(line.substr(pos + 1)); } catch(...) { continue; }
        
        if (key == "vol") engine->setVolume(val);
        else if (key == "race") engine->setRaceEnabled(val != 0);
        else if (key == "filters") engine->setFiltersEnabled(val != 0);
        else if (key == "eqActive") engine->setEqEnabled(val != 0);
        else if (key == "delay") delay = val;
        else if (key == "att") att = val;
        else if (key == "center") center = val;
        else if (key == "freq") engine->setCrossoverFreq(val);
        else if (key.substr(0, 2) == "eq" && key.length() == 4) {
            int b = key[2] - '0';
            char p = key[3];
            if (b >= 0 && b < 3) {
                double f = engine->getEqFreq(b);
                double q = engine->getEqQ(b);
                double g = engine->getEqGain(b);
                if (p == 'f') f = val;
                if (p == 'q') q = val;
                if (p == 'g') g = val;
                engine->setEqBand(b, f, q, g);
            }
        }
    }
    engine->setParameters(delay, att, center, engine->getFreqLimit());
    std::cout << "[Config] Einstellungen aus " << CONFIG_FILE << " geladen." << std::endl;
}

void resetToDefaults(RACEDspEngine* engine) {
    engine->setVolume(0.5);
    engine->setRaceEnabled(true);
    engine->setFiltersEnabled(true);
    engine->setEqEnabled(false);
    engine->setCrossoverFreq(400.0);
    engine->setParameters(68.0, -2.3, 0.1, true);
    
    // Default EQ Bands
    engine->setEqBand(0, 100.0, 0.7, 0.0);
    engine->setEqBand(1, 1000.0, 0.7, 0.0);
    engine->setEqBand(2, 5000.0, 0.7, 0.0);
}


// Das erweiterte Web-Interface
const char* HTML_CONTENT = R"HTML(
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>OAPW Control - 192kHz</title>
    <style>
        body { background-color: #121212; color: #ffffff; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; padding: 2rem; max-width: 600px; margin: 0 auto; }
        h1 { font-weight: 300; border-bottom: 1px solid #333; padding-bottom: 10px; margin-bottom: 1rem; }
        .control-group { margin-bottom: 1.5rem; background: #1e1e1e; padding: 1.5rem; border-radius: 8px; }
        .calc-group { border: 1px solid #007aff; }
        .eq-band { border-top: 1px solid #333; padding-top: 1rem; margin-top: 1rem; }
        label { display: flex; justify-content: space-between; margin-bottom: 0.5rem; font-weight: bold; font-size: 0.9rem; }
        
        .control-row { display: flex; align-items: center; gap: 10px; margin-bottom: 1rem; }
        .control-row input[type="range"] { margin-bottom: 0; flex-grow: 1; cursor: pointer; }
        
        .stepper-buttons { display: flex; flex-direction: column; gap: 4px; }
        .stepper-buttons button { width: 36px; padding: 6px; margin: 0; font-size: 10px; border-radius: 4px; background: #333; }
        .stepper-buttons button:hover { background: #007aff; }

        .action-row { display: flex; gap: 10px; margin-bottom: 1.5rem; }
        .action-row button { margin-bottom: 0; padding: 12px; font-size: 1rem; flex: 1; }

        input[type="range"] { width: 100%; cursor: pointer; margin-bottom: 1rem; }
        input[type="number"] { width: 100%; padding: 10px; margin-bottom: 15px; background: #333; color: white; border: none; border-radius: 4px; box-sizing: border-box; font-size: 1rem; }
        .value-display { font-weight: normal; color: #aaa; }
        button { width: 100%; padding: 15px; font-size: 1.2rem; font-weight: bold; border: none; border-radius: 8px; cursor: pointer; background: #007aff; color: white; margin-bottom: 1.0rem; transition: background 0.3s;}
        button.off { background: #333; color: #888; }

        button.btn-save { background: #007aff; }
        button.btn-save:hover { background: #0a84ff; }
        button.btn-reset { background: #333333; color: #aaaaaa; }
        button.btn-reset:hover { background: #5a2a2a; color: #ffffff; }

        h3 { margin-top: 0; margin-bottom: 1rem; font-weight: 500; color: #007aff; }
        h4 { margin-top: 0; margin-bottom: 1rem; color: #fff; font-size: 1rem; }
        canvas { width: 100%; height: 180px; background: #000; border-radius: 8px; margin-bottom: 1rem; display: block; border: 1px solid #333; }
    </style>
</head>
<body>
    <h1>OAPW Ambiophonics HiFi (192kHz)</h1>
    
    <div class="action-row">
        <button class="btn-save" onclick="saveConfig()">💾 Config Speichern</button>
        <button class="btn-reset" onclick="resetDefaults()">↺ Reset Defaults</button>
    </div>

    <button id="btnRace" onclick="toggleRace()">RACE: AN</button>
    <button id="btnFilters" onclick="toggleFilters()">Filter: AN</button>
    <button id="btnEq" onclick="toggleEq()">EQ: AUS</button>

    <div class="control-group">
        <h3>Real-Time Analyzer [Hz]</h3>
        <canvas id="analyzer" width="600" height="180"></canvas>
    </div>

    <div class="control-group calc-group">
        <h3>Geometrie-Rechner</h3>
        <label>Abstand Lautsprecher (Mitte-Mitte in cm)</label>
        <input type="number" id="spkDist" value="30">
        <label>Abstand Hörer zur Basislinie (in cm)</label>
        <input type="number" id="listDist" value="80">
        <button onclick="calcGeometry()" style="margin-bottom: 0; background: #0a84ff;">Berechnen & Anwenden</button>
    </div>

    <div class="control-group">
        <h3>DSP Parameter</h3>
        <label>Lautstärke <span id="valVol" class="value-display">50%</span></label>
        <div class="control-row">
            <input type="range" id="vol" min="0" max="100" value="50" oninput="updateParam('vol', this.value)">
            <div class="stepper-buttons">
                <button onclick="stepValue('vol', 1)">▲</button>
                <button onclick="stepValue('vol', -1)">▼</button>
            </div>
        </div>

        <label>Interaural Delay <span id="valDelay" class="value-display">68 &micro;s</span></label>
        <div class="control-row">
            <input type="range" id="delay" min="0" max="200" step="1" value="68" oninput="updateParam('delay', this.value)">
            <div class="stepper-buttons">
                <button onclick="stepValue('delay', 1)">▲</button>
                <button onclick="stepValue('delay', -1)">▼</button>
            </div>
        </div>

        <label>Filter Trennfrequenz (RACE) <span id="valFreq" class="value-display">400 Hz</span></label>
        <div class="control-row">
            <input type="range" id="freq" min="100" max="1000" step="10" value="400" oninput="updateParam('freq', this.value)">
            <div class="stepper-buttons">
                <button onclick="stepValue('freq', 10)">▲</button>
                <button onclick="stepValue('freq', -10)">▼</button>
            </div>
        </div>

        <label>Attenuation <span id="valAtt" class="value-display">-2.3 dB</span></label>
        <div class="control-row">
            <input type="range" id="att" min="-12.0" max="0.0" step="0.1" value="-2.3" oninput="updateParam('att', this.value)">
            <div class="stepper-buttons">
                <button onclick="stepValue('att', 0.1)">▲</button>
                <button onclick="stepValue('att', -0.1)">▼</button>
            </div>
        </div>

        <label>Center Parameter <span id="valCenter" class="value-display">0.10</span></label>
        <div class="control-row">
            <input type="range" id="center" min="0" max="100" value="10" step="1" oninput="updateParam('center', this.value)">
            <div class="stepper-buttons">
                <button onclick="stepValue('center', 1)">▲</button>
                <button onclick="stepValue('center', -1)">▼</button>
            </div>
        </div>
    </div>

    <div class="control-group">
        <h3>Parametrischer Equalizer (PEQ)</h3>
        
        <div class="eq-band">
            <h4>Band 1 (Bass)</h4>
            <label>Frequenz <span id="valEq0F" class="value-display">100 Hz</span></label>
            <div class="control-row">
                <input type="range" id="eq0F" min="20" max="500" step="1" value="100" oninput="updateEq(0, 'f', this.value)">
                <div class="stepper-buttons">
                    <button onclick="stepEq(0, 'f', 1)">▲</button>
                    <button onclick="stepEq(0, 'f', -1)">▼</button>
                </div>
            </div>
            
            <label>Q-Faktor <span id="valEq0Q" class="value-display">0.7</span></label>
            <div class="control-row">
                <input type="range" id="eq0Q" min="0.1" max="5.0" step="0.1" value="0.7" oninput="updateEq(0, 'q', this.value)">
                <div class="stepper-buttons">
                    <button onclick="stepEq(0, 'q', 0.1)">▲</button>
                    <button onclick="stepEq(0, 'q', -0.1)">▼</button>
                </div>
            </div>

            <label>Gain <span id="valEq0G" class="value-display">0.0 dB</span></label>
            <div class="control-row">
                <input type="range" id="eq0G" min="-15" max="15" step="0.5" value="0" oninput="updateEq(0, 'g', this.value)">
                <div class="stepper-buttons">
                    <button onclick="stepEq(0, 'g', 0.5)">▲</button>
                    <button onclick="stepEq(0, 'g', -0.5)">▼</button>
                </div>
            </div>
        </div>

        <div class="eq-band">
            <h4>Band 2 (Mitten)</h4>
            <label>Frequenz <span id="valEq1F" class="value-display">1000 Hz</span></label>
            <div class="control-row">
                <input type="range" id="eq1F" min="200" max="5000" step="10" value="1000" oninput="updateEq(1, 'f', this.value)">
                <div class="stepper-buttons">
                    <button onclick="stepEq(1, 'f', 10)">▲</button>
                    <button onclick="stepEq(1, 'f', -10)">▼</button>
                </div>
            </div>

            <label>Q-Faktor <span id="valEq1Q" class="value-display">0.7</span></label>
            <div class="control-row">
                <input type="range" id="eq1Q" min="0.1" max="5.0" step="0.1" value="0.7" oninput="updateEq(1, 'q', this.value)">
                <div class="stepper-buttons">
                    <button onclick="stepEq(1, 'q', 0.1)">▲</button>
                    <button onclick="stepEq(1, 'q', -0.1)">▼</button>
                </div>
            </div>

            <label>Gain <span id="valEq1G" class="value-display">0.0 dB</span></label>
            <div class="control-row">
                <input type="range" id="eq1G" min="-15" max="15" step="0.5" value="0" oninput="updateEq(1, 'g', this.value)">
                <div class="stepper-buttons">
                    <button onclick="stepEq(1, 'g', 0.5)">▲</button>
                    <button onclick="stepEq(1, 'g', -0.5)">▼</button>
                </div>
            </div>
        </div>

        <div class="eq-band">
            <h4>Band 3 (Höhen)</h4>
            <label>Frequenz <span id="valEq2F" class="value-display">5000 Hz</span></label>
            <div class="control-row">
                <input type="range" id="eq2F" min="2000" max="20000" step="100" value="5000" oninput="updateEq(2, 'f', this.value)">
                <div class="stepper-buttons">
                    <button onclick="stepEq(2, 'f', 100)">▲</button>
                    <button onclick="stepEq(2, 'f', -100)">▼</button>
                </div>
            </div>

            <label>Q-Faktor <span id="valEq2Q" class="value-display">0.7</span></label>
            <div class="control-row">
                <input type="range" id="eq2Q" min="0.1" max="5.0" step="0.1" value="0.7" oninput="updateEq(2, 'q', this.value)">
                <div class="stepper-buttons">
                    <button onclick="stepEq(2, 'q', 0.1)">▲</button>
                    <button onclick="stepEq(2, 'q', -0.1)">▼</button>
                </div>
            </div>

            <label>Gain <span id="valEq2G" class="value-display">0.0 dB</span></label>
            <div class="control-row">
                <input type="range" id="eq2G" min="-15" max="15" step="0.5" value="0" oninput="updateEq(2, 'g', this.value)">
                <div class="stepper-buttons">
                    <button onclick="stepEq(2, 'g', 0.5)">▲</button>
                    <button onclick="stepEq(2, 'g', -0.5)">▼</button>
                </div>
            </div>
        </div>
    </div>

    <script>
        let raceActive = true;
        let filtersActive = true;
        let eqActive = false;

        function updateParam(param, value) {
            if(param === 'vol') document.getElementById('valVol').innerHTML = value + '%';
            if(param === 'delay') document.getElementById('valDelay').innerHTML = value + ' &micro;s';
            if(param === 'att') document.getElementById('valAtt').innerHTML = parseFloat(value).toFixed(1) + ' dB';
            if(param === 'center') document.getElementById('valCenter').innerHTML = (value/100).toFixed(2);
            if(param === 'freq') document.getElementById('valFreq').innerHTML = value + ' Hz';
            fetch(`/api/update?${param}=${value}`);
        }

        function stepValue(sliderId, step) {
            const slider = document.getElementById(sliderId);
            let currentValue = parseFloat(slider.value);
            let newValue = currentValue + step;
            const min = parseFloat(slider.min);
            const max = parseFloat(slider.max);
            
            if (newValue < min) newValue = min;
            if (newValue > max) newValue = max;
            
            slider.value = (sliderId === 'att') ? newValue.toFixed(1) : Math.round(newValue);
            updateParam(sliderId, slider.value);
        }

        function updateEq(band, param, value) {
            let labelId = 'valEq' + band + param.toUpperCase();
            if(param === 'f') document.getElementById(labelId).innerHTML = value + ' Hz';
            if(param === 'q') document.getElementById(labelId).innerHTML = parseFloat(value).toFixed(1);
            if(param === 'g') document.getElementById(labelId).innerHTML = parseFloat(value).toFixed(1) + ' dB';
            fetch(`/api/update?eq${band}${param}=${value}`);
        }

        function stepEq(band, param, step) {
            const sliderId = 'eq' + band + param.toUpperCase();
            const slider = document.getElementById(sliderId);
            let newValue = parseFloat(slider.value) + step;
            const min = parseFloat(slider.min);
            const max = parseFloat(slider.max);
            
            if (newValue < min) newValue = min;
            if (newValue > max) newValue = max;
            
            slider.value = (param === 'g' || param === 'q') ? newValue.toFixed(1) : Math.round(newValue);
            updateEq(band, param, slider.value);
        }

        function toggleRace() {
            raceActive = !raceActive;
            let btn = document.getElementById('btnRace');
            btn.innerText = raceActive ? "RACE: AN" : "RACE: AUS";
            btn.className = raceActive ? "" : "off";
            fetch(`/api/update?race=${raceActive ? 1 : 0}`);
        }

        function toggleFilters() {
            filtersActive = !filtersActive;
            let btn = document.getElementById('btnFilters');
            btn.innerText = filtersActive ? "Filter: AN" : "Filter: AUS";
            btn.className = filtersActive ? "" : "off";
            fetch(`/api/update?filters=${filtersActive ? 1 : 0}`);
        }

        function toggleEq() {
            eqActive = !eqActive;
            let btn = document.getElementById('btnEq');
            btn.innerText = eqActive ? "EQ: AN" : "EQ: AUS";
            btn.className = eqActive ? "" : "off";
            fetch(`/api/update?eq=${eqActive ? 1 : 0}`);
        }

        function saveConfig() {
            fetch('/api/save').then(() => alert('Die Konfiguration wurde erfolgreich gespeichert!'));
        }

        function resetDefaults() {
            if(confirm('Möchtest du wirklich alle DSP Parameter auf ihre Standardwerte zurücksetzen?')) {
                fetch('/api/reset').then(() => {
                    setTimeout(() => location.reload(), 200);
                });
            }
        }

        function calcGeometry() {
            let ds = parseFloat(document.getElementById('spkDist').value);
            let dl = parseFloat(document.getElementById('listDist').value);
            if(isNaN(ds) || isNaN(dl) || ds <= 0 || dl <= 0) return;
            let earDist = 17.5; 
            let c = 34.32;      
            let d_ipsi = Math.sqrt(Math.pow(dl, 2) + Math.pow(ds/2 - earDist/2, 2));
            let d_contra = Math.sqrt(Math.pow(dl, 2) + Math.pow(ds/2 + earDist/2, 2));
            let delta_d = d_contra - d_ipsi;
            let idealDelay = Math.round((delta_d / c) * 1000); 
            let geoLoss = 20 * Math.log10(d_ipsi / d_contra);
            let idealAtt = geoLoss - (idealDelay * 0.015); 
            
            if (idealDelay > 200) idealDelay = 200;
            if (idealDelay < 0) idealDelay = 0;
            if (idealAtt < -12) idealAtt = -12;
            if (idealAtt > 0) idealAtt = 0;
            
            document.getElementById('delay').value = idealDelay;
            updateParam('delay', idealDelay);
            let attRounded = (Math.round(idealAtt * 10) / 10).toFixed(1);
            document.getElementById('att').value = attRounded;
            updateParam('att', attRounded);
        }

        const canvas = document.getElementById('analyzer');
        const ctx = canvas.getContext('2d');
        
        function drawSpectrum() {
            fetch('/api/spectrum')
                .then(r => r.json())
                .then(data => {
                    ctx.clearRect(0, 0, canvas.width, canvas.height);
                    let labelHeight = 20;
                    let graphHeight = canvas.height - labelHeight;
                    let barWidth = (canvas.width / data.length) - 1.5;
                    let x = 0;
                    
                    let gradient = ctx.createLinearGradient(0, graphHeight, 0, 0);
                    gradient.addColorStop(0, '#007aff');
                    gradient.addColorStop(0.5, '#0a84ff');
                    gradient.addColorStop(1, '#ff3b30');
                    
                    for(let i = 0; i < data.length; i++) {
                        let barHeight = data[i] * graphHeight;
                        ctx.fillStyle = gradient;
                        ctx.fillRect(x, graphHeight - barHeight, barWidth, barHeight);
                        x += barWidth + 1.5;
                    }
                    
                    const minLog = Math.log10(20);
                    const maxLog = Math.log10(22050);
                    const logRange = maxLog - minLog;
                    
                    const labels = [20, 50, 100, 500, 1000, 5000, 10000, 20000];
                    const labelTexts = ['20', '50', '100', '500', '1k', '5k', '10k', '20k'];
                    
                    ctx.font = '11px -apple-system, BlinkMacSystemFont, sans-serif';
                    for (let i = 0; i < labels.length; i++) {
                        let pos = (Math.log10(labels[i]) - minLog) / logRange;
                        let labelX = pos * canvas.width;
                        ctx.fillStyle = 'rgba(255, 255, 255, 0.25)';
                        ctx.fillRect(labelX, 0, 1, graphHeight);
                        ctx.fillStyle = '#888';
                        if (i === 0) { ctx.textAlign = 'left'; labelX += 2; }
                        else if (i === labels.length - 1) { ctx.textAlign = 'right'; labelX -= 2; }
                        else { ctx.textAlign = 'center'; }
                        ctx.fillText(labelTexts[i], labelX, canvas.height - 4);
                    }
                    setTimeout(drawSpectrum, 50);
                })
                .catch(e => { setTimeout(drawSpectrum, 1000); });
        }

        window.onload = () => {
            fetch('/api/status').then(r => r.json()).then(data => {
                document.getElementById('vol').value = data.volume * 100;
                document.getElementById('valVol').innerHTML = Math.round(data.volume * 100) + '%';
                document.getElementById('delay').value = data.delayUs;
                document.getElementById('valDelay').innerHTML = data.delayUs + ' &micro;s';
                document.getElementById('att').value = data.attenuationDb;
                document.getElementById('valAtt').innerHTML = data.attenuationDb.toFixed(1) + ' dB';
                document.getElementById('center').value = data.centerP * 100;
                document.getElementById('valCenter').innerHTML = data.centerP.toFixed(2);
                document.getElementById('freq').value = data.freqLimit;
                document.getElementById('valFreq').innerHTML = data.freqLimit + ' Hz';

                raceActive = data.raceActive;
                document.getElementById('btnRace').innerText = raceActive ? "RACE: AN" : "RACE: AUS";
                document.getElementById('btnRace').className = raceActive ? "" : "off";
                filtersActive = data.filtersActive;
                document.getElementById('btnFilters').innerText = filtersActive ? "Filter: AN" : "Filter: AUS";
                document.getElementById('btnFilters').className = filtersActive ? "" : "off";
                eqActive = data.eqActive;
                document.getElementById('btnEq').innerText = eqActive ? "EQ: AN" : "EQ: AUS";
                document.getElementById('btnEq').className = eqActive ? "" : "off";

                for(let b=0; b<3; b++) {
                    document.getElementById('eq'+b+'F').value = data.eq[b].f;
                    document.getElementById('valEq'+b+'F').innerHTML = data.eq[b].f + ' Hz';
                    document.getElementById('eq'+b+'Q').value = data.eq[b].q;
                    document.getElementById('valEq'+b+'Q').innerHTML = data.eq[b].q.toFixed(1);
                    document.getElementById('eq'+b+'G').value = data.eq[b].g;
                    document.getElementById('valEq'+b+'G').innerHTML = data.eq[b].g.toFixed(1) + ' dB';
                }
            });
            drawSpectrum();
        };
    </script>
</body>
</html>
)HTML";

// --- Asynchroner Web-Server Thread ---
void webServerLoop(RACEDspEngine* dspEngine) {
    httplib::Server svr;

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(HTML_CONTENT, "text/html");
    });

    // Neue API Route: Speichern der Config
    svr.Get("/api/save", [dspEngine](const httplib::Request&, httplib::Response& res) {
        saveConfig(dspEngine);
        res.set_content("Saved", "text/plain");
    });

    // Neue API Route: Reset auf Defaults
    svr.Get("/api/reset", [dspEngine](const httplib::Request&, httplib::Response& res) {
        resetToDefaults(dspEngine);
        res.set_content("Reset", "text/plain");
    });

    svr.Get("/api/status", [dspEngine](const httplib::Request&, httplib::Response& res) {
        std::string json = "{";
        json += "\"volume\":" + std::to_string(dspEngine->getVolume()) + ",";
        json += "\"delayUs\":" + std::to_string(dspEngine->getDelayUs()) + ",";
        json += "\"attenuationDb\":" + std::to_string(dspEngine->getAttenuationDb()) + ",";
        json += "\"centerP\":" + std::to_string(dspEngine->getCenterP()) + ",";
        json += "\"freqLimit\":" + std::to_string(dspEngine->getCrossoverFreq()) + ",";
        json += "\"raceActive\":" + std::string(dspEngine->getRaceEnabled() ? "true" : "false") + ",";
        json += "\"filtersActive\":" + std::string(dspEngine->getFiltersEnabled() ? "true" : "false") + ",";
        json += "\"eqActive\":" + std::string(dspEngine->getEqEnabled() ? "true" : "false") + ",";
        json += "\"eq\":[";
        for(int b=0; b<3; b++) {
            json += "{\"f\":" + std::to_string(dspEngine->getEqFreq(b)) + 
                    ",\"q\":" + std::to_string(dspEngine->getEqQ(b)) + 
                    ",\"g\":" + std::to_string(dspEngine->getEqGain(b)) + "}";
            if(b < 2) json += ",";
        }
        json += "]}";
        res.set_content(json, "application/json");
    });

    svr.Get("/api/spectrum", [dspEngine](const httplib::Request&, httplib::Response& res) {
        std::vector<double> bands = dspEngine->getSpectrumBands();
        std::string json = "[";
        for (size_t i = 0; i < bands.size(); ++i) {
            json += std::to_string(bands[i]);
            if (i < bands.size() - 1) json += ",";
        }
        json += "]";
        res.set_content(json, "application/json");
    });

    svr.Get("/api/update", [dspEngine](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("vol")) dspEngine->setVolume(std::stod(req.get_param_value("vol")) / 100.0);
        if (req.has_param("race")) dspEngine->setRaceEnabled(std::stoi(req.get_param_value("race")) != 0);
        if (req.has_param("filters")) dspEngine->setFiltersEnabled(std::stoi(req.get_param_value("filters")) != 0);
        if (req.has_param("eq")) dspEngine->setEqEnabled(std::stoi(req.get_param_value("eq")) != 0);

        if (req.has_param("delay") || req.has_param("att") || req.has_param("center")) {
            double delayUs = dspEngine->getDelayUs();
            double attDb = dspEngine->getAttenuationDb();
            double center = dspEngine->getCenterP();
            
            if (req.has_param("delay")) delayUs = std::stod(req.get_param_value("delay"));
            if (req.has_param("att")) attDb = std::stod(req.get_param_value("att"));
            if (req.has_param("center")) center = std::stod(req.get_param_value("center")) / 100.0;
            
            dspEngine->setParameters(delayUs, attDb, center, dspEngine->getFreqLimit());
        }

        if (req.has_param("freq")) {
            dspEngine->setCrossoverFreq(std::stod(req.get_param_value("freq")));
        }

        for(int b=0; b<3; b++) {
            std::string pf = "eq" + std::to_string(b) + "f";
            std::string pq = "eq" + std::to_string(b) + "q";
            std::string pg = "eq" + std::to_string(b) + "g";
            if (req.has_param(pf) || req.has_param(pq) || req.has_param(pg)) {
                double f = req.has_param(pf) ? std::stod(req.get_param_value(pf)) : dspEngine->getEqFreq(b);
                double q = req.has_param(pq) ? std::stod(req.get_param_value(pq)) : dspEngine->getEqQ(b);
                double g = req.has_param(pg) ? std::stod(req.get_param_value(pg)) : dspEngine->getEqGain(b);
                dspEngine->setEqBand(b, f, q, g);
            }
        }
        res.set_content("OK", "text/plain");
    });

    std::cout << "\n[Web] Web-Interface gestartet auf Port 8080" << std::endl;
    svr.listen("0.0.0.0", 8080); 
}

int main() {
    std::setlocale(LC_ALL, "C"); // Zwingt C++ systemweit zum Punkt als Dezimaltrennzeichen
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "=== OAPW_Player_HiFi Engine (ALSA Loopback) ===" << std::endl;

    const std::string capture_device = "hw:Loopback,1,0";
    const std::string playback_device = "default"; 
    const unsigned int sample_rate = 192000;
    const unsigned int channels = 2;
    const snd_pcm_format_t capture_format = SND_PCM_FORMAT_S32_LE;
    const snd_pcm_format_t playback_format = SND_PCM_FORMAT_S32_LE;

    const size_t ring_buffer_size = 131072;
    LockFreeRingBuffer<float> ring_buffer(ring_buffer_size);

    AlsaCapture capture(capture_device, capture_format, sample_rate, channels, ring_buffer);
    AlsaPlayback playback(playback_device, playback_format, sample_rate, channels, ring_buffer);

    RACEDspEngine race_engine(68.0, -2.3, 0.1, true, sample_rate);
    
    // Laden der bestehenden Config direkt nach der Instanziierung
    loadConfig(&race_engine);

    std::vector<double> dsp_work_buffer;

    playback.set_dsp_callback([&race_engine](float* buffer, size_t frames) {
        race_engine.processSamples(buffer, frames);
    });

    std::thread webThread(webServerLoop, &race_engine);
    webThread.detach(); 

    std::cout << "Starte ALSA Capture..." << std::endl;
    if (!capture.start()) {
        std::cerr << "Abbruch: Konnte Capture-Thread nicht starten." << std::endl;
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Starte ALSA Playback..." << std::endl;
    if (!playback.start()) {
        std::cerr << "Abbruch: Konnte Playback-Thread nicht starten." << std::endl;
        capture.stop();
        return 1;
    }

    std::cout << "\nOAPW_Player_HiFi läuft als Hintergrunddienst." << std::endl;
    std::cout << "GUI erreichbar unter: http://<raspberry-pi-ip>:8080" << std::endl;
    std::cout << "Drücke Strg+C zum Beenden...\n" << std::endl;

    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\nStoppe ALSA-Pipeline..." << std::endl;
    playback.stop();
    capture.stop();

    std::cout << "OAPW_Player_HiFi erfolgreich beendet." << std::endl;
    return 0;
}
