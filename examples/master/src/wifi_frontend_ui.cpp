#include "wifi_frontend_ui.hpp"

#include <cstdlib>
#include <cstring>
#include <functional>

#include <WiFi.h>

#include "espnow_link/profile.hpp"

namespace {

const char* roleToString(uint8_t role_code) {
  if (role_code == static_cast<uint8_t>(espnow_link::kProfilePms & 0xFFU)) return "PMS";
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileRelay & 0xFFU)) return "RELAY";
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileSens & 0xFFU)) return "SENS";
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileSemu & 0xFFU)) return "SEMU";
  if (role_code == static_cast<uint8_t>(espnow_link::kProfileRemu & 0xFFU)) return "REMU";
  return "UNKNOWN";
}

const char* wifiStatusToString(wl_status_t st) {
  switch (st) {
    case WL_CONNECTED:
      return "connected";
    case WL_DISCONNECTED:
      return "disconnected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_NO_SSID_AVAIL:
      return "ssid_unavailable";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_IDLE_STATUS:
      return "idle";
    default:
      return "unknown";
  }
}

const char* wifiModeToString(wifi_mode_t mode) {
  switch (mode) {
    case WIFI_MODE_STA:
      return "sta";
    case WIFI_MODE_AP:
      return "ap";
    case WIFI_MODE_APSTA:
      return "apsta";
    case WIFI_MODE_NULL:
      return "off";
    default:
      return "unknown";
  }
}

bool parseBoolText(const String& raw, bool default_value) {
  String v = raw;
  v.trim();
  v.toLowerCase();
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  return default_value;
}

String payloadToHex(const std::vector<uint8_t>& payload) {
  static const char* kHex = "0123456789ABCDEF";
  String out;
  out.reserve(payload.size() * 2U);
  for (uint8_t b : payload) {
    out += kHex[(b >> 4) & 0x0FU];
    out += kHex[b & 0x0FU];
  }
  return out;
}

String payloadToText(const std::vector<uint8_t>& payload) {
  String out;
  out.reserve(payload.size());
  for (uint8_t b : payload) {
    const char c = static_cast<char>(b);
    if (c >= 32 && c <= 126) {
      out += c;
    } else if (c == '\n' || c == '\r' || c == '\t') {
      out += c;
    } else {
      out += '.';
    }
  }
  return out;
}

}  // namespace

void MasterWifiFrontendUi::begin(espnow_link::ManagementFrontendAdapter* adapter, const Config& cfg) {
  adapter_ = adapter;
  cfg_ = cfg;
  last_reconnect_attempt_ms_ = static_cast<uint32_t>(millis());
  ap_mode_active_ = false;
  sta_reconnect_enabled_ = false;
  espnow_channel_ = 0U;

  if (adapter_ == nullptr) {
    Serial.println("[MASTER][WIFI-UI] adapter null; disabled");
    return;
  }

  espnow_channel_ = resolveEspNowChannel_();
  sta_reconnect_enabled_ = (cfg_.ssid != nullptr && cfg_.ssid[0] != '\0');

  if (sta_reconnect_enabled_) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg_.ssid, cfg_.password);

    const uint32_t started_ms = static_cast<uint32_t>(millis());
    while (WiFi.status() != WL_CONNECTED &&
           static_cast<uint32_t>(millis()) - started_ms < cfg_.sta_connect_timeout_ms) {
      delay(40);
    }

    if (cfg_.enforce_channel_match &&
        espnow_channel_ >= 1U &&
        espnow_channel_ <= 14U &&
        WiFi.status() == WL_CONNECTED &&
        WiFi.channel() != espnow_channel_) {
      Serial.printf("[MASTER][WIFI-UI] STA channel mismatch wifi=%u espnow=%u; fallback AP enabled\n",
                    static_cast<unsigned int>(WiFi.channel()),
                    static_cast<unsigned int>(espnow_channel_));
      WiFi.disconnect(false, false);
      sta_reconnect_enabled_ = false;
      (void)startApFallback_("sta_channel_mismatch");
    }
  }

  if (WiFi.status() != WL_CONNECTED && cfg_.fallback_to_ap) {
    sta_reconnect_enabled_ = false;
    (void)startApFallback_("sta_unavailable");
  }

  setupRoutes_();
  server_.begin();
  const String sta_ip = WiFi.localIP().toString();
  const String ap_ip = WiFi.softAPIP().toString();
  const String chosen_ip = ap_mode_active_ ? ap_ip : sta_ip;
  Serial.printf("[MASTER][WIFI-UI] UI ready mode=%s wifi_ch=%u espnow_ch=%u url=http://%s/\n",
                wifiModeToString(WiFi.getMode()),
                static_cast<unsigned int>(WiFi.channel()),
                static_cast<unsigned int>(espnow_channel_),
                chosen_ip.c_str());
}

void MasterWifiFrontendUi::begin(espnow_link::ManagementFrontendAdapter* adapter) {
  begin(adapter, Config{});
}

void MasterWifiFrontendUi::setRadioTransitionTestHooks(const std::function<bool(void)>& start_cb,
                                                       const std::function<String(void)>& status_cb,
                                                       const std::function<bool(void)>& abort_cb) {
  radio_test_start_cb_ = start_cb;
  radio_test_status_cb_ = status_cb;
  radio_test_abort_cb_ = abort_cb;
}

void MasterWifiFrontendUi::loop() {
  tryReconnectWifi_();
  if (adapter_ != nullptr) {
    (void)adapter_->drainToCache(12U, 24U);
  }
  server_.handleClient();
}

void MasterWifiFrontendUi::setupRoutes_() {
  server_.on("/", HTTP_GET, [this]() { handleRoot_(); });
  server_.on("/api/ping", HTTP_GET, [this]() { handlePing_(); });
  server_.on("/api/discovery/start", HTTP_ANY, [this]() { handleDiscoveryStart_(); });
  server_.on("/api/discovery/stop", HTTP_ANY, [this]() { handleDiscoveryStop_(); });
  server_.on("/api/discovery/snapshot", HTTP_GET, [this]() { handleDiscoverySnapshot_(); });
  server_.on("/api/pair", HTTP_ANY, [this]() { handlePair_(); });
  server_.on("/api/unpair", HTTP_ANY, [this]() { handleUnpair_(); });
  server_.on("/api/paired/snapshot", HTTP_GET, [this]() { handlePairedSnapshot_(); });
  server_.on("/api/op/status", HTTP_GET, [this]() { handleOperationStatus_(); });
  server_.on("/api/op/wait", HTTP_GET, [this]() { handleOperationWait_(); });
  server_.on("/api/events", HTTP_GET, [this]() { handleEvents_(); });
  server_.on("/api/state", HTTP_GET, [this]() { handleState_(); });
  server_.on("/api/command", HTTP_ANY, [this]() { handleCommand_(); });
  server_.on("/api/radiotest/start", HTTP_ANY, [this]() { handleRadioTestStart_(); });
  server_.on("/api/radiotest/status", HTTP_GET, [this]() { handleRadioTestStatus_(); });
  server_.on("/api/radiotest/abort", HTTP_ANY, [this]() { handleRadioTestAbort_(); });

  server_.onNotFound([this]() {
    sendJson_(404, "{\"ok\":false,\"error\":\"not_found\"}");
  });
}

void MasterWifiFrontendUi::handleRoot_() {
  static const char kPage[] PROGMEM = R"HTML(
<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Master Management Test UI</title>
<style>
body{font-family:ui-sans-serif,system-ui;margin:12px;background:#f5f7fb;color:#1e293b}
h2{margin:0 0 6px 0}h3{margin:8px 0 6px 0}
.card{background:#fff;border:1px solid #dbe2ea;border-radius:10px;padding:10px;margin-bottom:10px}
button,input{padding:6px 8px;margin:3px;border-radius:8px;border:1px solid #c8d0da}
button{background:#0b67d0;color:#fff;border:0;cursor:pointer}
button.alt{background:#64748b}
button.good{background:#0b8f63}
button.warn{background:#b26a00}
button.danger{background:#b42318}
table{border-collapse:collapse;width:100%;font-size:13px}th,td{border-bottom:1px solid #e7edf4;padding:6px;text-align:left}
.row{display:flex;flex-wrap:wrap;gap:6px;align-items:center}
.help{font-size:12px;color:#475569}
.cmd{display:inline-flex;align-items:center;gap:4px}
.small{font-size:12px}
.layout{display:grid;grid-template-columns:minmax(0,1fr) 520px;gap:12px;align-items:start}
.leftPane{min-width:0}
.rightPane{min-width:0}
.sticky{position:sticky;top:10px}
#respMeta{margin-bottom:8px;padding:8px;border-radius:8px;background:#eef2f7;color:#334155}
#respMeta.err{background:#fdecea;color:#9f1239}
pre{background:#0f172a;color:#e2e8f0;padding:10px;border-radius:8px;white-space:pre-wrap;max-height:72vh;overflow:auto}
@media (max-width: 1200px){.layout{grid-template-columns:1fr}.sticky{position:static}}
</style></head><body>
<h2>Master Management Test UI</h2>
<div class="help">Goal: test most CLI-equivalent management commands from browser. Select target MAC first for slave commands.</div>
<div class="layout"><div class="leftPane">

<div class="card">
  <h3>Live State</h3>
  <div class="row">
    <button onclick="refreshState()">Refresh State</button>
    <button class="alt" onclick="refreshEvents()">Refresh Events</button>
    <button class="warn" onclick="runUrl('/api/radiotest/start')">Start Radio Transition Test</button>
    <button class="danger" onclick="runUrl('/api/radiotest/abort')">Abort Radio Transition Test</button>
    <button class="alt" onclick="runUrl('/api/radiotest/status')">Radio Test Status</button>
    <span id="stateLine" class="small"></span>
  </div>
  <div class="help">Start/abort app-owned radio transition workflow (serial logs show each step and prompts).</div>
</div>

<div class="card">
  <h3>Discovery And Pairing</h3>
  <div class="row">
    <input id="discWindow" value="12000" title="Discovery window ms">
    <button onclick="runUrl('/api/discovery/start?window_ms='+enc('discWindow')+'&timeout_ms='+enc('timeoutMs'))">Discovery Start</button>
    <button class="alt" onclick="runUrl('/api/discovery/stop?timeout_ms='+enc('timeoutMs'))">Discovery Stop</button>
    <button onclick="refreshDiscovery()">Discovery Snapshot</button>
    <button onclick="refreshPaired()">Paired Snapshot</button>
  </div>
  <div class="row">
    <input id="targetMac" placeholder="Target MAC AA:BB:CC:DD:EE:FF">
    <button class="good" onclick="runUrl('/api/pair?peer='+enc('targetMac')+'&timeout_ms='+enc('timeoutMs'))">Pair Target</button>
    <button class="warn" onclick="runUrl('/api/unpair?peer='+enc('targetMac')+'&timeout_ms='+enc('timeoutMs'))">Unpair Target</button>
  </div>
  <div class="row">
    <div style="flex:1;min-width:280px">
      <div class="small">Discovered (with role if provided by discovery payload)</div>
      <table><thead><tr><th>MAC</th><th>Role</th><th>Name</th><th>RSSI</th><th>Use</th></tr></thead><tbody id="discRows"></tbody></table>
    </div>
    <div style="flex:1;min-width:280px">
      <div class="small">Paired</div>
      <table><thead><tr><th>MAC</th><th>Role</th><th>Use</th></tr></thead><tbody id="pairRows"></tbody></table>
    </div>
  </div>
</div>

<div class="card">
  <h3>Command Parameters</h3>
  <div class="row">
    <label class="small">timeout_ms <input id="timeoutMs" value="5000"></label>
    <label class="small">wait_terminal <input id="waitTerminal" value="1"></label>
    <label class="small">setting key <input id="settingKey" placeholder="example: v0.detect_fall_delta_cm"></label>
    <label class="small">setting value <input id="settingVal" placeholder="example: 35"></label>
    <label class="small">channel <input id="channelVal" value="6"></label>
    <label class="small">path <input id="pathVal" value="/"></label>
    <label class="small">epoch_s <input id="epochVal" placeholder="UNIX seconds"></label>
    <label class="small">ota_path <input id="otaPath" value="/sd/fw.bin"></label>
    <label class="small">ota_target <input id="otaTarget" value="running"></label>
    <label class="small">ota_chunk <input id="otaChunk" value="220"></label>
    <label class="small">ota_role <input id="otaRole" value="s"></label>
    <label class="small">ota_id <input id="otaId" placeholder="archive id"></label>
    <label class="small">trig_idx <input id="trigIdx" value="1"></label>
    <label class="small">trig_dir(1/2) <input id="trigDir" value="1"></label>
    <label class="small">trig_delay_ms <input id="trigDelay" value="40"></label>
    <label class="small">trig_hold_ms <input id="trigHold" value="120"></label>
    <label class="small">trig_src_vi <input id="trigSrcVi" value="255"></label>
  </div>
  <div class="help">Each button runs <code>/api/command?name=...</code>. Output includes command name, req/op IDs, terminal state, payload hex/text.</div>
  <div class="help">SEMU/SENS key examples: <code>detect_fall_delta_cm</code>, <code>detect_release_delta_cm</code>, <code>detect_window_ms</code>, <code>detect_clear_hold_ms</code>, <code>tfl_a_calib_mm</code>, <code>tfl_b_calib_mm</code>.</div>
  <div class="help">SEMU child key example: <code>v0.detect_fall_delta_cm</code>, <code>v0.tfl_a_calib_mm</code>. REMU child example: <code>v0.output_enable</code>. Telemetry includes <code>tfl_a_flux</code>, <code>tfl_b_flux</code>, <code>tfl_a_temp_c</code>, <code>tfl_b_temp_c</code>.</div>
</div>

<div class="card">
  <h3>Core Slave Commands</h3>
  <div class="row">
    <span class="cmd"><button onclick="runCmd('desc_get','Read target descriptor/profile')">DescGet</button></span>
    <span class="cmd"><button onclick="runCmd('caps_get','Read target capabilities')">CapsGet</button></span>
    <span class="cmd"><button onclick="runCmd('settings_get','Read full settings list')">SettingsGet</button></span>
    <span class="cmd"><button onclick="runCmd('telem_pull','Pull telemetry once')">TelemPull</button></span>
    <span class="cmd"><button onclick="runCmd('live_get','Read liveness status')">LiveGet</button></span>
    <span class="cmd"><button onclick="runCmd('time_get','Read target time')">TimeGet</button></span>
    <span class="cmd"><button onclick="runCmd('ping_get','Round-trip ping')">PingGet</button></span>
    <span class="cmd"><button class="warn" onclick="runCmd('audio_ping','Trigger audio ping on target')">AudioPing</button></span>
  </div>
  <div class="row">
    <button onclick="runCmd('setting_get','Read one setting key')">SettingGet(key)</button>
    <button class="good" onclick="runCmd('setting_set','Write one setting key')">SettingSet(key,value)</button>
    <button class="warn" onclick="runCmd('restart_slave','Restart selected slave')">RestartSlave</button>
    <button class="danger" onclick="runCmd('reset_slave','Factory reset selected slave')">ResetSlave</button>
  </div>
</div>

<div class="card">
  <h3>Master Runtime Controls</h3>
  <div class="row">
    <button onclick="runCmd('cli_status','Read local CLI enable state')">CliStatus</button>
    <button class="good" onclick="runCmd('cli_enable','Enable local CLI')">CliEnable</button>
    <button class="warn" onclick="runCmd('cli_disable','Disable local CLI')">CliDisable</button>
    <button onclick="runCmd('chainloop_status','Read chain loop status')">ChainLoopStatus</button>
    <button class="good" onclick="runCmd('chainloop_enable','Enable chain auto-loop on supported slaves')">ChainLoopEnable</button>
    <button class="warn" onclick="runCmd('chainloop_disable','Disable chain auto-loop')">ChainLoopDisable</button>
    <button onclick="runCmd('channel_runtime_get','Read runtime channel table')">ChannelRuntimeGet</button>
    <button class="warn" onclick="runCmd('channel_sync_all','Sync all paired slaves to channel value')">ChannelSyncAll</button>
  </div>
</div>

<div class="card">
  <h3>Push, Logs, Storage, Metrics</h3>
  <div class="row">
    <button onclick="runCmd('push_get','Read telemetry push status')">PushGet</button>
    <button class="good" onclick="runCmd('push_start','Start push stream')">PushStart</button>
    <button onclick="runCmd('push_update','Update push stream')">PushUpdate</button>
    <button class="warn" onclick="runCmd('push_pause','Pause push stream')">PushPause</button>
    <button class="good" onclick="runCmd('push_resume','Resume push stream')">PushResume</button>
    <button class="warn" onclick="runCmd('push_stop','Stop push stream')">PushStop</button>
  </div>
  <div class="row">
    <button onclick="runCmd('log_local_status','Read local log status')">LogLocalStatus</button>
    <button class="good" onclick="runCmd('log_local_enable','Enable local logs')">LogLocalEnable</button>
    <button class="warn" onclick="runCmd('log_local_disable','Disable local logs')">LogLocalDisable</button>
    <button class="danger" onclick="runCmd('log_local_clear','Clear local logs')">LogLocalClear</button>
    <button onclick="runCmd('log_remote_status','Read remote log status (target)')">LogRemoteStatus</button>
  </div>
  <div class="row">
    <button onclick="runCmd('storage_info','Storage info')">StorageInfo</button>
    <button onclick="runCmd('storage_list','Storage list path')">StorageList</button>
    <button onclick="runCmd('storage_stat','Storage stat path')">StorageStat</button>
    <button class="danger" onclick="runCmd('storage_format','Format storage')">StorageFormat</button>
    <button onclick="runCmd('metrics_get','Read metrics counters')">MetricsGet</button>
    <button class="warn" onclick="runCmd('metrics_reset','Reset metrics counters')">MetricsReset</button>
    <button onclick="runCmd('queue_get','Read queue depth from service')">QueueGet</button>
  </div>
</div>

<div class="card">
  <h3>OTA Commands</h3>
  <div class="help">Use <code>ota_path</code> for local file path, <code>ota_target</code> for apply target, <code>ota_role</code>/<code>ota_id</code> for archive commands.</div>
  <div class="row">
    <button onclick="runCmd('ota_status','Read OTA status')">OtaStatus</button>
    <button onclick="runCmd('ota_manifest','Read OTA manifest')">OtaManifest</button>
    <button onclick="runCmd('ota_manifest_rebuild','Rebuild OTA manifest')">OtaManifestRebuild</button>
    <button onclick="runCmd('ota_capacity','Read OTA capacity')">OtaCapacity</button>
    <button onclick="runCmd('ota_gate','Read OTA gate')">OtaGate</button>
  </div>
  <div class="row">
    <button class="good" onclick="runCmd('ota_push_start','Start OTA push from ota_path')">OtaPushStart</button>
    <button onclick="runCmd('ota_push_status','Read OTA push status')">OtaPushStatus</button>
    <button class="warn" onclick="runCmd('ota_push_abort','Abort OTA push')">OtaPushAbort</button>
    <button class="good" onclick="runCmd('ota_update_start','Start OTA update to selected target from ota_path')">OtaUpdateStart</button>
    <button class="warn" onclick="runCmd('ota_master_update_start','Start OTA update on master from ota_path')">OtaMasterUpdateStart</button>
  </div>
  <div class="row">
    <button class="good" onclick="runCmd('ota_apply','Apply OTA target')">OtaApply</button>
    <button class="warn" onclick="runCmd('ota_rollback','Rollback OTA')">OtaRollback</button>
    <button class="warn" onclick="runCmd('ota_clear_scope','Clear OTA scope path/value')">OtaClearScope</button>
  </div>
  <div class="row">
    <button onclick="runCmd('ota_archive_list','List OTA archive entries')">OtaArchiveList</button>
    <button onclick="runCmd('ota_archive_save_running','Save running image to archive')">OtaArchiveSaveRunning</button>
    <button onclick="runCmd('ota_archive_save_staged','Save staged image to archive')">OtaArchiveSaveStaged</button>
    <button onclick="runCmd('ota_archive_restore','Restore archive id')">OtaArchiveRestore</button>
    <button onclick="runCmd('ota_archive_verify','Verify archive id')">OtaArchiveVerify</button>
    <button class="warn" onclick="runCmd('ota_archive_delete','Delete archive id')">OtaArchiveDelete</button>
    <button class="danger" onclick="runCmd('ota_archive_clear','Clear archive role')">OtaArchiveClear</button>
  </div>
</div>

<div class="card">
  <h3>Topology Commands</h3>
  <div class="help">Use trigger fields for <code>TopologyTriggerSend</code>.</div>
  <div class="row">
    <button onclick="runCmd('topology_status','Read topology staged/committed status')">TopologyStatus</button>
    <button onclick="runCmd('topology_slots_committed','Read committed topology slots')">TopologySlots(committed)</button>
    <button onclick="runCmd('topology_slots_staged','Read staged topology slots')">TopologySlots(staged)</button>
    <button class="warn" onclick="runCmd('topology_commit','Commit staged topology')">TopologyCommit</button>
    <button class="good" onclick="runCmd('topology_trigger_send','Send topology trigger with trigger params')">TopologyTriggerSend</button>
  </div>
</div>

<div class="card">
  <h3>Events And Operation Tracking</h3>
  <div class="row">
    <input id="opId" placeholder="operation id">
    <button onclick="runUrl('/api/op/status?op_id='+enc('opId'))">OpStatus</button>
    <button onclick="runUrl('/api/op/wait?op_id='+enc('opId')+'&timeout_ms='+enc('timeoutMs'))">OpWait</button>
    <input id="evtSince" value="0">
    <input id="evtLimit" value="60">
    <button onclick="refreshEvents()">Events</button>
  </div>
</div>

  </div>
  <div class="rightPane">
    <div class="card sticky">
      <h3>Response Panel</h3>
      <div id="respMeta" class="small">No command sent yet.</div>
      <div class="row">
        <button class="alt" onclick="clearOut()">Clear Response</button>
        <button class="alt" onclick="copyOut()">Copy Response</button>
      </div>
      <pre id="out">ready</pre>
    </div>
  </div>
</div>

<script>
function q(id){return document.getElementById(id);}
function enc(id){return encodeURIComponent((q(id).value||'').trim());}
function setTarget(mac){q('targetMac').value=mac||'';}

function stamp(){
  var d=new Date();
  return d.toLocaleTimeString();
}

function setMeta(text,isErr){
  var m=q('respMeta');
  if(!m){return;}
  m.textContent='['+stamp()+'] '+text;
  m.className='small'+(isErr?' err':'');
}

async function fetchJson(url){
  try{
    const r=await fetch(url,{method:'GET'});
    const t=await r.text();
    let out=null;
    try{
      out=JSON.parse(t);
    }catch(e){
      out={ok:false,parse_error:true,raw:t};
    }
    out.http_status=r.status;
    out.http_ok=r.ok;
    out.request_url=url;
    return out;
  }catch(e){
    return {ok:false,error:'fetch_failed',message:String(e),request_url:url};
  }
}
function show(obj){
  q('out').textContent=JSON.stringify(obj,null,2);
  if(obj&&obj.op_id){q('opId').value=obj.op_id;}
}

async function safeRun(label,url){
  setMeta('Sending '+label+' ...',false);
  const out=await fetchJson(url);
  show(out);
  const ok=(out&&out.ok===true) || (out&&out.http_ok===true&&out.ok!==false);
  setMeta(label+(ok?' completed':' failed'),!ok);
  return out;
}

async function runUrl(url){await safeRun(url,url);}

async function runCmd(name,info){
  const p=new URLSearchParams();
  p.set('name',name);
  p.set('target',q('targetMac').value||'');
  p.set('timeout_ms',q('timeoutMs').value||'5000');
  p.set('wait',q('waitTerminal').value||'1');
  p.set('key',q('settingKey').value||'');
  p.set('value',q('settingVal').value||'');
  p.set('channel',q('channelVal').value||'');
  p.set('path',q('pathVal').value||'/');
  p.set('epoch_s',q('epochVal').value||'');
  p.set('ota_path',q('otaPath').value||'');
  p.set('ota_target',q('otaTarget').value||'');
  p.set('ota_chunk',q('otaChunk').value||'');
  p.set('ota_role',q('otaRole').value||'');
  p.set('ota_id',q('otaId').value||'');
  p.set('trig_idx',q('trigIdx').value||'');
  p.set('trig_dir',q('trigDir').value||'');
  p.set('trig_delay_ms',q('trigDelay').value||'');
  p.set('trig_hold_ms',q('trigHold').value||'');
  p.set('trig_src_vi',q('trigSrcVi').value||'');
  const out=await safeRun('command:'+name,'/api/command?'+p.toString());
  out.ui_info=info||'';
  show(out);
}

async function refreshState(){
  const s=await safeRun('state','/api/state');
  show(s);
  q('stateLine').textContent='wifi='+(s.wifi_status||'?')+' mode='+(s.wifi_mode||'?')+' ch='+(s.wifi_channel||'?')+'/'+(s.espnow_channel||'?')+' ip='+(s.ip||'?')+' paired='+(s.paired_count||0)+' discovered='+(s.discovered_count||0);
}

async function refreshDiscovery(){
  const out=await safeRun('discovery_snapshot','/api/discovery/snapshot?refresh=1&timeout_ms='+(q('timeoutMs').value||'5000'));
  const rows=q('discRows'); rows.innerHTML='';
  (out.peers||[]).forEach(p=>{
    const rssiText=(typeof p.rssi!=='undefined' && p.rssi!==null)?p.rssi:'';
    const tr=document.createElement('tr');
    const tdMac=document.createElement('td');
    tdMac.textContent=p.peer||'';
    const tdRole=document.createElement('td');
    tdRole.textContent=p.role||'UNKNOWN';
    const tdName=document.createElement('td');
    tdName.textContent=p.name||'';
    const tdRssi=document.createElement('td');
    tdRssi.textContent=String(rssiText);
    const tdUse=document.createElement('td');
    const btn=document.createElement('button');
    btn.textContent='use';
    btn.onclick=function(){setTarget(p.peer||'');};
    tdUse.appendChild(btn);
    tr.appendChild(tdMac);
    tr.appendChild(tdRole);
    tr.appendChild(tdName);
    tr.appendChild(tdRssi);
    tr.appendChild(tdUse);
    rows.appendChild(tr);
  });
  show(out);
}

async function refreshPaired(){
  const out=await safeRun('paired_snapshot','/api/paired/snapshot?refresh=1&timeout_ms='+(q('timeoutMs').value||'5000'));
  const rows=q('pairRows'); rows.innerHTML='';
  (out.peers||[]).forEach(p=>{
    const tr=document.createElement('tr');
    const tdMac=document.createElement('td');
    tdMac.textContent=p.peer||'';
    const tdRole=document.createElement('td');
    tdRole.textContent=p.role||'UNKNOWN';
    const tdUse=document.createElement('td');
    const btn=document.createElement('button');
    btn.textContent='use';
    btn.onclick=function(){setTarget(p.peer||'');};
    tdUse.appendChild(btn);
    tr.appendChild(tdMac);
    tr.appendChild(tdRole);
    tr.appendChild(tdUse);
    rows.appendChild(tr);
  });
  show(out);
}

async function refreshEvents(){
  const out=await safeRun('events','/api/events?since_seq='+enc('evtSince')+'&limit='+enc('evtLimit'));
  if(out&&out.next_seq!==undefined){q('evtSince').value=out.next_seq;}
  show(out);
}

function clearOut(){
  q('out').textContent='cleared';
  setMeta('Response cleared',false);
}

async function copyOut(){
  try{
    await navigator.clipboard.writeText(q('out').textContent||'');
    setMeta('Response copied to clipboard',false);
  }catch(e){
    setMeta('Copy failed: '+String(e),true);
  }
}

window.addEventListener('error',function(e){
  setMeta('JS error: '+(e&&e.message?e.message:'unknown'),true);
});
window.addEventListener('unhandledrejection',function(e){
  setMeta('Promise error: '+String(e&&e.reason?e.reason:e),true);
});

refreshState();
refreshDiscovery();
refreshPaired();
</script>
</body></html>
)HTML";
  server_.send(200, "text/html; charset=utf-8", kPage);
}
void MasterWifiFrontendUi::handlePing_() {
  String json = "{";
  json += "\"ok\":true,";
  json += "\"ms\":" + String(static_cast<unsigned long>(millis())) + ",";
  json += "\"wifi_mode\":\"" + String(wifiModeToString(WiFi.getMode())) + "\",";
  json += "\"wifi_status\":\"" + String(wifiStatusToString(WiFi.status())) + "\",";
  json += "\"wifi_channel\":" + String(static_cast<unsigned int>(WiFi.channel())) + ",";
  json += "\"espnow_channel\":" + String(static_cast<unsigned int>(espnow_channel_)) + ",";
  json += "\"sta_ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handleDiscoveryStart_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }
  const uint32_t window_ms = parseU32Arg_("window_ms", 12000U);
  const uint32_t timeout_ms = parseU32Arg_("timeout_ms", 0U);
  espnow_link::ManagementFrontendAdapter::OperationHandle op{};
  if (!adapter_->discoveryStartTracked(window_ms, op, timeout_ms)) {
    sendJson_(500, "{\"ok\":false,\"error\":\"submit_failed\"}");
    return;
  }
  String json = "{";
  json += "\"ok\":true,";
  json += "\"op_id\":" + String(static_cast<unsigned long>(op.operation_id)) + ",";
  json += "\"req_id\":" + String(static_cast<unsigned long>(op.req_id)) + "}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handleDiscoveryStop_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }
  const uint32_t timeout_ms = parseU32Arg_("timeout_ms", 0U);
  espnow_link::ManagementFrontendAdapter::OperationHandle op{};
  if (!adapter_->discoveryStopTracked(op, timeout_ms)) {
    sendJson_(500, "{\"ok\":false,\"error\":\"submit_failed\"}");
    return;
  }
  String json = "{";
  json += "\"ok\":true,";
  json += "\"op_id\":" + String(static_cast<unsigned long>(op.operation_id)) + ",";
  json += "\"req_id\":" + String(static_cast<unsigned long>(op.req_id)) + "}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handleDiscoverySnapshot_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }

  const bool refresh = parseU32Arg_("refresh", 1U) != 0U;
  const uint32_t timeout_ms = parseU32Arg_("timeout_ms", 0U);
  espnow_link::ManagementFrontendAdapter::CommandRunResult run{};
  bool ok = true;
  if (refresh) {
    ok = adapter_->commandRunAndWait(static_cast<uint16_t>(espnow_link::ManagementCommandId::DiscoverySnapshotGet),
                                     {},
                                     run,
                                     timeout_ms);
  }
  (void)adapter_->drainToCache();

  const auto& peers = adapter_->cachedDiscoveredPeers();
  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"refresh_req_id\":" + String(static_cast<unsigned long>(run.req_id)) + ",";
  json += "\"count\":" + String(static_cast<unsigned int>(peers.size())) + ",";
  json += "\"peers\":[";

  for (size_t i = 0U; i < peers.size(); ++i) {
    if (i != 0U) json += ",";
    json += "{\"peer\":\"" + macToString_(peers[i]) + "\"";

    espnow_link::ManagementFrontendAdapter::EventSnapshotPage page{};
    adapter_->eventsSnapshot(0U, adapter_->eventRingCapacity(), page);
    for (size_t e = page.events.size(); e > 0U; --e) {
      const auto& rec = page.events[e - 1U];
      if (rec.event.event_id != espnow_link::ManagementEventId::DiscoveryUpdate) continue;
      espnow_link::ManagementDiscoveryUpdatePayload upd{};
      if (!espnow_link::ManagementFrontendAdapter::decodeDiscoveryUpdateEvent(rec.event, upd)) continue;
      if (!(upd.peer == peers[i])) continue;
      json += ",\"rssi\":" + String(upd.rssi);
      json += ",\"name\":\"" + jsonEscape_(String(upd.name.c_str())) + "\"";
      json += ",\"role_code\":" + String(static_cast<unsigned int>(upd.role_code));
      json += ",\"role\":\"" + String(roleToString(upd.role_code)) + "\"";
      break;
    }

    json += "}";
  }

  json += "]}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handlePair_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }
  espnow_link::MacAddress peer{};
  if (!parseMacArg_("peer", peer) && !parseMacArg_("mac", peer)) {
    sendJson_(400, "{\"ok\":false,\"error\":\"missing_or_invalid_peer\"}");
    return;
  }
  const uint32_t timeout_ms = parseU32Arg_("timeout_ms", 0U);
  espnow_link::ManagementFrontendAdapter::OperationHandle op{};
  if (!adapter_->pairPeerTracked(peer, op, timeout_ms)) {
    sendJson_(500, "{\"ok\":false,\"error\":\"submit_failed\"}");
    return;
  }
  String json = "{";
  json += "\"ok\":true,";
  json += "\"peer\":\"" + macToString_(peer) + "\",";
  json += "\"op_id\":" + String(static_cast<unsigned long>(op.operation_id)) + ",";
  json += "\"req_id\":" + String(static_cast<unsigned long>(op.req_id)) + "}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handleUnpair_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }
  espnow_link::MacAddress peer{};
  if (!parseMacArg_("peer", peer) && !parseMacArg_("mac", peer)) {
    sendJson_(400, "{\"ok\":false,\"error\":\"missing_or_invalid_peer\"}");
    return;
  }
  const uint32_t timeout_ms = parseU32Arg_("timeout_ms", 0U);
  espnow_link::ManagementFrontendAdapter::OperationHandle op{};
  if (!adapter_->unpairPeerTracked(peer, op, timeout_ms)) {
    sendJson_(500, "{\"ok\":false,\"error\":\"submit_failed\"}");
    return;
  }
  String json = "{";
  json += "\"ok\":true,";
  json += "\"peer\":\"" + macToString_(peer) + "\",";
  json += "\"op_id\":" + String(static_cast<unsigned long>(op.operation_id)) + ",";
  json += "\"req_id\":" + String(static_cast<unsigned long>(op.req_id)) + "}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handlePairedSnapshot_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }
  const uint32_t timeout_ms = parseU32Arg_("timeout_ms", 0U);
  std::vector<espnow_link::ManagementPairedPeerInfo> peers{};
  bool ok = adapter_->pairedSnapshotGetResolved(peers, timeout_ms);
  if (!ok) {
    peers.clear();
    for (const auto& peer : adapter_->cachedPairedPeers()) {
      espnow_link::ManagementPairedPeerInfo p{};
      p.peer = peer;
      peers.push_back(p);
    }
  }

  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"count\":" + String(static_cast<unsigned int>(peers.size())) + ",";
  json += "\"peers\":[";
  for (size_t i = 0U; i < peers.size(); ++i) {
    if (i != 0U) json += ",";
    json += "{\"peer\":\"" + macToString_(peers[i].peer) + "\",";
    json += "\"role_code\":" + String(static_cast<unsigned int>(peers[i].role_code)) + ",";
    json += "\"role\":\"" + String(roleToString(peers[i].role_code)) + "\"}";
  }
  json += "]}";
  sendJson_(200, json);
}
void MasterWifiFrontendUi::handleOperationStatus_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }
  const uint32_t op_id = parseU32Arg_("op_id", parseU32Arg_("req_id", 0U));
  if (op_id == 0U) {
    sendJson_(400, "{\"ok\":false,\"error\":\"missing_op_id\"}");
    return;
  }

  espnow_link::ManagementFrontendAdapter::OperationStatus st{};
  if (!adapter_->operationStatus(op_id, st)) {
    sendJson_(404, "{\"ok\":false,\"error\":\"operation_not_found\"}");
    return;
  }

  String json = "{";
  json += "\"ok\":true,";
  json += "\"op_id\":" + String(static_cast<unsigned long>(op_id)) + ",";
  json += "\"cmd_id\":" + String(static_cast<unsigned int>(st.cmd_id)) + ",";
  json += "\"state\":\"" + String(opStateToString_(st.state)) + "\",";
  json += "\"terminal\":" + String(st.terminal ? "true" : "false") + ",";
  json += "\"status\":\"" + String(statusToString_(st.status_code)) + "\",";
  json += "\"stage\":\"" + jsonEscape_(String(st.stage)) + "\",";
  json += "\"message\":\"" + jsonEscape_(String(st.message.c_str())) + "\"";
  json += "}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handleOperationWait_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }
  const uint32_t op_id = parseU32Arg_("op_id", parseU32Arg_("req_id", 0U));
  const uint32_t timeout_ms = parseU32Arg_("timeout_ms", 0U);
  if (op_id == 0U) {
    sendJson_(400, "{\"ok\":false,\"error\":\"missing_op_id\"}");
    return;
  }

  espnow_link::ManagementFrontendAdapter::OperationStatus st{};
  bool ok = adapter_->operationWait(op_id, st, timeout_ms);

  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"op_id\":" + String(static_cast<unsigned long>(op_id)) + ",";
  json += "\"cmd_id\":" + String(static_cast<unsigned int>(st.cmd_id)) + ",";
  json += "\"state\":\"" + String(opStateToString_(st.state)) + "\",";
  json += "\"terminal\":" + String(st.terminal ? "true" : "false") + ",";
  json += "\"status\":\"" + String(statusToString_(st.status_code)) + "\",";
  json += "\"stage\":\"" + jsonEscape_(String(st.stage)) + "\",";
  json += "\"message\":\"" + jsonEscape_(String(st.message.c_str())) + "\"";
  json += "}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handleEvents_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }

  const uint64_t since_seq = static_cast<uint64_t>(parseU32Arg_("since_seq", 0U));
  size_t limit = parseSizeArg_("limit", 40U);
  if (limit == 0U) limit = 40U;
  if (limit > 200U) limit = 200U;

  (void)adapter_->drainToCache();
  espnow_link::ManagementFrontendAdapter::EventSnapshotPage page{};
  adapter_->eventsSnapshot(since_seq, limit, page);

  String json = "{";
  json += "\"ok\":true,";
  json += "\"next_seq\":" + String(static_cast<unsigned long>(page.next_seq)) + ",";
  json += "\"count\":" + String(static_cast<unsigned int>(page.events.size())) + ",";
  json += "\"events\":[";
  for (size_t i = 0U; i < page.events.size(); ++i) {
    if (i != 0U) json += ",";
    const auto& e = page.events[i];
    json += "{";
    json += "\"seq\":" + String(static_cast<unsigned long>(e.seq)) + ",";
    json += "\"event\":\"" + jsonEscape_(String(e.event_name.c_str())) + "\",";
    json += "\"cmd_id\":" + String(static_cast<unsigned int>(e.event.cmd_id)) + ",";
    json += "\"req_id\":" + String(static_cast<unsigned long>(e.event.req_id)) + ",";
    json += "\"status\":\"" + jsonEscape_(String(e.status_name.c_str())) + "\"";
    json += "}";
  }
  json += "]}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handleState_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }

  (void)adapter_->drainToCache();
  const auto& discovered = adapter_->cachedDiscoveredPeers();
  const auto& paired = adapter_->cachedPairedPeers();
  espnow_link::ManagementFrontendAdapter::StateGenerations gens{};
  adapter_->stateGenerationsGet(gens);
  auto q = adapter_->queueDepth();

  String json = "{";
  json += "\"ok\":true,";
  json += "\"wifi_mode\":\"" + String(wifiModeToString(WiFi.getMode())) + "\",";
  json += "\"wifi_status\":\"" + String(wifiStatusToString(WiFi.status())) + "\",";
  json += "\"wifi_channel\":" + String(static_cast<unsigned int>(WiFi.channel())) + ",";
  json += "\"espnow_channel\":" + String(static_cast<unsigned int>(espnow_channel_)) + ",";
  json += "\"sta_reconnect\":" + String(sta_reconnect_enabled_ ? "true" : "false") + ",";
  json += "\"sta_ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"ip\":\"" + String(ap_mode_active_ ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"orch_wait_ms\":" + String(static_cast<unsigned long>(adapter_->orchestrationWaitDefaultMs())) + ",";
  json += "\"paired_generation\":" + String(static_cast<unsigned long>(gens.paired_generation)) + ",";
  json += "\"discovery_generation\":" + String(static_cast<unsigned long>(gens.discovery_generation)) + ",";
  json += "\"queue\":{";
  json += "\"requests\":" + String(static_cast<unsigned int>(q.requests)) + ",";
  json += "\"responses\":" + String(static_cast<unsigned int>(q.responses)) + ",";
  json += "\"events\":" + String(static_cast<unsigned int>(q.events));
  json += "},";
  json += "\"discovered_count\":" + String(static_cast<unsigned int>(discovered.size())) + ",";
  json += "\"paired_count\":" + String(static_cast<unsigned int>(paired.size()));
  json += "}";
  sendJson_(200, json);
}

void MasterWifiFrontendUi::handleRadioTestStart_() {
  if (!radio_test_start_cb_) {
    sendJson_(503, "{\"ok\":false,\"error\":\"radio_test_not_supported\"}");
    return;
  }
  const bool started = radio_test_start_cb_();
  String json = "{";
  json += "\"ok\":" + String(started ? "true" : "false") + ",";
  json += "\"started\":" + String(started ? "true" : "false");
  if (radio_test_status_cb_) {
    json += ",\"status\":";
    json += radio_test_status_cb_();
  }
  json += "}";
  sendJson_(started ? 200 : 409, json);
}

void MasterWifiFrontendUi::handleRadioTestStatus_() {
  if (!radio_test_status_cb_) {
    sendJson_(503, "{\"ok\":false,\"error\":\"radio_test_not_supported\"}");
    return;
  }
  sendJson_(200, radio_test_status_cb_());
}

void MasterWifiFrontendUi::handleRadioTestAbort_() {
  if (!radio_test_abort_cb_) {
    sendJson_(503, "{\"ok\":false,\"error\":\"radio_test_not_supported\"}");
    return;
  }
  const bool aborted = radio_test_abort_cb_();
  String json = "{";
  json += "\"ok\":" + String(aborted ? "true" : "false") + ",";
  json += "\"aborted\":" + String(aborted ? "true" : "false");
  if (radio_test_status_cb_) {
    json += ",\"status\":";
    json += radio_test_status_cb_();
  }
  json += "}";
  sendJson_(aborted ? 200 : 409, json);
}

void MasterWifiFrontendUi::handleCommand_() {
  if (adapter_ == nullptr) {
    sendJson_(503, "{\"ok\":false,\"error\":\"adapter_not_ready\"}");
    return;
  }

  String name = server_.hasArg("name") ? server_.arg("name") : "";
  name.trim();
  name.toLowerCase();
  if (name.isEmpty()) {
    sendJson_(400, "{\"ok\":false,\"error\":\"missing_name\"}");
    return;
  }

  const uint32_t timeout_ms = parseU32Arg_("timeout_ms", 0U);
  const bool wait_terminal = server_.hasArg("wait") ? parseBoolText(server_.arg("wait"), true) : true;
  const String key = server_.hasArg("key") ? server_.arg("key") : "";
  const String value = server_.hasArg("value") ? server_.arg("value") : "";
  String path = server_.hasArg("path") ? server_.arg("path") : "/";
  path.trim();
  if (path.isEmpty()) path = "/";
  String ota_path = server_.hasArg("ota_path") ? server_.arg("ota_path") : path;
  ota_path.trim();
  if (ota_path.isEmpty()) ota_path = path;
  String ota_target = server_.hasArg("ota_target") ? server_.arg("ota_target") : value;
  ota_target.trim();
  if (ota_target.isEmpty()) ota_target = "running";
  const uint16_t ota_chunk = static_cast<uint16_t>(parseU32Arg_("ota_chunk", 220U));
  const String ota_role_text = server_.hasArg("ota_role") ? server_.arg("ota_role") : "s";
  const char ota_role = (ota_role_text.length() > 0U) ? ota_role_text[0] : 's';
  String ota_id = server_.hasArg("ota_id") ? server_.arg("ota_id") : "";
  ota_id.trim();

  const uint8_t channel = static_cast<uint8_t>(parseU32Arg_("channel", 0U));
  const uint32_t read_offset = parseU32Arg_("offset", 0U);
  const uint16_t read_max = static_cast<uint16_t>(parseU32Arg_("max_bytes", 96U));
  const int32_t trig_idx = parseI32Arg_("trig_idx", 1);
  const uint8_t trig_dir = static_cast<uint8_t>(parseU32Arg_("trig_dir", 1U));
  const uint16_t trig_delay = static_cast<uint16_t>(parseU32Arg_("trig_delay_ms", 40U));
  const uint16_t trig_hold = static_cast<uint16_t>(parseU32Arg_("trig_hold_ms", 120U));
  const uint8_t trig_src_vi = static_cast<uint8_t>(parseU32Arg_("trig_src_vi", 255U));
  uint64_t epoch_s = static_cast<uint64_t>(parseU32Arg_("epoch_s", 0U));
  if (epoch_s == 0U) {
    epoch_s = static_cast<uint64_t>(millis() / 1000U);
  }

  espnow_link::MacAddress target{};
  const bool use_target = parseMacArg_("target", target);

  auto& c = adapter_->commands();
  uint32_t req_id = 0U;
  uint16_t cmd_id = 0U;
  bool submitted = false;
  auto submitCommand = [&](uint16_t command_id, const std::vector<uint8_t>& payload) -> bool {
    espnow_link::ManagementController::SubmitOptions submit_options{};
    submit_options.timeout_ms = timeout_ms;
    if (use_target) {
      submit_options.has_target_peer = true;
      submit_options.target_peer = target;
    }
    const espnow_link::ManagementController::SubmitResult submit_result =
        c.submit(command_id, payload, submit_options);
    req_id = submit_result.req_id;
    return submit_result.accepted;
  };
  auto submitCommandNoPayload = [&](espnow_link::ManagementCommandId command_id) -> bool {
    return submitCommand(static_cast<uint16_t>(command_id), {});
  };

  if (name == "desc_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::DescGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::DescGet);
  } else if (name == "caps_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::CapsGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::CapsGet);
  } else if (name == "settings_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::SettingsGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::SettingsGet);
  } else if (name == "setting_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::SettingGet);
    if (key.isEmpty()) {
      sendJson_(400, "{\"ok\":false,\"error\":\"missing_key\"}");
      return;
    }
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildSettingGetByKeyPayload(std::string(key.c_str())));
  } else if (name == "setting_set") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::SettingSet);
    if (key.isEmpty()) {
      sendJson_(400, "{\"ok\":false,\"error\":\"missing_key\"}");
      return;
    }
    submitted = submitCommand(cmd_id,
                              espnow_link::management_utils::buildSettingSetByKeyPayload(std::string(key.c_str()),
                                                                                          std::string(value.c_str())));
  } else if (name == "telem_pull") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::TelemPull);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::TelemPull);
  } else if (name == "live_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LiveGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::LiveGet);
  } else if (name == "time_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::TimeGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::TimeGet);
  } else if (name == "time_set") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::TimeSet);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildTimeSetPayload(epoch_s));
  } else if (name == "ping_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::PingGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::PingGet);
  } else if (name == "audio_ping") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::AudioPingRequest);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::AudioPingRequest);
  } else if (name == "restart_slave") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::RestartSlaveRequest);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::RestartSlaveRequest);
  } else if (name == "reset_slave") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::ResetSlaveRequest);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::ResetSlaveRequest);
  } else if (name == "restart_master") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::RestartMasterRequest);
    submitted = c.restartMasterRequest(&req_id, timeout_ms);
  } else if (name == "reset_master") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::ResetMasterRequest);
    submitted = c.resetMasterRequest(&req_id, timeout_ms);
  } else if (name == "cli_enable") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::CliControlSet);
    submitted = c.cliEnable(&req_id, timeout_ms);
  } else if (name == "cli_disable") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::CliControlSet);
    submitted = c.cliDisable(&req_id, timeout_ms);
  } else if (name == "cli_status") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::CliControlSet);
    submitted = c.cliStatusGet(&req_id, timeout_ms);
  } else if (name == "chainloop_enable") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::ChainLoopControlSet);
    submitted = c.chainLoopEnable(&req_id, timeout_ms);
  } else if (name == "chainloop_disable") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::ChainLoopControlSet);
    submitted = c.chainLoopDisable(&req_id, timeout_ms);
  } else if (name == "chainloop_status") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::ChainLoopControlSet);
    submitted = c.chainLoopStatusGet(&req_id, timeout_ms);
  } else if (name == "channel_runtime_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::ChannelRuntimeGet);
    submitted = c.channelRuntimeGet(&req_id, timeout_ms);
  } else if (name == "channel_sync_all") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::ChannelSyncAll);
    submitted = c.channelSyncAll(channel, &req_id, timeout_ms);
  } else if (name == "push_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::PushGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::PushGet);
  } else if (name == "push_start") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::PushStart);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::PushStart);
  } else if (name == "push_update") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::PushUpdate);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::PushUpdate);
  } else if (name == "push_pause") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::PushPause);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::PushPause);
  } else if (name == "push_resume") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::PushResume);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::PushResume);
  } else if (name == "push_stop") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::PushStop);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::PushStop);
  } else if (name == "log_local_status") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogLocalStatusGet);
    submitted = c.logLocalStatusGet(&req_id, timeout_ms);
  } else if (name == "log_local_enable") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogLocalControlSet);
    submitted = c.logLocalSetEnabled(true, &req_id, timeout_ms);
  } else if (name == "log_local_disable") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogLocalControlSet);
    submitted = c.logLocalSetEnabled(false, &req_id, timeout_ms);
  } else if (name == "log_local_clear") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogLocalClear);
    submitted = c.logLocalClear(&req_id, timeout_ms);
  } else if (name == "log_local_read") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogLocalRead);
    submitted = c.logLocalRead(read_offset, read_max, &req_id, timeout_ms);
  } else if (name == "log_remote_status") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogRemoteStatusGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::LogRemoteStatusGet);
  } else if (name == "log_remote_enable") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogRemoteControlSet);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildLogControlPayload(true));
  } else if (name == "log_remote_disable") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogRemoteControlSet);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildLogControlPayload(false));
  } else if (name == "log_remote_clear") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogRemoteClear);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::LogRemoteClear);
  } else if (name == "log_remote_read") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::LogRemoteRead);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildLogReadPayload(read_offset, read_max));
  } else if (name == "storage_info") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::StorageInfoGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::StorageInfoGet);
  } else if (name == "storage_list") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::StorageList);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildStringPayloadU16(std::string(path.c_str())));
  } else if (name == "storage_stat") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::StorageStat);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildStringPayloadU16(std::string(path.c_str())));
  } else if (name == "storage_format") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::StorageFormat);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::StorageFormat);
  } else if (name == "ota_status") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaStatusGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::OtaStatusGet);
  } else if (name == "ota_manifest") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaManifestGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::OtaManifestGet);
  } else if (name == "ota_manifest_rebuild") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaManifestRebuild);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::OtaManifestRebuild);
  } else if (name == "ota_capacity") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaCapacityGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::OtaCapacityGet);
  } else if (name == "ota_gate") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaGateGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::OtaGateGet);
  } else if (name == "ota_apply") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaApply);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildStringPayloadU16(std::string(ota_target.c_str())));
  } else if (name == "ota_rollback") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaRollback);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::OtaRollback);
  } else if (name == "ota_clear_scope") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaClearScope);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildStringPayloadU16(std::string(path.c_str())));
  } else if (name == "ota_push_start") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaPushStart);
    submitted = submitCommand(cmd_id,
                              espnow_link::management_utils::buildOtaPushStartPayload(std::string(ota_path.c_str()),
                                                                                       ota_chunk));
  } else if (name == "ota_push_status") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaPushStatus);
    submitted = c.otaPushStatus(&req_id, timeout_ms);
  } else if (name == "ota_push_abort") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaPushAbort);
    submitted = c.otaPushAbort(&req_id, timeout_ms);
  } else if (name == "ota_update_start") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaUpdateStart);
    submitted = submitCommand(cmd_id,
                              espnow_link::management_utils::buildOtaPushStartPayload(std::string(ota_path.c_str()),
                                                                                       ota_chunk));
  } else if (name == "ota_master_update_start") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaMasterUpdateStart);
    submitted = c.otaUpdateMasterStart(std::string(ota_path.c_str()), &req_id, timeout_ms);
  } else if (name == "ota_archive_list") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaArchiveList);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildOtaArchivePayload(ota_role, {}, false));
  } else if (name == "ota_archive_save_running") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaArchiveSaveRunning);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildOtaArchivePayload(ota_role, {}, false));
  } else if (name == "ota_archive_save_staged") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaArchiveSaveStaged);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildOtaArchivePayload(ota_role, {}, false));
  } else if (name == "ota_archive_restore") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaArchiveRestore);
    if (ota_id.isEmpty()) {
      sendJson_(400, "{\"ok\":false,\"error\":\"missing_ota_id\"}");
      return;
    }
    submitted = submitCommand(cmd_id,
                              espnow_link::management_utils::buildOtaArchivePayload(ota_role,
                                                                                    std::string(ota_id.c_str()),
                                                                                    false));
  } else if (name == "ota_archive_delete") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaArchiveDelete);
    if (ota_id.isEmpty()) {
      sendJson_(400, "{\"ok\":false,\"error\":\"missing_ota_id\"}");
      return;
    }
    submitted = submitCommand(cmd_id,
                              espnow_link::management_utils::buildOtaArchivePayload(ota_role,
                                                                                    std::string(ota_id.c_str()),
                                                                                    false));
  } else if (name == "ota_archive_clear") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaArchiveClear);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildOtaArchivePayload(ota_role, {}, false));
  } else if (name == "ota_archive_verify") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::OtaArchiveVerify);
    if (ota_id.isEmpty()) {
      sendJson_(400, "{\"ok\":false,\"error\":\"missing_ota_id\"}");
      return;
    }
    submitted = submitCommand(cmd_id,
                              espnow_link::management_utils::buildOtaArchivePayload(ota_role,
                                                                                    std::string(ota_id.c_str()),
                                                                                    false));
  } else if (name == "topology_status") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::TopologyStatusGet);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::TopologyStatusGet);
  } else if (name == "topology_slots_committed") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::TopologySlotsGet);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildTopologySlotsGetPayload(true));
  } else if (name == "topology_slots_staged") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::TopologySlotsGet);
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildTopologySlotsGetPayload(false));
  } else if (name == "topology_commit") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::TopologyCommit);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::TopologyCommit);
  } else if (name == "topology_trigger_send") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::TopologyTriggerSend);
    espnow_link::ManagementTopologyTriggerSendPayload trig{};
    trig.target_index = static_cast<int8_t>(trig_idx);
    trig.direction = trig_dir;
    trig.delay_ms = trig_delay;
    trig.hold_ms = trig_hold;
    trig.source_virtual_index = trig_src_vi;
    submitted = submitCommand(cmd_id, espnow_link::management_utils::buildTopologyTriggerSendPayload(trig));
  } else if (name == "metrics_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::MetricsGet);
    submitted = c.metricsGet(&req_id, timeout_ms);
  } else if (name == "metrics_reset") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::MetricsReset);
    submitted = c.metricsReset(&req_id, timeout_ms);
  } else if (name == "queue_get") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::QueueGet);
    submitted = c.queueGet(&req_id, timeout_ms);
  } else if (name == "commtest_run") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::CommTestRun);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::CommTestRun);
  } else if (name == "commtest_status") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::CommTestStatus);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::CommTestStatus);
  } else if (name == "commtest_report") {
    cmd_id = static_cast<uint16_t>(espnow_link::ManagementCommandId::CommTestReport);
    submitted = submitCommandNoPayload(espnow_link::ManagementCommandId::CommTestReport);
  } else {
    sendJson_(400, "{\"ok\":false,\"error\":\"unknown_command_name\"}");
    return;
  }

  if (!submitted) {
    String json = "{";
    json += "\"ok\":false,";
    json += "\"name\":\"" + jsonEscape_(name) + "\",";
    json += "\"cmd_id\":" + String(static_cast<unsigned int>(cmd_id)) + ",";
    json += "\"error\":\"submit_failed\"";
    json += "}";
    sendJson_(500, json);
    return;
  }

  espnow_link::ManagementFrontendAdapter::OperationStatus st{};
  bool status_ok = false;
  bool wait_ok = false;
  if (wait_terminal) {
    wait_ok = adapter_->operationWait(req_id, st, timeout_ms);
    status_ok = true;
  } else {
    status_ok = adapter_->operationStatus(req_id, st);
  }

  String json = "{";
  json += "\"ok\":true,";
  json += "\"name\":\"" + jsonEscape_(name) + "\",";
  json += "\"cmd_id\":" + String(static_cast<unsigned int>(cmd_id)) + ",";
  json += "\"req_id\":" + String(static_cast<unsigned long>(req_id)) + ",";
  json += "\"op_id\":" + String(static_cast<unsigned long>(req_id)) + ",";
  json += "\"wait\":" + String(wait_terminal ? "true" : "false") + ",";
  json += "\"wait_ok\":" + String(wait_ok ? "true" : "false") + ",";
  json += "\"target_used\":" + String(use_target ? "true" : "false");
  if (use_target) {
    json += ",\"target\":\"" + macToString_(target) + "\"";
  }

  if (status_ok) {
    json += ",\"state\":\"" + String(opStateToString_(st.state)) + "\"";
    json += ",\"terminal\":" + String(st.terminal ? "true" : "false");
    json += ",\"status\":\"" + String(statusToString_(st.status_code)) + "\"";
    json += ",\"stage\":\"" + jsonEscape_(String(st.stage)) + "\"";
    json += ",\"message\":\"" + jsonEscape_(String(st.message.c_str())) + "\"";
    json += ",\"result_payload_hex\":\"" + payloadToHex(st.result_payload) + "\"";
    json += ",\"result_payload_text\":\"" + jsonEscape_(payloadToText(st.result_payload)) + "\"";

    if (st.has_response) {
      bool enabled = false;
      if (espnow_link::ManagementFrontendAdapter::decodeCliControlResponse(st.last_response, enabled)) {
        json += ",\"cli_enabled\":" + String(enabled ? "true" : "false");
      }
      if (espnow_link::ManagementFrontendAdapter::decodeChainLoopControlResponse(st.last_response, enabled)) {
        json += ",\"chainloop_enabled\":" + String(enabled ? "true" : "false");
      }

      bool log_available = false;
      bool log_enabled = false;
      uint8_t log_min = 0U;
      espnow_link::LogStorageStats log_stats{};
      if (espnow_link::ManagementFrontendAdapter::decodeLogStatusResponse(
              st.last_response, log_available, log_enabled, log_min, log_stats)) {
        json += ",\"log_available\":" + String(log_available ? "true" : "false");
        json += ",\"log_enabled\":" + String(log_enabled ? "true" : "false");
        json += ",\"log_min_level\":" + String(static_cast<unsigned int>(log_min));
        json += ",\"log_bytes_used\":" + String(static_cast<unsigned long>(log_stats.bytes_used));
        json += ",\"log_bytes_dropped\":" + String(static_cast<unsigned long>(log_stats.bytes_dropped));
        json += ",\"log_records_appended\":" + String(static_cast<unsigned long>(log_stats.records_appended));
        json += ",\"log_rotations\":" + String(static_cast<unsigned long>(log_stats.rotations));
      }

      uint32_t log_off = 0U;
      uint32_t log_total = 0U;
      std::vector<uint8_t> log_chunk{};
      if (espnow_link::ManagementFrontendAdapter::decodeLogReadResponse(st.last_response, log_off, log_total, log_chunk)) {
        json += ",\"log_chunk_offset\":" + String(static_cast<unsigned long>(log_off));
        json += ",\"log_chunk_total\":" + String(static_cast<unsigned long>(log_total));
        json += ",\"log_chunk_size\":" + String(static_cast<unsigned int>(log_chunk.size()));
      }

      espnow_link::ManagementRuntimeChannelStatusPayload ch{};
      if (espnow_link::ManagementFrontendAdapter::decodeChannelRuntimeStatusResponse(st.last_response, ch)) {
        json += ",\"runtime_channel\":" + String(static_cast<unsigned int>(ch.current_channel));
        json += ",\"runtime_entries\":" + String(static_cast<unsigned int>(ch.entries.size()));
      }

      espnow_link::DescriptorResponse d{};
      if (espnow_link::ManagementFrontendAdapter::decodeStorageInfoResponse(st.last_response, d) ||
          espnow_link::ManagementFrontendAdapter::decodeStorageListResponse(st.last_response, d) ||
          espnow_link::ManagementFrontendAdapter::decodeStorageStatResponse(st.last_response, d)) {
        json += ",\"descriptor_message\":\"" + jsonEscape_(String(d.message.c_str())) + "\"";
      }

      espnow_link::ManagementTopologyStatusPayload topo_status{};
      if (espnow_link::ManagementFrontendAdapter::decodeTopologyStatusResponse(st.last_response, topo_status)) {
        json += ",\"topology_has_staged\":" + String(topo_status.has_staged ? "true" : "false");
        json += ",\"topology_has_committed\":" + String(topo_status.has_committed ? "true" : "false");
        json += ",\"topology_staged_version\":" + String(static_cast<unsigned long>(topo_status.staged_version));
        json += ",\"topology_committed_version\":" + String(static_cast<unsigned long>(topo_status.committed_version));
      }

      uint8_t slot_state = 0U;
      std::vector<espnow_link::ManagementTopologySlotPayload> topo_slots{};
      if (espnow_link::ManagementFrontendAdapter::decodeTopologySlotsResponse(st.last_response, slot_state, topo_slots)) {
        json += ",\"topology_slot_state\":" + String(static_cast<unsigned int>(slot_state));
        json += ",\"topology_slot_count\":" + String(static_cast<unsigned int>(topo_slots.size()));
      }

      espnow_link::ManagementTopologyTriggerSendResponsePayload trig_rsp{};
      if (espnow_link::ManagementFrontendAdapter::decodeTopologyTriggerSendResponse(st.last_response, trig_rsp)) {
        json += ",\"topology_trigger_seq\":" + String(static_cast<unsigned int>(trig_rsp.seq));
      }

      espnow_link::ManagementOtaPushStatusPayload ota_push_status{};
      if (espnow_link::ManagementFrontendAdapter::decodeOtaPushStatusResponse(st.last_response, ota_push_status)) {
        json += ",\"ota_push_active\":" + String(ota_push_status.active ? "true" : "false");
        json += ",\"ota_push_phase\":" + String(static_cast<unsigned int>(ota_push_status.phase));
        json += ",\"ota_push_next_offset\":" + String(static_cast<unsigned long>(ota_push_status.next_offset));
        json += ",\"ota_push_total_size\":" + String(static_cast<unsigned long>(ota_push_status.total_size));
      }

      espnow_link::ManagementOtaPushStartPayload ota_push_start{};
      if (espnow_link::ManagementFrontendAdapter::decodeOtaPushStartResponse(st.last_response, ota_push_start)) {
        json += ",\"ota_start_req_id\":" + String(static_cast<unsigned long>(ota_push_start.req_id));
        json += ",\"ota_start_total_size\":" + String(static_cast<unsigned long>(ota_push_start.total_size));
        json += ",\"ota_start_chunk_bytes\":" + String(static_cast<unsigned int>(ota_push_start.chunk_bytes));
      }

      std::string archive_msg{};
      if (espnow_link::ManagementFrontendAdapter::decodeOtaArchiveResponse(st.last_response, archive_msg)) {
        json += ",\"ota_archive_msg\":\"" + jsonEscape_(String(archive_msg.c_str())) + "\"";
      }
    }

    if (st.has_event) {
      espnow_link::ManagementChannelSyncAllResultPayload sync{};
      if (espnow_link::ManagementFrontendAdapter::decodeChannelSyncAllEvent(st.last_event, sync)) {
        json += ",\"channel_sync_channel\":" + String(static_cast<unsigned int>(sync.channel));
        json += ",\"channel_sync_acked\":" + String(static_cast<unsigned int>(sync.acked_peers));
        json += ",\"channel_sync_total\":" + String(static_cast<unsigned int>(sync.total_peers));
      }

      espnow_link::ManagementChainLoopResultPayload chain{};
      if (espnow_link::ManagementFrontendAdapter::decodeChainLoopEvent(st.last_event, chain)) {
        json += ",\"chainloop_enabled_event\":" + String(chain.enabled ? "true" : "false");
        json += ",\"chainloop_acked\":" + String(static_cast<unsigned int>(chain.acked_peers));
        json += ",\"chainloop_total\":" + String(static_cast<unsigned int>(chain.total_peers));
      }

      espnow_link::ManagementOtaPushResultPayload ota_push_result{};
      if (espnow_link::ManagementFrontendAdapter::decodeOtaPushResultEvent(st.last_event, ota_push_result)) {
        json += ",\"ota_push_result_req\":" + String(static_cast<unsigned long>(ota_push_result.req_id));
        json += ",\"ota_push_result_status\":" + String(static_cast<unsigned int>(ota_push_result.ota_status_code));
      }

      espnow_link::ManagementOtaUpdateResultPayload ota_update_result{};
      if (espnow_link::ManagementFrontendAdapter::decodeOtaUpdateResultEvent(st.last_event, ota_update_result)) {
        json += ",\"ota_update_phase\":" + String(static_cast<unsigned int>(ota_update_result.phase));
        json += ",\"ota_update_status\":" + String(static_cast<unsigned int>(ota_update_result.ota_status_code));
      }

      espnow_link::ManagementTopologyTriggerEventPayload topo_event{};
      if (espnow_link::ManagementFrontendAdapter::decodeTopologyTriggerEvent(st.last_event, topo_event)) {
        json += ",\"topology_trigger_event_state\":" + String(static_cast<unsigned int>(topo_event.state));
        json += ",\"topology_trigger_event_reason\":" + String(static_cast<unsigned int>(topo_event.reason));
        json += ",\"topology_trigger_event_seq\":" + String(static_cast<unsigned int>(topo_event.seq));
      }
    }
  }

  json += "}";
  sendJson_(200, json);
}

bool MasterWifiFrontendUi::parseMacArg_(const char* arg_name, espnow_link::MacAddress& out_mac) {
  if (arg_name == nullptr || arg_name[0] == '\0' || !server_.hasArg(arg_name)) return false;
  String raw = server_.arg(arg_name);
  raw.trim();
  if (raw.isEmpty()) return false;
  return espnow_link::parseMac(raw.c_str(), out_mac);
}

int32_t MasterWifiFrontendUi::parseI32Arg_(const char* arg_name, int32_t default_value) {
  if (arg_name == nullptr || arg_name[0] == '\0' || !server_.hasArg(arg_name)) return default_value;
  String raw = server_.arg(arg_name);
  raw.trim();
  if (raw.isEmpty()) return default_value;
  char* endp = nullptr;
  const long v = std::strtol(raw.c_str(), &endp, 10);
  if (endp == nullptr || *endp != '\0') return default_value;
  return static_cast<int32_t>(v);
}

uint32_t MasterWifiFrontendUi::parseU32Arg_(const char* arg_name, uint32_t default_value) {
  if (arg_name == nullptr || arg_name[0] == '\0' || !server_.hasArg(arg_name)) return default_value;
  String raw = server_.arg(arg_name);
  raw.trim();
  if (raw.isEmpty()) return default_value;
  char* endp = nullptr;
  const unsigned long v = std::strtoul(raw.c_str(), &endp, 10);
  if (endp == nullptr || *endp != '\0') return default_value;
  return static_cast<uint32_t>(v);
}

size_t MasterWifiFrontendUi::parseSizeArg_(const char* arg_name, size_t default_value) {
  return static_cast<size_t>(parseU32Arg_(arg_name, static_cast<uint32_t>(default_value)));
}

String MasterWifiFrontendUi::macToString_(const espnow_link::MacAddress& mac) {
  const std::string text = espnow_link::macToString(mac);
  return String(text.c_str());
}

String MasterWifiFrontendUi::jsonEscape_(const String& in) {
  String out;
  out.reserve(in.length() + 8U);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += c;
    }
  }
  return out;
}

const char* MasterWifiFrontendUi::opStateToString_(espnow_link::ManagementFrontendAdapter::OperationState st) {
  switch (st) {
    case espnow_link::ManagementFrontendAdapter::OperationState::Queued:
      return "queued";
    case espnow_link::ManagementFrontendAdapter::OperationState::Running:
      return "running";
    case espnow_link::ManagementFrontendAdapter::OperationState::Succeeded:
      return "succeeded";
    case espnow_link::ManagementFrontendAdapter::OperationState::Failed:
      return "failed";
    case espnow_link::ManagementFrontendAdapter::OperationState::Timeout:
      return "timeout";
    case espnow_link::ManagementFrontendAdapter::OperationState::Canceled:
      return "canceled";
    default:
      return "unknown";
  }
}

const char* MasterWifiFrontendUi::statusToString_(espnow_link::ManagementStatus st) {
  return espnow_link::management_utils::managementStatusToString(st);
}

bool MasterWifiFrontendUi::submitSimple_(uint16_t cmd_id,
                                         const std::vector<uint8_t>& payload,
                                         espnow_link::ManagementFrontendAdapter::OperationHandle& out_op,
                                         uint32_t timeout_ms) {
  if (adapter_ == nullptr) return false;
  return adapter_->operationSubmit(cmd_id, payload, out_op, timeout_ms);
}

uint8_t MasterWifiFrontendUi::resolveEspNowChannel_() {
  uint8_t fallback = cfg_.espnow_channel;
  if (!cfg_.prefer_runtime_channel || adapter_ == nullptr) {
    return fallback;
  }

  espnow_link::ManagementFrontendAdapter::CommandRunResult run{};
  const bool ok = adapter_->commandRunAndWait(static_cast<uint16_t>(espnow_link::ManagementCommandId::ChannelRuntimeGet),
                                              {},
                                              run,
                                              1200U);
  if (!ok ||
      !run.has_response ||
      run.response.status != espnow_link::ManagementStatus::Ok) {
    return fallback;
  }

  espnow_link::ManagementRuntimeChannelStatusPayload status{};
  if (!espnow_link::ManagementFrontendAdapter::decodeChannelRuntimeStatusResponse(run.response, status)) {
    return fallback;
  }
  if (status.current_channel >= 1U && status.current_channel <= 14U) {
    return status.current_channel;
  }
  return fallback;
}

bool MasterWifiFrontendUi::startApFallback_(const char* reason) {
  const uint8_t ap_channel =
      (espnow_channel_ >= 1U && espnow_channel_ <= 14U) ? espnow_channel_ : static_cast<uint8_t>(1U);
  WiFi.mode(WIFI_AP);
  bool ap_ok = false;
  if (cfg_.ap_password != nullptr && std::strlen(cfg_.ap_password) >= 8U) {
    ap_ok = WiFi.softAP(cfg_.ap_ssid, cfg_.ap_password, ap_channel);
  } else {
    ap_ok = WiFi.softAP(cfg_.ap_ssid, nullptr, ap_channel);
  }
  ap_mode_active_ = ap_ok;
  Serial.printf("[MASTER][WIFI-UI] AP fallback %s reason=%s ssid=%s ip=%s channel=%u\n",
                ap_ok ? "enabled" : "failed",
                reason ? reason : "",
                cfg_.ap_ssid ? cfg_.ap_ssid : "",
                WiFi.softAPIP().toString().c_str(),
                static_cast<unsigned int>(ap_channel));
  return ap_ok;
}

void MasterWifiFrontendUi::sendJson_(int code, const String& json) {
  server_.sendHeader("Cache-Control", "no-store, max-age=0");
  server_.sendHeader("Access-Control-Allow-Origin", "*");
  server_.send(code, "application/json", json);
}

void MasterWifiFrontendUi::tryReconnectWifi_() {
  if (!sta_reconnect_enabled_) return;
  if (cfg_.ssid == nullptr || cfg_.ssid[0] == '\0') return;
  if (WiFi.status() == WL_CONNECTED || WiFi.status() == WL_IDLE_STATUS) {
    if (cfg_.enforce_channel_match &&
        espnow_channel_ >= 1U &&
        espnow_channel_ <= 14U &&
        WiFi.channel() != espnow_channel_) {
      Serial.printf("[MASTER][WIFI-UI] reconnect channel mismatch wifi=%u espnow=%u; stop STA reconnect\n",
                    static_cast<unsigned int>(WiFi.channel()),
                    static_cast<unsigned int>(espnow_channel_));
      WiFi.disconnect(false, false);
      sta_reconnect_enabled_ = false;
      if (cfg_.fallback_to_ap && !ap_mode_active_) {
        (void)startApFallback_("reconnect_channel_mismatch");
      }
    }
    return;
  }
  const uint32_t now = static_cast<uint32_t>(millis());
  if (now - last_reconnect_attempt_ms_ < cfg_.reconnect_interval_ms) return;
  last_reconnect_attempt_ms_ = now;
  Serial.printf("[MASTER][WIFI-UI] reconnecting to %s\n", cfg_.ssid);
  WiFi.disconnect(false, false);
  WiFi.begin(cfg_.ssid, cfg_.password);
}
