#include "wifi_server.h"

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Plotter</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#222;color:#eee;padding:16px}
h1{font-size:1.2rem;margin-bottom:12px}
textarea{width:100%;height:140px;background:#111;color:#0f0;border:1px solid #444;font:12px monospace;padding:8px;resize:vertical}
button{background:#0a6;color:#fff;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;margin:4px 2px}
button:hover{background:#0c8}
input[type=file]{color:#eee;margin:8px 0}
label{display:block;margin:8px 0 4px;color:#aaa;font-size:0.85rem}
#log{background:#111;color:#0f0;border:1px solid #444;font:12px monospace;padding:8px;height:200px;overflow-y:auto;white-space:pre-wrap}
.row{display:flex;gap:8px;flex-wrap:wrap}
</style>
</head><body>
<h1>ESP32 GCode Plotter</h1>
<div class=row>
<button onclick="sendCmd('G28')">Home</button>
<button onclick="sendCmd('M3')">Pen Down</button>
<button onclick="sendCmd('M5')">Pen Up</button>
<button onclick="sendCmd('?')">Report</button>
</div>
<label>Send GCode</label>
<textarea id=gcode>G21
G90
G0 X50 Y50
G1 X100 Y50 F1500
G1 X100 Y100
G1 X50 Y100
G1 X50 Y50
M5</textarea>
<div class=row>
<button onclick="runGcode()">Send</button>
<button onclick="document.getElementById('file').click()">Upload File</button>
</div>
<input type=file id=file accept=".gcode,.nc,.txt" onchange="uploadFile(this)">
<label>Log</label>
<div id=log></div>
<script>
const ws=new(location.protocol=='https:'?WSS:WS)('ws://'+location.host+'/ws');
ws.onmessage=function(e){const log=document.getElementById('log');log.textContent+=e.data+'\n';log.scrollTop=log.scrollHeight};
ws.onopen=function(){log('Connected')};
function log(m){const l=document.getElementById('log');l.textContent+=m+'\n';l.scrollTop=l.scrollHeight}
function sendCmd(c){ws.send(c)}
function runGcode(){sendCmd(document.getElementById('gcode').value)}
function uploadFile(input){const f=input.files[0];if(!f)return;const r=new FileReader;r.onload=function(e){sendCmd('$upload '+f.name+':'+e.target.result)};r.readAsText(f)}
</script></body></html>
)rawliteral";

void PlotterServer::begin(const char* ssid, const char* pass, bool apMode) {
    if (apMode) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(ssid, pass);
        Serial.printf("AP started: %s (IP: %s)\n", ssid, WiFi.softAPIP().toString().c_str());
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);
        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
            delay(500);
        }
        Serial.printf("WiFi: %s (IP: %s)\n", WiFi.status() == WL_CONNECTED ? "OK" : "FAIL",
                      WiFi.localIP().toString().c_str());
    }
    setupPages();
    _server.begin();
}

void PlotterServer::setupPages() {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send_P(200, "text/html", INDEX_HTML);
    });
    _ws.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType t,
                        void* arg, uint8_t* data, size_t len) {
        if (t == WS_EVT_CONNECT) {
            _hasClient = true;
            Serial.printf("WS client #%u\n", c->id());
        } else if (t == WS_EVT_DISCONNECT) {
            _hasClient = false;
        } else if (t == WS_EVT_DATA) {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len) {
                ((char*)data)[len] = '\0';
                if (_cmdCb) _cmdCb((const char*)data);
            }
        }
    });
    _server.addHandler(&_ws);
}

void PlotterServer::broadcast(const char* msg) {
    _ws.textAll(msg);
}

void PlotterServer::handleClient() {
    _ws.cleanupClients();
}
