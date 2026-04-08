#pragma once

#include <pgmspace.h>

static const char PSTATION_WEB_UI[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PowerStation</title>
<style>
:root{--bg:#f0f2f5;--surface:#fff;--surface2:#f7f9fb;--border:#e2e6ec;--text:#1a2233;--muted:#6b7a8d;--primary:#1a73e8;--primary-soft:#e8f0fe;--success:#0d9f4f;--success-soft:#e6f9ed;--warning:#e8a500;--warning-soft:#fef6e0;--danger:#d93025;--danger-soft:#fce8e6;--radius:12px;--shadow:0 1px 3px rgba(0,0,0,.08);--mono:'Cascadia Mono','Consolas','Courier New',monospace;--font:'Segoe UI Variable','Segoe UI',system-ui,sans-serif}
*{margin:0;padding:0;box-sizing:border-box}body{font-family:var(--font);background:var(--bg);color:var(--text);font-size:14px;line-height:1.5}

/* STATUS BAR */
.sbar{background:#1a2233;color:#fff;padding:6px 16px;display:flex;align-items:center;gap:12px;font-size:13px;font-family:var(--mono);overflow-x:auto;white-space:nowrap}
.sbar .dot{width:8px;height:8px;border-radius:50%;background:#555;flex-shrink:0}.sbar .dot.on{background:var(--success)}.sbar .dot.warn{background:var(--warning)}
.sbar .sv{color:#8899aa}.sbar .sv b{color:#fff;margin-left:2px}.sbar .sep{color:#334;margin:0 2px}
.sbar .sv.green b{color:#4ade80}.sbar .sv.amber b{color:#fbbf24}.sbar .sv.red b{color:#f87171}

/* TABS */
.tabs{background:var(--surface);border-bottom:1px solid var(--border);display:flex;gap:0;overflow-x:auto;padding:0 8px}
.tab{border:none;background:none;color:var(--muted);padding:12px 16px;font-size:13px;font-weight:600;cursor:pointer;border-bottom:2px solid transparent;white-space:nowrap;font-family:var(--font)}
.tab:hover{color:var(--text)}.tab.active{color:var(--primary);border-bottom-color:var(--primary)}

/* LAYOUT */
.page{display:none;padding:12px;max-width:960px;margin:0 auto}.page.active{display:block}
.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:12px;box-shadow:var(--shadow)}
.card h3{font-size:15px;margin-bottom:12px;display:flex;align-items:center;gap:8px}
.card h3 svg{width:18px;height:18px;color:var(--primary)}
.grid2{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}
.grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
.grid4{display:grid;grid-template-columns:repeat(2,1fr);gap:10px}
@media(min-width:640px){.grid4{grid-template-columns:repeat(4,1fr)}}

/* METRIC TILES */
.mt{padding:12px;border-radius:10px;background:var(--surface2);border:1px solid var(--border)}.mt label{font-size:11px;color:var(--muted);display:block;margin-bottom:2px;text-transform:uppercase;letter-spacing:.5px}.mt .mv{font-size:18px;font-weight:700}

/* BATTERY VISUAL */
.bat-box{display:flex;align-items:center;justify-content:center;gap:20px;padding:20px 0}
.bat-shell{position:relative;width:180px;height:80px;border:3px solid #444;border-radius:8px;background:#1a1a2e;overflow:hidden}
.bat-tip{position:absolute;right:-10px;top:50%;transform:translateY(-50%);width:8px;height:28px;background:#444;border-radius:0 4px 4px 0}
.bat-fill{position:absolute;left:3px;top:3px;bottom:3px;border-radius:4px;transition:width .6s,background .6s;min-width:0}
.bat-pct{position:absolute;left:0;top:0;right:0;bottom:0;display:flex;align-items:center;justify-content:center;font-size:28px;font-weight:800;color:#fff;text-shadow:0 1px 4px rgba(0,0,0,.6);font-family:var(--mono)}
.bat-info{text-align:left}.bat-info .bi-row{font-size:14px;color:var(--muted);margin-bottom:6px}.bat-info .bi-row b{color:var(--text);font-size:16px}

/* BAR GAUGES */
.bar-card{display:flex;flex-direction:column;gap:6px}
.bar-caption{font-size:11px;color:var(--muted);text-transform:uppercase;font-weight:700;letter-spacing:.5px}
.bar-value{font-size:22px;font-weight:800;color:var(--text);line-height:1}
.bar-shell{position:relative;height:18px;border-radius:10px;background:#e6e9f0;overflow:hidden}
.bar-fill{height:100%;width:0;border-radius:10px;min-width:4px;transition:width .25s ease}
.bar-ticks{position:absolute;inset:0;display:flex;justify-content:space-between;align-items:center;padding:0 4px;pointer-events:none}
.bar-ticks span{width:2px;height:10px;background:#c4cad4;border-radius:2px}

/* CHIPS */
.chip{display:inline-flex;align-items:center;gap:6px;padding:6px 12px;border-radius:999px;font-size:12px;font-weight:600;border:1px solid var(--border);background:var(--surface2)}
.chip.green{background:var(--success-soft);color:var(--success);border-color:#b7e4c7}
.chip.red{background:var(--danger-soft);color:var(--danger);border-color:#f5c6c6}
.chip.amber{background:var(--warning-soft);color:#92650b;border-color:#f1dfb2}

/* TOGGLE SWITCHES */
.sw-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:12px}
.sw-card{padding:16px;border-radius:var(--radius);border:1px solid var(--border);background:var(--surface);display:flex;align-items:center;justify-content:space-between;gap:16px}
.sw-left{display:flex;align-items:center;gap:12px}
.sw-icon{width:36px;height:36px;border-radius:10px;display:flex;align-items:center;justify-content:center;background:var(--surface2);border:1px solid var(--border);color:var(--muted)}
.sw-icon.on{background:var(--success-soft);border-color:#b7e4c7;color:var(--success)}
.sw-label{font-weight:600;font-size:14px}.sw-state{font-size:12px;color:var(--muted)}
.sw-state.on{color:var(--success)}.sw-state.off{color:var(--muted)}
.toggle{position:relative;width:52px;height:28px;cursor:pointer;flex-shrink:0}
.toggle input{opacity:0;width:0;height:0}
.toggle .slider{position:absolute;inset:0;background:#ccc;border-radius:28px;transition:.25s}
.toggle .slider:before{content:'';position:absolute;width:22px;height:22px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.25s;box-shadow:0 1px 3px rgba(0,0,0,.2)}
.toggle input:checked+.slider{background:var(--success)}
.toggle input:checked+.slider:before{transform:translateX(24px)}
.toggle input:disabled+.slider{opacity:.4;cursor:not-allowed}

/* INDICATORS */
.ind-grid{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:12px}
.ind{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted)}.ind .idot{width:10px;height:10px;border-radius:50%;border:2px solid var(--border);background:#ccc}
.ind .idot.on{background:var(--success);border-color:var(--success)}.ind .idot.off{background:#ccc}.ind .idot.warn{background:var(--warning);border-color:var(--warning)}

/* SHUTDOWN */
.shut-zone{margin-top:16px;padding:16px;border:2px dashed var(--danger);border-radius:var(--radius);text-align:center}
.shut-btn{background:var(--danger);color:#fff;border:none;padding:12px 32px;border-radius:10px;font-size:15px;font-weight:700;cursor:pointer;display:inline-flex;align-items:center;gap:8px}
.shut-btn:hover{filter:brightness(.9)}

/* EVENTS */
.ev-group{margin-bottom:16px}.ev-group-head{display:flex;align-items:center;gap:8px;padding:8px 0;cursor:pointer;user-select:none}
.ev-badge{padding:3px 10px;border-radius:999px;font-size:12px;font-weight:700;font-family:var(--mono)}
.ev-badge.boot{background:var(--primary-soft);color:var(--primary)}.ev-badge.charge{background:var(--success-soft);color:var(--success)}
.ev-badge.discharge{background:var(--warning-soft);color:#92650b}.ev-badge.alarm{background:var(--danger-soft);color:var(--danger)}
.ev-badge.other{background:var(--surface2);color:var(--muted)}
.ev-count{font-size:12px;color:var(--muted)}.ev-items{display:block}
.ev-table{width:100%;border-collapse:collapse;font-size:13px}
.ev-table th,.ev-table td{border:1px solid var(--border);padding:8px 10px;text-align:left}
.ev-table th{background:var(--surface2);color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.5px}
.ev-table tbody tr:nth-child(even){background:#f9fbfd}
.ev-time{color:var(--muted);font-family:var(--mono);font-size:12px;white-space:nowrap}
.ev-detail{display:flex;flex-wrap:wrap;gap:6px 12px}
.ev-detail span{color:var(--muted)}.ev-detail b{color:var(--text)}
.alarm-tag{display:inline-block;padding:1px 6px;border-radius:999px;font-size:10px;font-family:var(--mono);background:var(--danger-soft);color:var(--danger);border:1px solid #f5c6c6}
.ev-actions{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:12px}
.ev-pg{display:flex;gap:6px;align-items:center}

/* BUTTONS */
.btn{display:inline-flex;align-items:center;gap:6px;border:1px solid var(--border);background:var(--surface);color:var(--text);border-radius:8px;padding:8px 14px;font-size:13px;font-weight:600;cursor:pointer;text-decoration:none;font-family:var(--font)}
.btn:hover{background:var(--surface2)}.btn.primary{background:var(--primary);color:#fff;border-color:var(--primary)}.btn.danger{background:var(--danger-soft);color:var(--danger);border-color:#f5c6c6}
.btn.sm{padding:5px 10px;font-size:12px}.btn:disabled{opacity:.5;cursor:not-allowed}

/* FORMS */
.form-grid{display:grid;gap:12px}@media(min-width:640px){.form-grid{grid-template-columns:1fr 1fr}}
.field label{display:block;font-size:12px;color:var(--muted);margin-bottom:4px}.field input,.field select{width:100%;border:1px solid var(--border);border-radius:8px;padding:10px 12px;font-size:14px;background:#fff;color:var(--text)}
.field input:focus,.field select:focus{outline:none;border-color:var(--primary);box-shadow:0 0 0 3px rgba(26,115,232,.12)}
.note{padding:10px;border-radius:8px;background:var(--surface2);border:1px solid var(--border);color:var(--muted);font-size:13px}
.upload-bar{height:8px;border-radius:999px;background:#e4ebf3;overflow:hidden;margin:8px 0}.upload-fill{height:100%;background:var(--primary);border-radius:inherit;transition:width .3s;width:0}

/* TERMINAL */
.term{background:#0d1117;border-radius:var(--radius);overflow:hidden;border:1px solid #30363d;font-family:var(--mono);font-size:13px}
.term-out{height:320px;overflow-y:auto;padding:12px;color:#c9d1d9;white-space:pre-wrap;word-break:break-all;line-height:1.6}
.term-out .t-cmd{color:#58a6ff}.term-out .t-ok{color:#3fb950}.term-out .t-err{color:#f85149}.term-out .t-info{color:#8b949e}
.term-in{display:flex;border-top:1px solid #30363d;background:#161b22}.term-in span{padding:10px 12px;color:#58a6ff;font-weight:700}
.term-in input{flex:1;background:none;border:none;color:#c9d1d9;padding:10px 8px;font-family:var(--mono);font-size:13px;outline:none}

/* DIAGNOSTICS */
.diag-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:8px}
.dg-item{padding:10px;text-align:center;border-radius:8px;background:var(--surface2);border:1px solid var(--border)}.dg-item label{font-size:11px;color:var(--muted);display:block}.dg-item b{font-size:16px}
.json-box{margin-top:8px;padding:10px;border-radius:8px;border:1px solid var(--border);background:#fbfcfd;font-family:var(--mono);font-size:12px;max-height:240px;overflow:auto;white-space:pre-wrap}

/* TOAST */
.toast-area{position:fixed;bottom:16px;right:16px;z-index:999;display:grid;gap:8px}
.toast{padding:12px 16px;border-radius:10px;background:var(--surface);border:1px solid var(--border);box-shadow:0 4px 12px rgba(0,0,0,.12);font-size:13px;min-width:220px;animation:fadeIn .2s}
.toast.success{border-left:4px solid var(--success)}.toast.danger{border-left:4px solid var(--danger)}.toast.warn{border-left:4px solid var(--warning)}
.toast strong{display:block;margin-bottom:2px}
@keyframes fadeIn{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}
</style></head><body>

<!-- STATUS BAR -->
<div class="sbar">
  <div class="dot" id="s-dot"></div>
  <span class="sv green"><span>SOC</span> <b id="s-soc">--%</b></span><span class="sep">|</span>
  <span class="sv"><span>V</span> <b id="s-volt">--</b></span><span class="sep">|</span>
  <span class="sv"><span>A</span> <b id="s-amp">--</b></span><span class="sep">|</span>
  <span class="sv amber"><span>W</span> <b id="s-pwr">--</b></span><span class="sep">|</span>
  <span class="sv"><span>BAT</span> <b id="s-tbat">--</b></span><span class="sep">|</span>
  <span class="sv"><span>WiFi</span> <b id="s-wifi">--</b></span><span class="sep">|</span>
  <span class="sv" id="s-ntp-wrap"><span>NTP</span> <b id="s-ntp">--</b></span>
</div>

<!-- NAVIGATION TABS -->
<nav class="tabs">
  <button class="tab active" data-p="dashboard">Dashboard</button>
  <button class="tab" data-p="control">Control</button>
  <button class="tab" data-p="events">Events</button>
  <button class="tab" data-p="network">Network</button>
  <button class="tab" data-p="settings">Settings</button>
  <button class="tab" data-p="system">System</button>
</nav>

<!-- ═══ DASHBOARD ═══ -->
<div class="page active" id="p-dashboard">
  <div class="card">
    <div class="bat-box">
      <div style="position:relative">
        <div class="bat-shell"><div class="bat-fill" id="bat-fill"></div><div class="bat-pct" id="bat-pct">--%</div><div class="bat-tip"></div></div>
      </div>
      <div class="bat-info">
        <div class="bi-row"><b id="bi-volt">--</b> V</div>
        <div class="bi-row"><b id="bi-curr">--</b> A</div>
        <div class="bi-row"><b id="bi-pwr">--</b> W</div>
        <div class="bi-row"><b id="bi-wh">--</b> Wh avail</div>
      </div>
    </div>
  </div>
  <div class="grid4">
    <div class="mt"><label>Voltage</label><div class="mv" data-bind="voltage">--</div></div>
    <div class="mt"><label>Current</label><div class="mv" data-bind="current">--</div></div>
    <div class="mt"><label>Power</label><div class="mv" data-bind="power">--</div></div>
    <div class="mt"><label>Runtime</label><div class="mv" data-bind="runtime">--</div></div>
    <div class="mt"><label>Bat Temp</label><div class="mv" data-bind="temp-bat">--</div></div>
    <div class="mt"><label>Sys Temp</label><div class="mv" data-bind="temp-sys">--</div></div>
    <div class="mt"><label>Available</label><div class="mv" data-bind="available-wh">--</div></div>
    <div class="mt"><label>State</label><div class="mv" data-bind="charging-state">--</div></div>
  </div>
  <div class="grid2" style="margin-top:12px">
    <div class="card bar-card">
      <div class="bar-caption">State of Charge</div>
      <div class="bar-shell"><div class="bar-fill" id="bar-soc"></div><div class="bar-ticks"><span></span><span></span><span></span></div></div>
      <div class="bar-value" id="bar-soc-val">--%</div>
    </div>
    <div class="card bar-card">
      <div class="bar-caption">Power</div>
      <div class="bar-shell"><div class="bar-fill" id="bar-power"></div><div class="bar-ticks"><span></span><span></span><span></span></div></div>
      <div class="bar-value" id="bar-power-val">--</div>
    </div>
  </div>
  <div class="card">
    <h3>Lifetime Stats</h3>
    <div class="grid3">
      <div class="mt"><label>Cycles (EFC)</label><div class="mv" data-bind="efc-total">--</div></div>
      <div class="mt"><label>SOH</label><div class="mv" data-bind="soh-last">--</div></div>
      <div class="mt"><label>Energy In</label><div class="mv" data-bind="energy-in">--</div></div>
      <div class="mt"><label>Energy Out</label><div class="mv" data-bind="energy-out">--</div></div>
      <div class="mt"><label>Peak Current</label><div class="mv" data-bind="peak-current">--</div></div>
      <div class="mt"><label>Peak Power</label><div class="mv" data-bind="peak-power">--</div></div>
    </div>
  </div>
  <div class="card" style="display:flex;gap:8px;flex-wrap:wrap">
    <a class="btn sm" href="/export/stats.json">Export JSON</a>
    <a class="btn sm" href="/export/stats.csv">Export CSV</a>
  </div>
</div>

<!-- ═══ CONTROL ═══ -->
<div class="page" id="p-control">
  <div class="card">
    <h3><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M12 1v4m0 14v4m-8.66-3.34 2.83-2.83m11.66-5.66 2.83-2.83M1 12h4m14 0h4M4.34 4.34l2.83 2.83m11.66 5.66 2.83 2.83"/></svg>System Indicators</h3>
    <div class="ind-grid" id="indicators">
      <div class="ind"><div class="idot" id="ind-link"></div><span>ESP Link</span></div>
      <div class="ind"><div class="idot" id="ind-charge"></div><span>Charging</span></div>
      <div class="ind"><div class="idot" id="ind-safe"></div><span>Safe Mode</span></div>
    </div>
  </div>
  <div class="card">
    <h3><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 12h14M12 5l7 7-7 7"/></svg>Port Control</h3>
    <div class="sw-grid" id="port-switches"></div>
  </div>
  <div class="card">
    <h3>Bridge Info</h3>
    <div class="grid2">
      <div class="mt"><label>RP2040 Mode</label><div class="mv" data-bind="mode">--</div></div>
      <div class="mt"><label>WiFi Mode</label><div class="mv" data-bind="wifi-mode">--</div></div>
      <div class="mt"><label>Hostname</label><div class="mv" data-bind="hostname">--</div></div>
      <div class="mt"><label>AP IP</label><div class="mv" data-bind="ap-ip">--</div></div>
      <div class="mt"><label>STA IP</label><div class="mv" data-bind="sta-ip">--</div></div>
      <div class="mt"><label>Status</label><div class="mv" data-bind="status">--</div></div>
      <div class="mt"><label>Time Sync</label><div class="mv" data-bind="ntp-status">--</div></div>
    </div>
  </div>
  <div class="shut-zone">
    <button class="shut-btn" id="shutdown-btn">
      <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M12 2v10"/><path d="M18.36 6.64A9 9 0 1 1 5.64 6.64"/></svg>
      SHUTDOWN SYSTEM
    </button>
    <div style="font-size:12px;color:var(--muted);margin-top:8px">Long-press confirmation required</div>
  </div>
</div>

<!-- ═══ EVENTS ═══ -->
<div class="page" id="p-events">
  <div class="card">
    <h3><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/></svg>System Statistics</h3>
    <div class="grid4" id="ev-stats">
      <div class="mt"><label>Boot Count</label><div class="mv" id="st-boots">--</div></div>
      <div class="mt"><label>Charge Cycles</label><div class="mv" id="st-cycles">--</div></div>
      <div class="mt"><label>Runtime</label><div class="mv" id="st-runtime">--</div></div>
      <div class="mt"><label>SOH</label><div class="mv" id="st-soh">--</div></div>
    </div>
  </div>
  <div class="card">
    <h3>Energy</h3>
    <div class="grid4">
      <div class="mt"><label>Total In</label><div class="mv" id="st-ein">--</div></div>
      <div class="mt"><label>Total Out</label><div class="mv" id="st-eout">--</div></div>
      <div class="mt"><label>Efficiency</label><div class="mv" id="st-eff">--</div></div>
      <div class="mt"><label>Avg per Cycle</label><div class="mv" id="st-avg-cycle">--</div></div>
    </div>
  </div>
  <div class="card">
    <h3>Load Profile</h3>
    <div class="grid4">
      <div class="mt"><label>Peak Current</label><div class="mv" id="st-peak-a">--</div></div>
      <div class="mt"><label>Peak Power</label><div class="mv" id="st-peak-w">--</div></div>
      <div class="mt"><label>Avg Temperature</label><div class="mv" id="st-tavg">--</div></div>
      <div class="mt"><label>Temp Range</label><div class="mv" id="st-trange">--</div></div>
    </div>
  </div>
  <div class="card">
    <h3>Fault History</h3>
    <div class="grid3">
      <div class="mt"><label>Alarm Events</label><div class="mv" id="st-alarms">--</div></div>
      <div class="mt"><label>OCP Events</label><div class="mv" id="st-ocp">--</div></div>
      <div class="mt"><label>Temp Events</label><div class="mv" id="st-temp-ev">--</div></div>
    </div>
  </div>
  <div class="card">
    <h3>Event Log</h3>
    <div class="ev-actions">
      <div class="ev-pg">
        <button class="btn sm" id="ev-prev" disabled>&larr;</button>
        <span id="ev-page-info" style="font-size:12px;color:var(--muted)">--</span>
        <button class="btn sm" id="ev-next" disabled>&rarr;</button>
      </div>
      <button class="btn sm primary" id="ev-refresh">Refresh</button>
      <a class="btn sm" href="/export/log.json">JSON</a>
      <a class="btn sm" href="/export/log.csv">CSV</a>
      <button class="btn sm danger" id="ev-clear">Clear</button>
    </div>
    <div id="ev-list" style="margin-top:8px"></div>
  </div>
</div>

<!-- ═══ NETWORK ═══ -->
<div class="page" id="p-network">
  <div class="card">
    <h3>WiFi Connection</h3>
    <div class="grid4" style="margin-bottom:12px">
      <div class="mt"><label>Mode</label><div class="mv" data-bind="wifi-mode">--</div></div>
      <div class="mt"><label>STA IP</label><div class="mv" data-bind="sta-ip">--</div></div>
      <div class="mt"><label>AP IP</label><div class="mv" data-bind="ap-ip">--</div></div>
      <div class="mt"><label>Clock</label><div class="mv" id="ntp-clock">--</div></div>
    </div>
    <form id="net-form">
      <div class="form-grid">
        <div class="field"><label>WiFi Mode</label><select name="wifi_mode"><option value="ap">AP</option><option value="sta">STA</option><option value="ap_sta">AP+STA</option></select></div>
        <div class="field"><label>Hostname</label><input name="hostname"></div>
        <div class="field"><label>AP SSID</label><input name="ap_ssid"></div>
        <div class="field"><label>AP Password</label><input name="ap_password" type="password" placeholder="unchanged"></div>
        <div class="field"><label>STA SSID</label><input name="sta_ssid"></div>
        <div class="field"><label>STA Password</label><input name="sta_password" type="password" placeholder="unchanged"></div>
        <div class="field"><label>OTA Password</label><input name="ota_password" type="password" placeholder="unchanged"></div>
        <div class="field"><label>Admin Password</label><input name="admin_password" type="password" placeholder="unchanged (empty=no auth)"></div>
      </div>
      <div style="margin-top:12px;display:flex;gap:8px;align-items:center"><button class="btn primary" type="submit">Save</button><button class="btn" type="button" id="net-reload">Reload</button><span class="note" id="net-note" style="flex:1"></span></div>
    </form>
  </div>
  <div class="card">
    <h3>Time Sync (NTP)</h3>
    <form id="ntp-form">
      <div class="form-grid">
        <div class="field"><label>NTP Enabled</label><select name="ntp_enabled"><option value="true">Yes</option><option value="false">No</option></select></div>
        <div class="field"><label>NTP Server</label><input name="ntp_server" value="pool.ntp.org"></div>
        <div class="field"><label>UTC Offset (sec)</label><input name="tz_offset" type="number" value="7200"></div>
      </div>
      <div style="margin-top:12px;display:flex;gap:8px;align-items:center"><button class="btn primary" type="submit">Save</button><span class="note" id="ntp-note"></span></div>
    </form>
  </div>
</div>

<!-- ═══ SETTINGS ═══ -->
<div class="page" id="p-settings">
  <div class="card">
    <h3>RP2040 Settings</h3>
    <form id="rp-form">
      <div class="form-grid">
        <div class="field"><label>Capacity (Ah)</label><input name="capacity_ah" type="number" step="0.1"></div>
        <div class="field"><label>Vbat Warn (V)</label><input name="vbat_warn_v" type="number" step="0.01"></div>
        <div class="field"><label>Vbat Cut (V)</label><input name="vbat_cut_v" type="number" step="0.01"></div>
        <div class="field"><label>Cell Warn (V)</label><input name="cell_warn_v" type="number" step="0.001"></div>
        <div class="field"><label>Cell Cut (V)</label><input name="cell_cut_v" type="number" step="0.001"></div>
        <div class="field"><label>Temp Warn (°C)</label><input name="temp_bat_warn_c" type="number" step="1"></div>
        <div class="field"><label>Temp Cut (°C)</label><input name="temp_bat_cut_c" type="number" step="1"></div>
        <div class="field"><label>Sound</label><select name="sound"><option value="full">Full</option><option value="minimal">Minimal</option><option value="silent">Silent</option></select></div>
      </div>
      <div style="margin-top:12px;display:flex;gap:8px;align-items:center"><button class="btn primary" type="submit">Apply</button><button class="btn" type="button" id="rp-reload">Reload</button><span class="note" id="rp-note" style="flex:1"></span></div>
    </form>
  </div>
</div>

<!-- ═══ SYSTEM ═══ -->
<div class="page" id="p-system">
  <div class="card">
    <h3><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>ESP32 Firmware Update</h3>
    <form id="ota-form">
      <div class="field"><label>Firmware binary (.bin)</label><input type="file" id="ota-file" accept=".bin"></div>
      <div class="upload-bar"><div class="upload-fill" id="ota-fill"></div></div>
      <div style="display:flex;gap:8px;align-items:center"><button class="btn primary" type="submit">Upload</button><span class="note" id="ota-note"></span></div>
    </form>
  </div>
  <div class="card">
    <h3><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg>CLI Terminal</h3>
    <div class="term">
      <div class="term-out" id="term-out"></div>
      <div class="term-in"><span>&gt;</span><input id="term-cmd" placeholder="Type command... (try HELP)" autocomplete="off" spellcheck="false"></div>
    </div>
  </div>
  <div class="card">
    <h3>Diagnostics</h3>
    <div class="diag-grid">
      <div class="dg-item"><label>Hello</label><b data-bind="hello-counter">0</b></div>
      <div class="dg-item"><label>Telemetry</label><b data-bind="telemetry-counter">0</b></div>
      <div class="dg-item"><label>Stats</label><b data-bind="stats-counter">0</b></div>
      <div class="dg-item"><label>ACK</label><b data-bind="ack-counter">0</b></div>
      <div class="dg-item"><label>Errors</label><b data-bind="error-counter">0</b></div>
    </div>
    <div class="json-box" id="raw-cache" style="margin-top:12px">Loading...</div>
    <div style="margin-top:8px"><button class="btn sm" id="sys-refresh">Refresh Cache</button></div>
  </div>
</div>

<div class="toast-area" id="toasts"></div>

<script>
const PAGE_SIZE=8;
const state={route:'dashboard',telemetry:null,system:null,stats:null,logs:null,ports:null,ntpTimer:null};
let logPage=0;

function $(id){return document.getElementById(id)}
function fixed(v,d,s){return v==null?'--':(Number(v).toFixed(d)+(s||''))}
function clamp(v,lo,hi){return Math.max(lo,Math.min(hi,v))}
function minutesToLabel(m){if(m==null)return'--';const h=Math.floor(m/60),mm=m%60;return h>0?h+'h '+mm+'m':mm+'m'}
function timeLabel(epoch){if(!epoch||epoch<1700000000)return'NO SYNC';const d=new Date(epoch*1000);return d.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});}

function setBind(name,val){document.querySelectorAll('[data-bind="'+name+'"]').forEach(el=>{el.textContent=val})}

function toast(title,msg,tone){const d=document.createElement('div');d.className='toast '+(tone||'');d.innerHTML='<strong>'+title+'</strong>'+msg;$('toasts').appendChild(d);setTimeout(()=>d.remove(),4000)}

async function api(url,opts){if(!opts)opts={};opts.credentials='same-origin';const r=await fetch(url,opts);if(r.status===401){location.reload();throw new Error('Auth required')}if(!r.ok)throw new Error('HTTP '+r.status);return r.json()}

/* ARC GAUGE */
function setBar(id,frac,color,text){
  const fill=$(id);if(!fill)return;
  fill.style.width=(Math.max(0,Math.min(1,frac))*100)+'%';
  fill.style.background=color;
  const v=$(id+'-val');if(v)v.textContent=text;
}
function updateGauges(){
  const t=state.telemetry;if(!t)return;
  const soc=clamp(t.soc_pct||0,0,100);
  const socColor=soc>50?'#22c55e':soc>20?'#eab308':'#ef4444';
  setBar('bar-soc',soc/100,socColor,Math.round(soc)+'%');
  const pw=t.power_w||0,pwMax=200;
  const frac=Math.min(Math.abs(pw)/pwMax,1);
  const pwColor=pw>140?'#ef4444':pw>70?'#eab308':'#3b82f6';
  const pwLabel=(t.charging?'+':'')+pw.toFixed(0)+'W';
  setBar('bar-power',frac,pwColor,pwLabel);
}

/* STATUS BAR */
function updateStatusBar(t,s){
  if(t){$('s-soc').textContent=fixed(t.soc_pct,0,'%');$('s-volt').textContent=fixed(t.voltage_v,1,'V');$('s-amp').textContent=fixed(t.current_a,1,'A');$('s-pwr').textContent=fixed(t.power_w,0,'W');$('s-tbat').textContent=fixed(t.temp_bat_c,0,'°C');
    const soc=t.soc_pct||0;$('s-soc').parentElement.className='sv '+(soc>50?'green':soc>20?'amber':'red');$('s-pwr').parentElement.className='sv '+(t.power_w>50?'amber':'');
  }
  if(s){
    $('s-wifi').textContent=s.wifi_mode||'--';const on=!!s.link_up;$('s-dot').className='dot'+(on?' on':' warn');
    const ntpWrap=$('s-ntp-wrap');const ntpText=(s.ntp_enabled===false)?'OFF':(s.ntp_synced?timeLabel(s.ntp_epoch):'NO SYNC');
    $('s-ntp').textContent=ntpText;ntpWrap.className='sv '+(s.ntp_synced?'green':'red');
  }
}

/* BATTERY VISUAL */
function renderBattery(t){if(!t)return;
  const soc=clamp(t.soc_pct||0,0,100),fill=$(soc>50?'bat-fill':'bat-fill');
  $('bat-fill').style.width=(soc*0.96)+'%';
  $('bat-fill').style.background=soc>50?'linear-gradient(90deg,#22c55e,#4ade80)':soc>20?'linear-gradient(90deg,#eab308,#fbbf24)':'linear-gradient(90deg,#ef4444,#f87171)';
  $('bat-pct').textContent=Math.round(soc)+'%';
  $('bi-volt').textContent=fixed(t.voltage_v,2);$('bi-curr').textContent=fixed(t.current_a,2);$('bi-pwr').textContent=fixed(t.power_w,1);$('bi-wh').textContent=fixed(t.available_wh,1);
}

/* RENDER FUNCTIONS */
function renderTelemetry(d){state.telemetry=d;
  setBind('voltage',fixed(d.voltage_v,2,' V'));setBind('current',fixed(d.current_a,2,' A'));setBind('power',fixed(d.power_w,0,' W'));
  setBind('runtime',minutesToLabel(d.time_min));setBind('available-wh',fixed(d.available_wh,1,' Wh'));
  setBind('temp-bat',fixed(d.temp_bat_c,1,' °C'));setBind('temp-sys',fixed(d.temp_inv_c,1,' °C'));
  setBind('charging-state',d.charging?'Charging':'Idle');
  updateGauges();
  renderBattery(d);updateStatusBar(d,null);
}
function renderSystem(d){state.system=d;
  setBind('mode',d.bridge_mode||d.rp_mode||'--');setBind('wifi-mode',d.wifi_mode||'--');setBind('hostname',d.hostname||'--');
  setBind('ap-ip',d.ap_ip||'--');setBind('sta-ip',d.sta_ip||'--');setBind('status',d.status||'--');
  setBind('hello-counter',d.hello_counter??'--');setBind('telemetry-counter',d.telemetry_counter??'--');
  setBind('stats-counter',d.stats_counter??'--');setBind('ack-counter',d.ack_counter??'--');setBind('error-counter',d.error_counter??'--');
  const ntp=d.ntp_enabled===false?'NTP off':(d.ntp_synced?('Synced '+timeLabel(d.ntp_epoch)): 'No sync');
  setBind('ntp-status',ntp);
  updateStatusBar(null,d);
  $('ind-link').className='idot'+(d.link_up?' on':' off');
}
function renderStats(d){state.stats=d;
  setBind('efc-total',fixed(d.efc_total,2));setBind('soh-last',fixed(d.soh_last,1,'%'));
  setBind('energy-in',fixed(d.energy_in_wh,1,' Wh'));setBind('energy-out',fixed(d.energy_out_wh,1,' Wh'));
  setBind('peak-current',fixed(d.peak_current_a,1,' A'));setBind('peak-power',fixed(d.peak_power_w,0,' W'));
  renderEvStats(d);
}

/* CONTROL PAGE */
function renderPorts(d){state.ports=d;const area=$('port-switches');
  if(!d){area.innerHTML='<div class="note">No port data.</div>';return}
  const ports=[
    {k:'dc_out',l:'DC Output',icon:'<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="16"/><line x1="8" y1="12" x2="16" y2="12"/></svg>'},
    {k:'usb_pd',l:'USB PD',icon:'<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="7" y="2" width="10" height="20" rx="2"/><line x1="12" y1="18" x2="12" y2="18.01"/></svg>'},
    {k:'fan',l:'Fan',icon:'<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 12c-1.5-3-4-4-6-3s-3 4-1.5 6 4 3 6 2m1.5-5c3-1.5 4-4 3-6s-4-3-6-1.5-3 4-2 6m5 1.5c1.5 3 4 4 6 3s3-4 1.5-6-4-3-6-2m-5-1.5c-3 1.5-4 4-3 6s4 3 6 1.5 3-4 2-6"/><circle cx="12" cy="12" r="2"/></svg>'}
  ];let h='';
  ports.forEach(p=>{
    const onKey=p.k+'_on',lockKey=p.k+'_lock';
    if(d[onKey]===undefined)return;
    const on=!!d[onKey],lk=d[lockKey]&&d[lockKey]!=='none'&&d[lockKey]!=='',isFan=p.k==='fan';
    h+='<div class="sw-card"><div class="sw-left"><div class="sw-icon'+(on?' on':'')+'">'+p.icon+'</div><div><div class="sw-label">'+p.l+(isFan?' (auto)':'')+'</div><div class="sw-state'+(on?' on':' off')+'">'+(on?'ON':'OFF')+(lk?' ('+d[lockKey]+')':'')+'</div></div></div>'
    +(isFan?'':('<label class="toggle"><input type="checkbox"'+(on?' checked':'')+(lk?' disabled':'')+' data-port="'+p.k+'"><span class="slider"></span></label>'))+'</div>'});
  if(d.safe_mode)h+='<div class="note" style="border-color:var(--danger);background:var(--danger-soft);margin-top:8px">Safe mode active — ports locked.</div>';
  if(d.policy)h+='<div class="note" style="margin-top:8px">Policy: '+d.policy+'</div>';
  area.innerHTML=h||'<div class="note">No ports.</div>';
  area.querySelectorAll('.toggle input').forEach(cb=>cb.addEventListener('change',async()=>{
    const act=cb.checked?'on':'off';
    try{const r=await api('/api/ports',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({port:cb.dataset.port,state:act})});
      toast('Port',r.message||'Done',r.ok?'success':'danger');await loadPorts()}catch(e){toast('Port',e.message,'danger');await loadPorts()}}));
  $('ind-charge').className='idot'+(state.telemetry&&state.telemetry.charging?' on':' off');
  $('ind-safe').className='idot'+(d.safe_mode?' warn':' off');
}

/* EVENTS STATS */
function renderEvStats(d){if(!d)return;
  $('st-boots').textContent=d.boot_count??'--';
  $('st-cycles').textContent=fixed(d.efc_total,1);
  const rh=d.runtime_h;$('st-runtime').textContent=rh!=null?(rh>=24?fixed(rh/24,1,' d'):fixed(rh,1,' h')):'--';
  $('st-soh').textContent=fixed(d.soh_last,1,'%');
  const ein=d.energy_in_wh||0,eout=d.energy_out_wh||0;
  $('st-ein').textContent=ein>=1000?fixed(ein/1000,2,' kWh'):fixed(ein,0,' Wh');
  $('st-eout').textContent=eout>=1000?fixed(eout/1000,2,' kWh'):fixed(eout,0,' Wh');
  $('st-eff').textContent=ein>0?fixed(eout/ein*100,1,'%'):'--';
  const cyc=d.efc_total||0;$('st-avg-cycle').textContent=cyc>0.1?fixed(eout/cyc,0,' Wh'):'--';
  $('st-peak-a').textContent=fixed(d.peak_current_a,1,' A');
  $('st-peak-w').textContent=fixed(d.peak_power_w,0,' W');
  $('st-tavg').textContent=fixed(d.temp_avg_c,1,' °C');
  $('st-trange').textContent=(d.temp_min_c!=null&&d.temp_max_c!=null)?fixed(d.temp_min_c,0)+'..'+fixed(d.temp_max_c,0)+' °C':'--';
  $('st-alarms').textContent=d.alarm_events??d.total_alarm_events??'--';
  $('st-ocp').textContent=d.ocp_events??d.total_ocp_events??'--';
  $('st-temp-ev').textContent=d.temp_events??d.total_temp_events??'--';
}

/* EVENT LOG */
function evBadge(n){if(!n)return'other';const u=n.toUpperCase();if(u==='BOOT')return'boot';if(u.includes('CHARGE'))return'charge';if(u.includes('DISCHARGE'))return'discharge';if(u==='ALARM'||u.includes('WARN')||u==='OCP')return'alarm';return'other'}
function renderLogs(p){state.logs=p;const total=p.total||0;
  $('ev-prev').disabled=logPage<=0;$('ev-next').disabled=(logPage+1)*PAGE_SIZE>=total;
  $('ev-page-info').textContent=total?((logPage*PAGE_SIZE+1)+'-'+Math.min((logPage+1)*PAGE_SIZE,total)+' / '+total):'empty';
  const list=$('ev-list');
  if(!p.events||!p.events.length){list.innerHTML='<div class="note">No events.</div>';return}
  let rows='';p.events.forEach(e=>{
    const name=e.kind_name||('type_'+e.kind),bc=evBadge(name);
    const time='#'+(e.seq!=null?e.seq:e.idx)+' '+(e.time||e.uptime||'');
    let det='<span>SOC</span> <b>'+(e.soc_pct??'--')+'%</b>';
    if(e.voltage_v!=null)det+=' <span>V</span> <b>'+fixed(e.voltage_v,2)+'</b>';
    if(e.current_a!=null)det+=' <span>A</span> <b>'+fixed(e.current_a,2)+'</b>';
    if(e.param&&e.param!==0){const u=e.param_label==='energy_wh'?' Wh':e.param_label==='temperature'?' °C':'';det+=' <span>'+(e.param_label||'val')+'</span> <b>'+fixed(e.param,1,u)+'</b>'}
    let alarms='';if(e.alarm_names&&e.alarm_names!=='none')alarms=' '+e.alarm_names.split(',').map(n=>'<span class="alarm-tag">'+n.trim()+'</span>').join(' ');
    rows+='<tr><td class="ev-time">'+time+'</td><td><span class="ev-badge '+bc+'">'+name+'</span></td><td><div class="ev-detail">'+det+alarms+'</div></td></tr>';
  });
  list.innerHTML='<table class="ev-table"><thead><tr><th style="width:28%">Time</th><th style="width:18%">Type</th><th>Details</th></tr></thead><tbody>'+rows+'</tbody></table>';
}

/* SETTINGS */
function renderRpSettings(d){const f=$('rp-form');
  ['capacity_ah','vbat_warn_v','vbat_cut_v','cell_warn_v','cell_cut_v','temp_bat_warn_c','temp_bat_cut_c'].forEach(k=>{if(f.elements[k]&&d[k]!=null)f.elements[k].value=d[k]});
  if(f.elements.sound&&d.buzzer_preset!=null)f.elements.sound.value=String(d.buzzer_preset)==='2'?'silent':(String(d.buzzer_preset)==='1'?'minimal':'full')}
function renderNetSettings(d){const f=$('net-form');f.elements.wifi_mode.value=d.wifi_mode||'ap';f.elements.hostname.value=d.hostname||'';f.elements.ap_ssid.value=d.ap_ssid||'';f.elements.sta_ssid.value=d.sta_ssid||'';f.elements.ap_password.value='';f.elements.sta_password.value='';f.elements.ota_password.value='';
  f.elements.admin_password.value='';f.elements.admin_password.placeholder=d.admin_password_set?'set (empty to remove)':'not set (empty=no auth)';
  const nf=$('ntp-form');if(nf){nf.elements.ntp_enabled.value=d.ntp_enabled?'true':'false';nf.elements.ntp_server.value=d.ntp_server||'pool.ntp.org';nf.elements.tz_offset.value=d.tz_offset!=null?d.tz_offset:7200}}

/* CLI TERMINAL */
const CLI_HELP=`POWERSTATION CLI
────────────────────────────────
PING                          Test connection
HELLO                         Handshake / state
HELP                          Show available commands

GET TELEMETRY                 Battery data (V, A, SOC, temps)
GET STATS                     Lifetime statistics
GET SETTINGS                  Current configuration
GET PORTS                     Port states (DC_OUT, USB_PD)
GET LOG <start> [count]       Read log entries (max 16)
GET OTA                       OTA subsystem status

SET <key> <value>             Change config value
  capacity_ah, vbat_warn_v, vbat_cut_v, cell_warn_v,
  cell_cut_v, temp_bat_warn_c, temp_bat_cut_c,
  sound (full|minimal|silent), esp_mode (off|web|ota)

ACTION LOG_RESET              Clear event log
ACTION STATS_RESET            Reset lifetime stats
ACTION PORT_SET <p> <s>       Port: dc_out|usb_pd  State: on|off|toggle
ACTION SHUTDOWN               Power off system

STATUS [text]                 Set bridge status text
`;
function termPrint(text,cls){const out=$('term-out');const s=document.createElement('span');s.className=cls||'';s.textContent=text+'\n';out.appendChild(s);out.scrollTop=out.scrollHeight}
function termInit(){termPrint(CLI_HELP,'t-info')}
async function termExec(cmd){if(!cmd.trim())return;termPrint('> '+cmd,'t-cmd');
  try{const r=await api('/api/cli',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({cmd})});
    if(r.lines&&r.lines.length)r.lines.forEach(l=>{try{const j=JSON.parse(l);termPrint(JSON.stringify(j,null,2),'t-ok')}catch(e){termPrint(l,'t-ok')}});
    else termPrint('(no response)','t-info');
  }catch(e){termPrint('ERROR: '+e.message,'t-err')}}

/* LOADING */
async function loadSystem(){try{renderSystem(await api('/api/system'))}catch(e){}}
async function loadTelemetry(){try{renderTelemetry(await api('/api/telemetry'))}catch(e){}}
async function loadStats(){try{renderStats(await api('/api/stats'))}catch(e){}}
async function loadLogs(pg){if(pg!=null)logPage=pg;try{renderLogs(await api('/api/log?start='+(logPage*PAGE_SIZE)+'&count='+PAGE_SIZE))}catch(e){toast('Events',e.message,'danger')}}
async function loadRpSettings(){try{renderRpSettings(await api('/api/settings/rp2040'))}catch(e){}}
async function loadNetSettings(){try{renderNetSettings(await api('/api/settings/network'))}catch(e){}}
async function loadPorts(){try{renderPorts(await api('/api/ports'))}catch(e){renderPorts(null)}}
async function loadCache(){try{$('raw-cache').textContent=JSON.stringify(await api('/api/cache'),null,2)}catch(e){}}
async function refreshCore(){await Promise.allSettled([loadSystem(),loadTelemetry(),loadStats()])}

/* NTP CLOCK */
function startNtpClock(){stopNtpClock();updateNtpClock();state.ntpTimer=setInterval(updateNtpClock,2000)}
function stopNtpClock(){if(state.ntpTimer){clearInterval(state.ntpTimer);state.ntpTimer=null}}
async function updateNtpClock(){try{const t=await api('/api/time');if(t.synced){const p=n=>String(n).padStart(2,'0');$('ntp-clock').textContent=p(t.hour)+':'+p(t.min)+':'+p(t.sec)}else{$('ntp-clock').textContent='not synced'}}catch(e){$('ntp-clock').textContent='--'}}

/* NAVIGATION */
function openPage(name){state.route=name;
  document.querySelectorAll('.page').forEach(p=>p.classList.toggle('active',p.id==='p-'+name));
  document.querySelectorAll('.tab').forEach(t=>t.classList.toggle('active',t.dataset.p===name));
  if(name==='events'){logPage=0;Promise.allSettled([loadLogs(0),loadStats()])}
  if(name==='settings')loadRpSettings();
  if(name==='network'){loadNetSettings();startNtpClock()}
  if(name==='control')loadPorts();
  if(name==='system'){loadCache();termInit()}
  if(name!=='network')stopNtpClock();
}

/* INIT */
document.addEventListener('DOMContentLoaded',async()=>{
  document.querySelectorAll('.tab').forEach(t=>t.addEventListener('click',()=>openPage(t.dataset.p)));
  /* Event buttons */
  $('ev-refresh').addEventListener('click',()=>loadLogs());
  $('ev-prev').addEventListener('click',()=>{if(logPage>0)loadLogs(logPage-1)});
  $('ev-next').addEventListener('click',()=>loadLogs(logPage+1));
  $('ev-clear').addEventListener('click',async()=>{if(!confirm('Clear entire event log?'))return;try{await api('/api/actions',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({name:'LOG_RESET'})});logPage=0;await loadLogs(0);toast('Events','Log cleared','success')}catch(e){toast('Events',e.message,'danger')}});
  /* Control buttons */
  $('shutdown-btn').addEventListener('click',async()=>{if(!confirm('Shut down the PowerStation?'))return;try{const r=await api('/api/actions',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({name:'SHUTDOWN'})});toast('Shutdown',r.message||'OK',r.ok?'success':'danger')}catch(e){toast('Shutdown',e.message,'danger')}});
  /* Settings forms */
  $('rp-form').addEventListener('submit',async ev=>{ev.preventDefault();const f=ev.target,n=$('rp-note');n.textContent='Applying...';try{for(const[k,v]of new FormData(f).entries()){if(!v)continue;const r=await api('/api/settings/rp2040/apply',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({key:k,value:v})});if(!r.ok)throw new Error(r.message||'Failed: '+k)}n.textContent='Saved.';toast('Settings','Applied','success');await loadRpSettings()}catch(e){n.textContent=e.message;toast('Settings',e.message,'danger')}});
  $('rp-reload').addEventListener('click',async()=>{try{await loadRpSettings();toast('Settings','Reloaded','success')}catch(e){toast('Settings',e.message,'danger')}});
  $('net-form').addEventListener('submit',async ev=>{ev.preventDefault();const n=$('net-note');n.textContent='Saving...';try{const r=await api('/api/settings/network',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(ev.target))});n.textContent=r.message||'Saved.';toast('Network',n.textContent,r.ok?'success':'danger');await loadSystem()}catch(e){n.textContent=e.message;toast('Network',e.message,'danger')}});
  $('net-reload').addEventListener('click',async()=>{try{await loadNetSettings();toast('Network','Reloaded','success')}catch(e){toast('Network',e.message,'danger')}});
  /* NTP form */
  $('ntp-form').addEventListener('submit',async ev=>{ev.preventDefault();const n=$('ntp-note');n.textContent='Saving...';try{const r=await api('/api/settings/network',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(ev.target))});n.textContent=r.message||'Saved.';toast('NTP',n.textContent,r.ok?'success':'danger')}catch(e){n.textContent=e.message;toast('NTP',e.message,'danger')}});
  /* Chart range selectors */
  updateGauges();
  /* OTA */
  $('ota-form').addEventListener('submit',ev=>{ev.preventDefault();const file=$('ota-file').files[0],note=$('ota-note'),fill=$('ota-fill');if(!file){note.textContent='Choose .bin first';return}
    fill.style.width='0%';note.textContent='Uploading...';const fd=new FormData();fd.append('update',file);const x=new XMLHttpRequest();x.open('POST','/api/ota/upload');
    x.upload.onprogress=e=>{if(!e.lengthComputable)return;const p=clamp(e.loaded/e.total*100,0,100);fill.style.width=p+'%';note.textContent='Uploading '+Math.round(p)+'%'};
    x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}if(x.status<300&&r.ok){fill.style.width='100%';note.textContent=r.message||'Done';toast('OTA',note.textContent,'success')}else{fill.style.width='0%';note.textContent=r.message||'HTTP '+x.status;toast('OTA',note.textContent,'danger')}};
    x.onerror=()=>{fill.style.width='0%';note.textContent='Failed';toast('OTA','Upload failed','danger')};x.send(fd)});
  /* CLI */
  $('term-cmd').addEventListener('keydown',async ev=>{if(ev.key!=='Enter')return;const v=$('term-cmd').value;$('term-cmd').value='';await termExec(v)});
  $('sys-refresh').addEventListener('click',()=>loadCache());
  /* Initial load */
  await refreshCore();loadLogs();termInit();
  setInterval(()=>{refreshCore();if(state.route==='control')loadPorts()},3000);
  setInterval(()=>{if(state.route==='events')loadLogs();if(state.route==='system')loadCache()},10000);
});
</script></body></html>
)HTML";
