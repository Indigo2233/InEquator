// InEquator RA Tracker firmware
// ESP8266 + STEP/DIR stepper driver (TMC2209 etc.) for a single-axis
// equatorial mount: sidereal tracking + multi-rate manual jog.
// Based on the EFucoser STEP/DIR firmware architecture.
//
// Board: Wemos D1 mini / NodeMCU (ESP8266 Arduino core)
// Libraries: WebSocketsServer
//  Pinout:
//   D1/GPIO5  -> driver STEP
//   D2/GPIO4  -> driver DIR
//   D5/GPIO14 -> driver ENABLE (active low)
//   D7/GPIO13 -> manual jog CW button (to GND)
//   D3/GPIO0  -> manual jog CCW button (to GND, keep released at boot)

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <EEPROM.h>
#include <ctype.h>
#include <math.h>
#include "StepEngine.h"

#define STEP_PIN 5    // D1
#define DIR_PIN 4     // D2
#define ENABLE_PIN 14 // D5
#define CW_PIN 13     // D7
#define CCW_PIN 0     // D3 (GPIO0)

#define DEVICE_RESPONSE "InEquator RA Tracker ver 2001"
#define FIRMWARE_VERSION 2001
#define EEPROM_SIZE 512
#define SETTINGS_MAGIC 0x1EE0C201UL
#define MEMORY_MAGIC 0x4D454D35UL
#define MEMORY_SLOT_COUNT 5
#define MEMORY_NAME_LENGTH 48
#define ASCOM_TCP_PORT 4030
#define WEBSOCKET_PORT 81
#define MAX_TCP_CLIENTS 4

const char *AP_PASSWORD = "012345678";
IPAddress apIp(192, 168, 4, 1);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

struct TrackerSettings {
  uint32_t magic;
  int32_t position;
  int motorSteps;        // stepper full steps per rev (42 motor = 200)
  int gearRatio;         // worm reduction (96)
  int microsteps;        // driver microstep setting (16)
  float siderealPeriod;  // seconds (86164.09)
  long ppmCorrection;    // clock/rate correction, parts per million
  int jogMultiplier;     // jog rate, x10000 of sidereal (80000 = 8x)
  int jogStepsPerSec;    // explicit jog rate in steps/s (used when rateMode == 1)
  uint8_t rateMode;      // 0 = sidereal multiplier, 1 = explicit steps/s
  int maxJogRate;        // steps/s cap
  int acceleration;      // steps/s^2
  bool tracking;
  bool reversed;
  bool hold;
  char staSsid[32];
  char staPassword[64];
  char staIp[16];
  char staGateway[16];
  char staSubnet[16];
  uint32_t memoryMagic;
  long memoryPositions[MEMORY_SLOT_COUNT];
  bool memoryValid[MEMORY_SLOT_COUNT];
  char memoryNames[MEMORY_SLOT_COUNT][MEMORY_NAME_LENGTH];
};

static_assert(sizeof(TrackerSettings) <= EEPROM_SIZE,
              "TrackerSettings exceeds EEPROM allocation");

TrackerSettings settings;
ESP8266WebServer server(80);
WebSocketsServer webSocket(WEBSOCKET_PORT);
WiFiServer tcpServer(ASCOM_TCP_PORT);
WiFiClient tcpClients[MAX_TCP_CLIENTS];

String tcpBuffers[MAX_TCP_CLIENTS];
String serialBuffer;
String apSsid;

// Jog state, ramped in loop()
float jogVel = 0.0f;         // current ramped jog velocity, steps/s
float jogTargetVel = 0.0f;   // commanded jog velocity, steps/s
int32_t jogStartPos = 0;
int32_t jogTargetSteps = 0;  // signed delta; 0 = continuous jog
bool buttonJogActive = false;
bool manualMoveCW = false;
bool manualMoveCCW = false;
int lastCWState = HIGH;
int lastCCWState = HIGH;
unsigned long lastCWDebounce = 0;
unsigned long lastCCWDebounce = 0;
unsigned long lastRampMicros = 0;
unsigned long lastStatusBroadcast = 0;
bool positionSaved = true;
const unsigned long debounceDelayMs = 20;

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>InEquator 赤道仪</title>
<style>
:root{color-scheme:dark;--bg:#111318;--panel:#1a1f28;--panel2:#202734;--line:#384252;--text:#f2f5f8;--muted:#aeb8c6;--accent:#4db6ac;--warn:#f4b24e;--danger:#ee6b63;--ok:#7bc96f}
body.red{--accent:#cc4444;--ok:#bb3333;--warn:#d4883a;--danger:#881111;--muted:#996666;--text:#eecccc;--panel2:#2a1a1a;--line:#4a3030}
body.red .progress-fill{background:var(--accent)}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif}
main{max-width:720px;margin:0 auto;padding:18px 14px 32px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:16px}
h1{font-size:22px;margin:0;font-weight:680}
.status{font-size:13px;color:var(--muted);padding:6px 9px;border:1px solid var(--line);border-radius:999px}
section{border:1px solid var(--line);background:var(--panel);border-radius:12px;padding:14px;margin-bottom:14px}
.control-panel{border-color:color-mix(in srgb,var(--accent) 50%,var(--line))}
.row{display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap}
.row:last-child{margin-bottom:0}
button{background:var(--panel2);color:var(--text);border:1px solid var(--line);border-radius:9px;padding:11px 14px;font-size:15px;cursor:pointer;touch-action:manipulation;user-select:none;-webkit-user-select:none;min-width:64px}
button:active{transform:translateY(1px)}
button.on{background:var(--accent);border-color:var(--accent);color:#0c1514}
button.danger{background:var(--danger);border-color:var(--danger);color:#fff}
.big{font-size:40px;font-weight:700;text-align:center;margin:6px 0 2px}
.muted{color:var(--muted);text-align:center;font-size:13px;margin-bottom:8px}
.jog-btn{flex:1;font-size:20px;padding:26px 10px}
label{font-size:13px;color:var(--muted);display:block;margin-bottom:4px}
input,select{width:100%;background:var(--panel2);border:1px solid var(--line);border-radius:8px;color:var(--text);padding:9px;font-size:15px;margin-bottom:10px}
input[type=checkbox]{width:auto;margin-right:6px;vertical-align:middle}
.check{display:flex;align-items:center;gap:6px;font-size:14px;color:var(--text);margin-bottom:10px}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}
.memory-row{display:grid;grid-template-columns:1fr auto auto auto;gap:8px;align-items:center;margin-bottom:8px}
.memory-row input{width:100%;margin:0}
.memory-pos{font-size:12px;color:var(--muted);text-align:center}
.msg{font-size:13px;color:var(--ok);min-height:16px}
h2{font-size:15px;margin:0 0 10px;color:var(--muted);font-weight:600}
</style>
</head>
<body>
<main>
<header><h1>InEquator RA 跟踪器</h1><span class="status" id="link">连接中…</span></header>
<section class="control-panel">
 <div class="row">
  <button id="tracking" class="wide">跟踪：关</button>
  <button id="halt">停止</button>
  <button id="nightToggle" style="min-width:56px">🌙</button>
 </div>
 <div class="big" id="angle">--.--°</div>
 <div class="muted" id="steps">步数 --</div>
 <div class="muted" id="rateInfo">--</div>
 <div class="row" id="presets"></div>
 <div class="row">
  <input type="number" id="rateSteps" placeholder="速率 步/s" style="flex:1;margin:0">
  <button id="applyRateSteps">按步/s 设速率</button>
 </div>
 <div class="row">
  <input type="number" id="degMove" placeholder="角度 °" style="flex:1;margin:0">
  <button id="degCW">＋°</button>
  <button id="degCCW">－°</button>
 </div>
 <div class="row">
  <input type="number" id="arcsecMove" placeholder="角秒 ″" style="flex:1;margin:0">
  <button id="arcE">＋″</button>
  <button id="arcW">－″</button>
 </div>
 <div class="row">
  <button id="cw" class="jog-btn">CW 正转</button>
  <button id="ccw" class="jog-btn">CCW 反转</button>
 </div>
 <div class="row"><button id="setZero" style="flex:1">当前位置设为零</button></div>
</section>
<section class="memory-panel">
 <h2>记忆位</h2>
 <div id="memories"></div>
</section>
<section class="settings-panel">
 <h2>设置</h2>
 <div class="grid2">
  <div><label>电机全步/转</label><input type="number" id="motorSteps"></div>
  <div><label>减速比</label><input type="number" id="gearRatio"></div>
  <div><label>细分</label><input type="number" id="microsteps"></div>
  <div><label>恒星周期(秒)</label><input type="number" step="0.01" id="siderealPeriod"></div>
  <div><label>PPM 校正</label><input type="number" id="ppm"></div>
  <div><label>最大微调速率(步/s)</label><input type="number" id="maxJogRate"></div>
  <div><label>加速度(步/s²)</label><input type="number" id="acceleration"></div>
  <div><label>STA WiFi</label><input id="staSsid" placeholder="留空=仅AP"></div>
  <div><label>STA 密码</label><input type="password" id="staPassword"></div>
 </div>
 <label class="check"><input type="checkbox" id="hold"> 停止时保持力矩</label>
 <label class="check"><input type="checkbox" id="reversed"> 反转方向</label>
 <button id="saveSettings" style="width:100%">保存设置</button>
 <div class="msg" id="settingsMessage"></div>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
let state={};
function api(path,body){return fetch(path,{method:body?'POST':'GET',headers:body?{'Content-Type':'application/json'}:{},body:body?JSON.stringify(body):undefined}).then(r=>{if(!r.ok)throw r;return r.json()});}
function fmtAngle(s){if(!state.stepsPerOutputRev)return '--.--°';const mod=((s%state.stepsPerOutputRev)+state.stepsPerOutputRev)%state.stepsPerOutputRev;return (mod/state.stepsPerOutputRev*360).toFixed(2)+'°';}
function setState(s){state=s;$('angle').textContent=fmtAngle(s.positionSteps);$('steps').textContent='步数 '+s.positionSteps+'  ·  每转 '+s.stepsPerOutputRev+' 步';$('rateInfo').textContent=s.rateMode===1?('速率 '+s.jogStepsPerSec+' 步/s'):('速率 '+s.jogRate/10000+'× 恒星速');$('tracking').textContent=s.tracking?'跟踪：开':'跟踪：关';$('tracking').classList.toggle('on',!!s.tracking);renderPresets();}
const PRESETS=[{n:10000,l:'1×'},{n:80000,l:'8×'},{n:320000,l:'32×'},{n:1280000,l:'128×'},{n:2560000,l:'256×'}];
function renderPresets(){$('presets').innerHTML='';PRESETS.forEach(p=>{const b=document.createElement('button');b.textContent=p.l;b.classList.toggle('on',state.jogRate===p.n);b.onclick=()=>api('/api/settings',{jogMultiplier:p.n}).then(setState);$('presets').append(b);});}
function jog(action){api('/api/jog',{action}).then(setState);}
function holdJog(btn,action){const start=()=>jog(action);const stop=()=>jog('halt');btn.addEventListener('pointerdown',e=>{e.preventDefault();btn.setPointerCapture(e.pointerId);start();});btn.addEventListener('pointerup',stop);btn.addEventListener('pointercancel',stop);btn.addEventListener('lostpointercapture',stop);}
holdJog($('cw'),'cw');holdJog($('ccw'),'ccw');
$('tracking').onclick=()=>api('/api/tracking',{tracking:!state.tracking}).then(setState);
$('halt').onclick=()=>jog('halt');
$('setZero').onclick=()=>api('/api/set-position',{steps:0}).then(setState);
$('applyRateSteps').onclick=()=>api('/api/settings',{rateMode:1,jogStepsPerSec:+$('rateSteps').value}).then(setState);
$('degCW').onclick=()=>api('/api/jog',{action:'move_deg',degrees:Math.abs(+$('degMove').value||0)}).then(setState);
$('degCCW').onclick=()=>api('/api/jog',{action:'move_deg',degrees:-Math.abs(+$('degMove').value||0)}).then(setState);
$('arcE').onclick=()=>api('/api/jog',{action:'move_arcsec',arcsec:Math.abs(+$('arcsecMove').value||0)}).then(setState);
$('arcW').onclick=()=>api('/api/jog',{action:'move_arcsec',arcsec:-Math.abs(+$('arcsecMove').value||0)}).then(setState);
$('saveSettings').onclick=()=>{const body={motorSteps:+$('motorSteps').value,gearRatio:+$('gearRatio').value,microsteps:+$('microsteps').value,siderealPeriod:+$('siderealPeriod').value,ppm:+$('ppm').value,maxJogRate:+$('maxJogRate').value,acceleration:+$('acceleration').value,hold:$('hold').checked,reversed:$('reversed').checked};if($('staSsid').value.trim())body.staSsid=$('staSsid').value.trim();if($('staPassword').value)body.staPassword=$('staPassword').value;api('/api/settings',body).then(()=>{$('settingsMessage').textContent='设置已保存';}).catch(()=>{$('settingsMessage').textContent='保存失败，请检查输入值';});};
function renderMemories(m){$('memories').innerHTML='';m.memories.forEach(mm=>{const row=document.createElement('div');row.className='memory-row';const name=document.createElement('input');name.value=mm.name||('目标 '+(mm.slot+1));const pos=document.createElement('span');pos.className='memory-pos';pos.textContent=mm.valid?fmtAngle(mm.position):'未保存';const save=document.createElement('button');save.textContent='保存';save.onclick=()=>api('/api/memories',{slot:mm.slot,action:'save',name:name.value.trim()}).then(renderMemories);const move=document.createElement('button');move.textContent='移动';move.disabled=!mm.valid;move.onclick=()=>api('/api/memories',{slot:mm.slot,action:'move'}).then(()=>refresh());row.append(name,pos,save,move);$('memories').append(row);});}
async function refresh(){try{const s=await api('/api/status');setState(s);fillSettings(s);}catch(e){$('link').textContent='已断开';}}
async function refreshMemories(){try{renderMemories(await api('/api/memories'));}catch(_){}}
function fillSettings(s){$('motorSteps').value=s.motorSteps;$('gearRatio').value=s.gearRatio;$('microsteps').value=s.microsteps;$('siderealPeriod').value=s.siderealPeriod;$('ppm').value=s.ppm;$('maxJogRate').value=s.maxJogRate;$('acceleration').value=s.acceleration;$('hold').checked=!!s.hold;$('reversed').checked=!!s.reversed;$('staSsid').value=s.staSsid||'';}
function connectWs(){const ws=new WebSocket('ws://'+location.hostname+':81/');ws.onopen=()=>{$('link').textContent='已连接'};ws.onmessage=e=>{try{setState(JSON.parse(e.data));}catch(_){}};ws.onclose=()=>{setTimeout(connectWs,1200);};}
(function(){const night=localStorage.getItem('inequator_night')==='1';if(night)document.body.classList.add('red');$('nightToggle').textContent=night?'☀️':'🌙';$('nightToggle').onclick=()=>{const on=document.body.classList.toggle('red');$('nightToggle').textContent=on?'☀️':'🌙';localStorage.setItem('inequator_night',on?'1':'0');};})();
connectWs();refresh();refreshMemories();setInterval(refresh,5000);
</script>
</body>
</html>
)rawliteral";

// ==================== Settings Management ====================

void saveSettings() {
  settings.magic = SETTINGS_MAGIC;
  settings.position = AxisEngine.getPosition();
  EEPROM.put(0, settings);
  EEPROM.commit();
}

void initializeMemorySlots() {
  settings.memoryMagic = MEMORY_MAGIC;
  for (int i = 0; i < MEMORY_SLOT_COUNT; i++) {
    settings.memoryPositions[i] = 0;
    settings.memoryValid[i] = false;
    String defaultName = String("目标 ") + (i + 1);
    defaultName.toCharArray(settings.memoryNames[i], MEMORY_NAME_LENGTH);
  }
}

void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, settings);
  bool settingsChanged = false;
  if (settings.magic != SETTINGS_MAGIC) {
    memset(&settings, 0, sizeof(settings));
    settings.magic = SETTINGS_MAGIC;
    settings.position = 0;
    settings.motorSteps = 200;
    settings.gearRatio = 96;
    settings.microsteps = 16;
    settings.siderealPeriod = 86164.09F;
    settings.ppmCorrection = 0;
    settings.jogMultiplier = 80000;
    settings.jogStepsPerSec = 100;
    settings.rateMode = 0;
    settings.maxJogRate = 4000;
    settings.acceleration = 2000;
    settings.tracking = false;
    settings.reversed = false;
    settings.hold = true;
    initializeMemorySlots();
    settingsChanged = true;
  }
  if (settings.motorSteps <= 0 || settings.gearRatio <= 0 || settings.microsteps <= 0) {
    settings.motorSteps = 200;
    settings.gearRatio = 96;
    settings.microsteps = 16;
    settingsChanged = true;
  }
  if (settings.siderealPeriod < 100.0F || settings.siderealPeriod > 1000000.0F) {
    settings.siderealPeriod = 86164.09F;
    settingsChanged = true;
  }
  if (settings.ppmCorrection < -10000 || settings.ppmCorrection > 10000) {
    settings.ppmCorrection = 0;
    settingsChanged = true;
  }
  if (settings.jogMultiplier < 100 || settings.jogMultiplier > 1000000) {
    settings.jogMultiplier = 80000;
    settingsChanged = true;
  }
  if (settings.jogStepsPerSec < 1 || settings.jogStepsPerSec > 10000) {
    settings.jogStepsPerSec = 100;
    settingsChanged = true;
  }
  if (settings.rateMode > 1) {
    settings.rateMode = 0;
    settingsChanged = true;
  }
  if (settings.maxJogRate < 100 || settings.maxJogRate > 10000) {
    settings.maxJogRate = 4000;
    settingsChanged = true;
  }
  if (settings.acceleration < 100 || settings.acceleration > 100000) {
    settings.acceleration = 2000;
    settingsChanged = true;
  }
  if (settings.memoryMagic != MEMORY_MAGIC) {
    initializeMemorySlots();
    settingsChanged = true;
  } else {
    for (int i = 0; i < MEMORY_SLOT_COUNT; i++) {
      settings.memoryNames[i][MEMORY_NAME_LENGTH - 1] = '\0';
      if (settings.memoryNames[i][0] == '\0') {
        String defaultName = String("目标 ") + (i + 1);
        defaultName.toCharArray(settings.memoryNames[i], MEMORY_NAME_LENGTH);
        settingsChanged = true;
      }
    }
  }
  if (settingsChanged) {
    EEPROM.put(0, settings);
    EEPROM.commit();
  }
}

// ==================== Motion Helpers ====================

long stepsPerOutputRev() {
  return (long)settings.motorSteps * settings.gearRatio * settings.microsteps;
}

// Motor steps/s required for sidereal output rate, PPM corrected.
float siderealRate() {
  return (float)stepsPerOutputRev()
         * (1.0F + settings.ppmCorrection / 1000000.0F)
         / settings.siderealPeriod;
}

float jogRateSteps() {
  float r = (settings.rateMode == 1)
      ? (float)settings.jogStepsPerSec
      : siderealRate() * settings.jogMultiplier / 10000.0F;
  if (r > settings.maxJogRate) {
    r = settings.maxJogRate;
  }
  return r;
}

// Output-axis angle conversions (1 rev = stepsPerOutputRev steps).
int32_t degreesToSteps(float degrees) {
  return (int32_t)lroundf(degrees * (float)stepsPerOutputRev() / 360.0F);
}

int32_t arcsecToSteps(float arcsec) {
  return (int32_t)lroundf(arcsec * (float)stepsPerOutputRev() / 1296000.0F);
}

bool isMoving() {
  return fabsf(jogVel) > 0.5F || jogTargetVel != 0.0F || jogTargetSteps != 0;
}

void updateEngineRate() {
  float base = settings.tracking ? siderealRate() : 0.0F;
  float net = base + jogVel;
  if (settings.reversed) {
    net = -net;
  }
  AxisEngine.setRate(net);
}

void startContinuousJog(int dir) {
  jogTargetSteps = 0;
  jogStartPos = AxisEngine.getPosition();
  jogTargetVel = dir * jogRateSteps();
  positionSaved = false;
}

void startStepJog(int32_t delta) {
  if (delta == 0) {
    return;
  }
  jogTargetSteps = delta;
  jogStartPos = AxisEngine.getPosition();
  jogTargetVel = (delta > 0 ? 1.0F : -1.0F) * jogRateSteps();
  positionSaved = false;
}

void stopJog() {
  jogTargetSteps = 0;
  jogTargetVel = 0.0F;
}

void updateEnablePin() {
  digitalWrite(ENABLE_PIN, (isMoving() || settings.hold) ? LOW : HIGH);
}

void setCurrentPosition(int32_t value) {
  AxisEngine.setPosition(value);
  positionSaved = true;
  saveSettings();
}

// ==================== Status / JSON ====================

String boolText(bool value) {
  return value ? "true" : "false";
}

String statusResponse() {
  String response = "P ";
  response += AxisEngine.getPosition();
  response += ";T ";
  response += boolText(settings.tracking);
  response += ";Q ";
  response += settings.jogMultiplier;
  response += ";Y ";
  response += settings.jogStepsPerSec;
  response += ";M ";
  response += boolText(isMoving());
  response += "#";
  return response;
}

String ipToString(const IPAddress &ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (c == '"' || c == '\\') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\n') {
      escaped += "\\n";
    } else if (c == '\r') {
      escaped += "\\r";
    } else {
      escaped += c;
    }
  }
  return escaped;
}

String memoriesJson() {
  String json = "{\"memories\":[";
  for (int i = 0; i < MEMORY_SLOT_COUNT; i++) {
    if (i > 0) json += ",";
    json += "{\"slot\":";
    json += i;
    json += ",\"name\":\"";
    json += jsonEscape(settings.memoryNames[i]);
    json += "\",\"position\":";
    json += settings.memoryPositions[i];
    json += ",\"valid\":";
    json += boolText(settings.memoryValid[i]);
    json += "}";
  }
  json += "]}";
  return json;
}

String statusJson() {
  String json = "{";
  json += "\"firmware\":";
  json += FIRMWARE_VERSION;
  json += ",\"positionSteps\":";
  json += AxisEngine.getPosition();
  json += ",\"tracking\":";
  json += boolText(settings.tracking);
  json += ",\"jogRate\":";
  json += settings.jogMultiplier;
  json += ",\"jogStepsPerSec\":";
  json += settings.jogStepsPerSec;
  json += ",\"rateMode\":";
  json += settings.rateMode;
  json += ",\"isMoving\":";
  json += boolText(isMoving());
  json += ",\"hold\":";
  json += boolText(settings.hold);
  json += ",\"reversed\":";
  json += boolText(settings.reversed);
  json += ",\"stepsPerOutputRev\":";
  json += stepsPerOutputRev();
  json += ",\"motorSteps\":";
  json += settings.motorSteps;
  json += ",\"gearRatio\":";
  json += settings.gearRatio;
  json += ",\"microsteps\":";
  json += settings.microsteps;
  json += ",\"siderealPeriod\":";
  json += String(settings.siderealPeriod, 2);
  json += ",\"ppm\":";
  json += settings.ppmCorrection;
  json += ",\"maxJogRate\":";
  json += settings.maxJogRate;
  json += ",\"acceleration\":";
  json += settings.acceleration;
  json += ",\"trackingRate\":";
  json += String(siderealRate(), 4);
  json += ",\"apSsid\":\"";
  json += apSsid;
  json += "\",\"apIp\":\"";
  json += ipToString(WiFi.softAPIP());
  json += "\",\"wifiIp\":\"";
  json += ipToString(WiFi.localIP());
  json += "\",\"staSsid\":\"";
  json += settings.staSsid;
  json += "\",\"staIp\":\"";
  json += settings.staIp;
  json += "\",\"staGateway\":\"";
  json += settings.staGateway;
  json += "\",\"staSubnet\":\"";
  json += settings.staSubnet;
  json += "\",\"tcpPort\":";
  json += ASCOM_TCP_PORT;
  json += "}";
  return json;
}

void broadcastStatus() {
  String json = statusJson();
  webSocket.broadcastTXT(json);
}

// ==================== Command Processing ====================

long commandParameter(String command) {
  if (command.length() <= 1) {
    return 0;
  }
  String param = command.substring(1);
  param.trim();
  return param.toInt();
}

long commandParameterAt(String command, int offset) {
  if (command.length() <= offset) {
    return 0;
  }
  String param = command.substring(offset);
  param.trim();
  return param.toInt();
}

String processCommand(String command) {
  command.trim();
  if (command.endsWith("#")) {
    command.remove(command.length() - 1);
  }
  command.trim();
  if (command.length() == 0) {
    return String(DEVICE_RESPONSE) + "#";
  }

  // Two-char commands: degree / arcsecond moves (signed = direction).
  if (command.startsWith("MD")) {
    long deg1000 = commandParameterAt(command, 2);
    if (deg1000 < -1000000L || deg1000 > 1000000L) {
      return "ERR:degrees#";
    }
    startStepJog(degreesToSteps(deg1000 / 1000.0F));
    broadcastStatus();
    return statusResponse();
  }
  if (command.startsWith("MA")) {
    long arcsec = commandParameterAt(command, 2);
    if (arcsec < -1296000L || arcsec > 1296000L) {
      return "ERR:arcsec#";
    }
    startStepJog(arcsecToSteps((float)arcsec));
    broadcastStatus();
    return statusResponse();
  }

  char code = command.charAt(0);
  long value = commandParameter(command);
  switch (code) {
    case '#':
      return String(DEVICE_RESPONSE) + "#";
    case 'G':
      return statusResponse();
    case 'V':
      return String("V ") + FIRMWARE_VERSION + "#";
    case 'I':
      return statusJson() + "#";
    case 'B':
      settings.tracking = value != 0;
      updateEngineRate();
      saveSettings();
      broadcastStatus();
      return String("B ") + boolText(settings.tracking) + "#";
    case 'Q':
      {
        if (value < 100 || value > 1000000) {
          return "ERR:jog_rate#";
        }
        settings.jogMultiplier = (int)value;
        settings.rateMode = 0;
        saveSettings();
        broadcastStatus();
        return String("Q ") + settings.jogMultiplier + "#";
      }
    case 'Y':
      {
        if (value < 1 || value > 10000) {
          return "ERR:jog_speed#";
        }
        settings.jogStepsPerSec = (int)value;
        settings.rateMode = 1;
        saveSettings();
        broadcastStatus();
        return String("Y ") + settings.jogStepsPerSec + "#";
      }
    case 'M':
      {
        if (command.startsWith("M+")) {
          startContinuousJog(1);
        } else if (command.startsWith("M-")) {
          startContinuousJog(-1);
        } else if (value != 0) {
          startStepJog((int32_t)value);
        } else {
          return "ERR:steps#";
        }
        broadcastStatus();
        return statusResponse();
      }
    case 'S':
      stopJog();
      broadcastStatus();
      return "S#";
    case 'P':
      setCurrentPosition((int32_t)value);
      broadcastStatus();
      return statusResponse();
    case 'R':
      settings.reversed = value != 0;
      updateEngineRate();
      saveSettings();
      broadcastStatus();
      return String("reversed = ") + boolText(settings.reversed) + "#";
    case 'C':
      settings.hold = value != 0;
      saveSettings();
      updateEnablePin();
      broadcastStatus();
      return String("hold = ") + boolText(settings.hold) + "#";
    case 'D':
      {
        if (value < -10000 || value > 10000) {
          return "ERR:ppm#";
        }
        settings.ppmCorrection = value;
        updateEngineRate();
        saveSettings();
        broadcastStatus();
        return String("D ") + settings.ppmCorrection + "#";
      }
    case 'X':
      {
        if (value < 100 || value > 10000) {
          return "ERR:jog_speed#";
        }
        settings.maxJogRate = (int)value;
        saveSettings();
        broadcastStatus();
        return String("X ") + settings.maxJogRate + "#";
      }
    case 'A':
      {
        if (value < 100 || value > 100000) {
          return "ERR:acceleration#";
        }
        settings.acceleration = (int)value;
        saveSettings();
        broadcastStatus();
        return String("A ") + settings.acceleration + "#";
      }
    default:
      return String("ERR:") + code + "#";
  }
}

// ==================== HTTP API ====================

void sendJson(int code, const String &json) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", json);
}

bool extractNumber(const String &body, const String &key, double &value) {
  int keyIndex = body.indexOf("\"" + key + "\"");
  if (keyIndex < 0) {
    return false;
  }
  int colon = body.indexOf(':', keyIndex);
  if (colon < 0) {
    return false;
  }
  int start = colon + 1;
  while (start < (int)body.length() && isspace(body.charAt(start))) {
    start++;
  }
  int end = start;
  while (end < (int)body.length()) {
    char c = body.charAt(end);
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
      end++;
    } else {
      break;
    }
  }
  if (end == start) {
    return false;
  }
  value = body.substring(start, end).toFloat();
  return true;
}

bool extractBool(const String &body, const String &key, bool &value) {
  int keyIndex = body.indexOf("\"" + key + "\"");
  if (keyIndex < 0) {
    return false;
  }
  int colon = body.indexOf(':', keyIndex);
  if (colon < 0) {
    return false;
  }
  String tail = body.substring(colon + 1);
  tail.trim();
  if (tail.startsWith("true") || tail.startsWith("1")) {
    value = true;
    return true;
  }
  if (tail.startsWith("false") || tail.startsWith("0")) {
    value = false;
    return true;
  }
  return false;
}

bool extractString(const String &body, const String &key, char *dest, size_t destSize) {
  int keyIndex = body.indexOf("\"" + key + "\"");
  if (keyIndex < 0) {
    return false;
  }
  int colon = body.indexOf(':', keyIndex);
  int firstQuote = body.indexOf('"', colon + 1);
  int secondQuote = body.indexOf('"', firstQuote + 1);
  if (colon < 0 || firstQuote < 0 || secondQuote < 0 || destSize == 0) {
    return false;
  }
  String value = body.substring(firstQuote + 1, secondQuote);
  value.toCharArray(dest, destSize);
  dest[destSize - 1] = '\0';
  return true;
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  sendJson(200, statusJson());
}

void handleTrackingApi() {
  String body = server.arg("plain");
  bool value;
  if (!extractBool(body, "tracking", value)) {
    sendJson(400, "{\"error\":\"invalid_tracking\"}");
    return;
  }
  settings.tracking = value;
  updateEngineRate();
  saveSettings();
  broadcastStatus();
  sendJson(200, statusJson());
}

void handleJogApi() {
  String body = server.arg("plain");
  char action[12] = "";
  if (!extractString(body, "action", action, sizeof(action))) {
    sendJson(400, "{\"error\":\"invalid_action\"}");
    return;
  }
  if (strcmp(action, "cw") == 0) {
    startContinuousJog(1);
  } else if (strcmp(action, "ccw") == 0) {
    startContinuousJog(-1);
  } else if (strcmp(action, "halt") == 0) {
    stopJog();
  } else if (strcmp(action, "move") == 0) {
    double stepsValue;
    if (!extractNumber(body, "steps", stepsValue)) {
      sendJson(400, "{\"error\":\"invalid_steps\"}");
      return;
    }
    startStepJog((int32_t)lround(stepsValue));
  } else if (strcmp(action, "move_deg") == 0) {
    double degrees;
    if (!extractNumber(body, "degrees", degrees) || degrees < -1000.0 || degrees > 1000.0) {
      sendJson(400, "{\"error\":\"invalid_degrees\"}");
      return;
    }
    startStepJog(degreesToSteps((float)degrees));
  } else if (strcmp(action, "move_arcsec") == 0) {
    double arcsec;
    if (!extractNumber(body, "arcsec", arcsec) || arcsec < -1296000.0 || arcsec > 1296000.0) {
      sendJson(400, "{\"error\":\"invalid_arcsec\"}");
      return;
    }
    startStepJog(arcsecToSteps((float)arcsec));
  } else {
    sendJson(400, "{\"error\":\"invalid_action\"}");
    return;
  }
  broadcastStatus();
  sendJson(200, statusJson());
}

void handleSetPositionApi() {
  String body = server.arg("plain");
  double value;
  if (!extractNumber(body, "steps", value)) {
    sendJson(400, "{\"error\":\"invalid_position\"}");
    return;
  }
  setCurrentPosition((int32_t)lround(value));
  broadcastStatus();
  sendJson(200, statusJson());
}

void handleSettingsPostApi() {
  String body = server.arg("plain");
  double numberValue;
  bool boolValue;

  if (extractNumber(body, "motorSteps", numberValue)) {
    if (numberValue < 1 || numberValue > 10000) {
      sendJson(400, "{\"error\":\"invalid_motor_steps\"}");
      return;
    }
    settings.motorSteps = (int)numberValue;
  }
  if (extractNumber(body, "gearRatio", numberValue)) {
    if (numberValue < 1 || numberValue > 10000) {
      sendJson(400, "{\"error\":\"invalid_gear_ratio\"}");
      return;
    }
    settings.gearRatio = (int)numberValue;
  }
  if (extractNumber(body, "microsteps", numberValue)) {
    if (numberValue < 1 || numberValue > 256) {
      sendJson(400, "{\"error\":\"invalid_microsteps\"}");
      return;
    }
    settings.microsteps = (int)numberValue;
  }
  if (extractNumber(body, "siderealPeriod", numberValue)) {
    if (numberValue < 100 || numberValue > 1000000) {
      sendJson(400, "{\"error\":\"invalid_sidereal_period\"}");
      return;
    }
    settings.siderealPeriod = (float)numberValue;
  }
  if (extractNumber(body, "ppm", numberValue)) {
    if (numberValue < -10000 || numberValue > 10000) {
      sendJson(400, "{\"error\":\"invalid_ppm\"}");
      return;
    }
    settings.ppmCorrection = (long)numberValue;
  }
  if (extractNumber(body, "jogMultiplier", numberValue)) {
    if (numberValue < 100 || numberValue > 1000000) {
      sendJson(400, "{\"error\":\"invalid_jog_rate\"}");
      return;
    }
    settings.jogMultiplier = (int)numberValue;
  }
  if (extractNumber(body, "jogStepsPerSec", numberValue)) {
    if (numberValue < 1 || numberValue > 10000) {
      sendJson(400, "{\"error\":\"invalid_jog_steps_per_sec\"}");
      return;
    }
    settings.jogStepsPerSec = (int)numberValue;
  }
  if (extractNumber(body, "rateMode", numberValue)) {
    if (numberValue < 0 || numberValue > 1) {
      sendJson(400, "{\"error\":\"invalid_rate_mode\"}");
      return;
    }
    settings.rateMode = (uint8_t)numberValue;
  }
  if (extractNumber(body, "maxJogRate", numberValue)) {
    if (numberValue < 100 || numberValue > 10000) {
      sendJson(400, "{\"error\":\"invalid_max_jog_rate\"}");
      return;
    }
    settings.maxJogRate = (int)numberValue;
  }
  if (extractNumber(body, "acceleration", numberValue)) {
    if (numberValue < 100 || numberValue > 100000) {
      sendJson(400, "{\"error\":\"invalid_acceleration\"}");
      return;
    }
    settings.acceleration = (int)numberValue;
  }
  if (extractBool(body, "hold", boolValue)) {
    settings.hold = boolValue;
  }
  if (extractBool(body, "reversed", boolValue)) {
    settings.reversed = boolValue;
  }
  bool staChanged = false;
  staChanged |= extractString(body, "staSsid", settings.staSsid, sizeof(settings.staSsid));
  staChanged |= extractString(body, "staPassword", settings.staPassword, sizeof(settings.staPassword));
  staChanged |= extractString(body, "staIp", settings.staIp, sizeof(settings.staIp));
  staChanged |= extractString(body, "staGateway", settings.staGateway, sizeof(settings.staGateway));
  staChanged |= extractString(body, "staSubnet", settings.staSubnet, sizeof(settings.staSubnet));

  updateEngineRate();
  updateEnablePin();
  saveSettings();

  if (staChanged && strlen(settings.staSsid) > 0) {
    IPAddress ip, gw, sn;
    if (strlen(settings.staIp) > 0
        && ip.fromString(settings.staIp)
        && gw.fromString(settings.staGateway)
        && sn.fromString(settings.staSubnet)) {
      WiFi.config(ip, gw, sn);
    }
    WiFi.begin(settings.staSsid, settings.staPassword);
  }

  broadcastStatus();
  sendJson(200, statusJson());
}

void handleMemoriesGetApi() {
  sendJson(200, memoriesJson());
}

void handleMemoriesPostApi() {
  String body = server.arg("plain");
  double slotValue;
  if (!extractNumber(body, "slot", slotValue)) {
    sendJson(400, "{\"error\":\"invalid_slot\"}");
    return;
  }
  int slot = (int)slotValue;
  if (slot < 0 || slot >= MEMORY_SLOT_COUNT || slotValue != slot) {
    sendJson(400, "{\"error\":\"invalid_slot\"}");
    return;
  }
  char action[12] = "";
  if (!extractString(body, "action", action, sizeof(action))) {
    sendJson(400, "{\"error\":\"invalid_action\"}");
    return;
  }

  if (strcmp(action, "save") == 0) {
    char name[MEMORY_NAME_LENGTH] = "";
    if (extractString(body, "name", name, sizeof(name)) && name[0] != '\0') {
      strncpy(settings.memoryNames[slot], name, MEMORY_NAME_LENGTH);
      settings.memoryNames[slot][MEMORY_NAME_LENGTH - 1] = '\0';
    }
    settings.memoryPositions[slot] = AxisEngine.getPosition();
    settings.memoryValid[slot] = true;
    saveSettings();
    sendJson(200, memoriesJson());
    return;
  }

  if (strcmp(action, "move") == 0) {
    if (!settings.memoryValid[slot]) {
      sendJson(409, "{\"error\":\"memory_not_saved\"}");
      return;
    }
    startStepJog(settings.memoryPositions[slot] - AxisEngine.getPosition());
    broadcastStatus();
    sendJson(200, memoriesJson());
    return;
  }

  sendJson(400, "{\"error\":\"invalid_action\"}");
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204, "text/plain", "");
}

void setupHttp() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/tracking", HTTP_POST, handleTrackingApi);
  server.on("/api/jog", HTTP_POST, handleJogApi);
  server.on("/api/set-position", HTTP_POST, handleSetPositionApi);
  server.on("/api/settings", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_POST, handleSettingsPostApi);
  server.on("/api/memories", HTTP_GET, handleMemoriesGetApi);
  server.on("/api/memories", HTTP_POST, handleMemoriesPostApi);
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
      return;
    }
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
}

void setupWifi() {
  apSsid = "InEquator-" + String(ESP.getChipId(), HEX);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIp, apGateway, apSubnet);
  WiFi.softAP(apSsid.c_str(), AP_PASSWORD);
  if (strlen(settings.staSsid) > 0) {
    IPAddress staIpAddr, staGw, staSn;
    if (strlen(settings.staIp) > 0
        && staIpAddr.fromString(settings.staIp)
        && staGw.fromString(settings.staGateway)
        && staSn.fromString(settings.staSubnet)) {
      WiFi.config(staIpAddr, staGw, staSn);
    }
    WiFi.begin(settings.staSsid, settings.staPassword);
  }
  tcpServer.begin();
  tcpServer.setNoDelay(true);
}

// ==================== Transport ====================

void handleTcpClients() {
  if (tcpServer.hasClient()) {
    WiFiClient nextClient = tcpServer.available();
    bool assigned = false;
    for (byte i = 0; i < MAX_TCP_CLIENTS; i++) {
      if (!tcpClients[i] || !tcpClients[i].connected()) {
        if (tcpClients[i]) {
          tcpClients[i].stop();
        }
        tcpClients[i] = nextClient;
        tcpClients[i].setNoDelay(true);
        tcpBuffers[i] = "";
        assigned = true;
        break;
      }
    }
    if (!assigned) {
      nextClient.stop();
    }
  }

  for (byte i = 0; i < MAX_TCP_CLIENTS; i++) {
    if (!tcpClients[i] || !tcpClients[i].connected()) {
      continue;
    }
    while (tcpClients[i].available()) {
      char c = (char)tcpClients[i].read();
      if (c == '#') {
        String response = processCommand(tcpBuffers[i]);
        tcpClients[i].print(response);
        tcpBuffers[i] = "";
      } else if (c != '\r' && c != '\n') {
        tcpBuffers[i] += c;
      }
    }
  }
}

void handleSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '#') {
      Serial.print(processCommand(serialBuffer));
      serialBuffer = "";
    } else if (c != '\r' && c != '\n') {
      serialBuffer += c;
    }
  }
}

// ==================== Manual Buttons ====================

void readButton(int pin, int &lastState, bool &pressed, unsigned long &lastDebounce) {
  int reading = digitalRead(pin);
  if (reading != lastState) {
    lastDebounce = millis();
    lastState = reading;
  }
  if ((millis() - lastDebounce) > debounceDelayMs) {
    pressed = (reading == LOW);
  }
}

void handleManualButtons() {
  readButton(CW_PIN, lastCWState, manualMoveCW, lastCWDebounce);
  readButton(CCW_PIN, lastCCWState, manualMoveCCW, lastCCWDebounce);
  if (manualMoveCW) {
    startContinuousJog(1);
    buttonJogActive = true;
  } else if (manualMoveCCW) {
    startContinuousJog(-1);
    buttonJogActive = true;
  } else if (buttonJogActive) {
    buttonJogActive = false;
    stopJog();
  }
}

// ==================== Jog Ramp ====================

void serviceJog() {
  unsigned long now = micros();
  float dt = (float)(now - lastRampMicros) / 1000000.0F;
  lastRampMicros = now;
  if (dt <= 0.0F || dt > 0.1F) {
    dt = 0.002F;
  }

  // Step-target jog completion
  if (jogTargetSteps != 0) {
    int32_t moved = AxisEngine.getPosition() - jogStartPos;
    if ((jogTargetSteps > 0 && moved >= jogTargetSteps)
        || (jogTargetSteps < 0 && moved <= jogTargetSteps)) {
      jogTargetSteps = 0;
      jogTargetVel = 0.0F;
    }
  }

  // Ramp velocity toward target
  float step = settings.acceleration * dt;
  if (step < 0.01F) {
    step = 0.01F;
  }
  if (jogVel < jogTargetVel) {
    jogVel += step;
    if (jogVel > jogTargetVel) {
      jogVel = jogTargetVel;
    }
  } else if (jogVel > jogTargetVel) {
    jogVel -= step;
    if (jogVel < jogTargetVel) {
      jogVel = jogTargetVel;
    }
  }

  // Settled: save position once
  if (jogTargetSteps == 0 && jogTargetVel == 0.0F
      && fabsf(jogVel) < 0.5F && !positionSaved) {
    jogVel = 0.0F;
    positionSaved = true;
    saveSettings();
    broadcastStatus();
  }

  updateEngineRate();
}

// ==================== Setup & Loop ====================

void setup() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(CW_PIN, INPUT_PULLUP);
  pinMode(CCW_PIN, INPUT_PULLUP);

  lastCWState = digitalRead(CW_PIN);
  lastCCWState = digitalRead(CCW_PIN);

  Serial.begin(9600);
  Serial.setTimeout(2000);

  loadSettings();
  AxisEngine.begin(STEP_PIN, DIR_PIN);
  AxisEngine.setPosition(settings.position);
  updateEnablePin();
  updateEngineRate();

  setupWifi();
  setupHttp();
  webSocket.begin();
  webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_CONNECTED) {
      String json = statusJson();
      webSocket.sendTXT(num, json);
    }
  });

  lastRampMicros = micros();

  Serial.println();
  Serial.println(DEVICE_RESPONSE);
  Serial.print("AP SSID: ");
  Serial.println(apSsid);
  Serial.println("AP URL: http://192.168.4.1");
}

void loop() {
  server.handleClient();
  webSocket.loop();
  handleTcpClients();
  handleSerial();
  handleManualButtons();
  serviceJog();
  updateEnablePin();

  if (millis() - lastStatusBroadcast > 500) {
    lastStatusBroadcast = millis();
    broadcastStatus();
  }
}
