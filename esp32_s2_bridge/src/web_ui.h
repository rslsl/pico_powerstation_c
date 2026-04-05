#pragma once

#include <pgmspace.h>

static const char PSTATION_WEB_UI[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <title>PowerStation Bridge</title>
  <style>
    :root{--bg:#f5f7fb;--surface:#fff;--surface-muted:#f6f8fb;--border:#e6ebf1;--text:#182433;--muted:#667382;--primary:#066fd1;--primary-soft:#e9f1fb;--success:#2fb344;--success-soft:#edf9f0;--warning:#f59f00;--warning-soft:#fff4db;--danger:#d63939;--danger-soft:#feeeee;--shadow:0 1px 2px rgba(24,36,51,.04),0 10px 24px rgba(24,36,51,.06);--radius:18px;--radius-sm:14px;--font:"Segoe UI Variable Text","Segoe UI","Helvetica Neue",sans-serif;--mono:"Cascadia Mono","Consolas",monospace}
    *{box-sizing:border-box}html,body{margin:0;min-height:100%}body{font-family:var(--font);color:var(--text);background:linear-gradient(180deg,#fbfcfe 0%,var(--bg) 160px)}button,input,select{font:inherit}a{text-decoration:none;color:inherit}
    .icons{position:absolute;width:0;height:0;overflow:hidden}.page{min-height:100vh}.container{max-width:1120px;margin:0 auto;padding:0 14px}
    .navbar{position:sticky;top:0;z-index:30;background:rgba(245,247,251,.92);backdrop-filter:blur(12px);border-bottom:1px solid var(--border)}.navbar-shell{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:12px 0 10px}
    .brand{display:flex;align-items:center;gap:12px;min-width:0}.brand-mark{width:42px;height:42px;border-radius:12px;display:grid;place-items:center;background:linear-gradient(135deg,var(--primary),#4f98e5);color:#fff;box-shadow:0 10px 20px rgba(6,111,209,.22)}.brand-mark svg{width:20px;height:20px}.brand-copy{min-width:0}.brand-title{font-size:16px;line-height:1.15;font-weight:700}
    .page-pretitle{font-size:11px;line-height:1.2;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}.toolbar{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:8px}.toolbar-pill{display:inline-flex;align-items:center;gap:8px;min-height:38px;padding:8px 12px;border-radius:999px;border:1px solid var(--border);background:rgba(255,255,255,.92);color:var(--muted);font-size:13px;max-width:220px}.toolbar-pill span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.toolbar-pill.online{background:var(--success-soft);border-color:#ccebd3;color:#1b7d2d}
    .status-dot{width:8px;height:8px;border-radius:50%;background:var(--warning);box-shadow:0 0 0 3px rgba(245,159,0,.14)}.status-dot.online{background:var(--success);box-shadow:0 0 0 3px rgba(47,179,68,.14)}
    .tabs{display:flex;gap:8px;overflow:auto;padding:0 0 12px;scrollbar-width:none}.tabs::-webkit-scrollbar{display:none}.nav-btn{border:1px solid transparent;background:transparent;color:var(--muted);border-radius:12px;padding:10px 12px;display:inline-flex;align-items:center;gap:8px;cursor:pointer;white-space:nowrap;flex:0 0 auto}.nav-btn.active{background:var(--surface);border-color:var(--border);box-shadow:var(--shadow);color:var(--text);font-weight:700}
    .toolbar-pill svg,.badge svg,.nav-btn svg,.mini-title svg,.section-title svg,.btn svg{width:18px;height:18px;flex:0 0 18px}.page-wrapper{padding:16px 0 30px}.page-header-card{padding:18px;display:grid;gap:16px;background:linear-gradient(180deg,#ffffff,#f8fbff)}
    .heading-row{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:flex-start;gap:14px}.heading-copy{min-width:0;display:grid;gap:6px}h1,h2,h3,p{margin:0}.page-title{font-size:clamp(28px,6vw,38px);line-height:1.04;letter-spacing:-.03em}.page-copy,.subtle,.note{color:var(--muted);line-height:1.55;font-size:14px}
    .badge-row{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.badge{display:flex;gap:10px;align-items:center;padding:14px;border-radius:var(--radius-sm);background:var(--surface);border:1px solid var(--border);min-width:0}.badge label,.metric label,.chip label,.field,.kv label,.bar-head span{display:block;margin-bottom:4px;font-size:11px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}.badge strong{display:block;font-size:13px;line-height:1.2;word-break:break-word}
    .metric-grid,.chip-grid,.two-col,.kv-grid,.form-grid{display:grid;gap:12px}.metric-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.chip-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.two-col,.kv-grid,.form-grid{grid-template-columns:1fr}.page-body{display:grid;gap:16px;margin-top:16px}.section{display:none;gap:16px}.section.active{display:grid}
    .card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow)}.panel{padding:16px;display:grid;gap:14px}.section-title,.mini-title{display:flex;align-items:flex-start;gap:12px}.section-title h2,.mini-title h3{font-size:18px;line-height:1.2}.section-title .icon,.mini-title .icon{width:38px;height:38px;border-radius:12px;display:grid;place-items:center;background:var(--primary-soft);color:var(--primary);border:1px solid #d6e6f8;flex:0 0 38px}
    .metric{padding:16px;border-radius:var(--radius-sm);border:1px solid var(--border);background:var(--surface);min-width:0}.metric strong{display:block;font-size:29px;line-height:1;margin-top:8px;letter-spacing:-.04em}.metric small{display:block;color:var(--muted);margin-top:10px;line-height:1.45;font-size:13px}
    .chip{padding:14px;border-radius:var(--radius-sm);border:1px solid var(--border);background:var(--surface-muted)}.chip strong{display:block;font-size:17px;line-height:1.15;word-break:break-word}.kv{padding:14px;border-radius:var(--radius-sm);border:1px solid var(--border);background:var(--surface-muted)}.kv strong{display:block;line-height:1.35;word-break:break-word;font-size:15px}
    .chart{min-height:154px;border:1px solid var(--border);border-radius:var(--radius-sm);background:linear-gradient(180deg,#ffffff,#f7faff);padding:10px;overflow:hidden}.chart svg{display:block;width:100%;height:130px}.empty{min-height:130px;display:grid;place-items:center;text-align:center;color:var(--muted);font-size:13px}
    .bars{display:grid;gap:10px}.bar{padding:12px;border-radius:var(--radius-sm);border:1px solid var(--border);background:var(--surface-muted)}.bar-head{display:flex;justify-content:space-between;gap:10px;align-items:flex-end;margin-bottom:8px}.bar-head strong{font-size:14px}.track,.upload-track,.dist-track{height:10px;border-radius:999px;background:#e4ebf3;overflow:hidden}.fill,.upload-fill,.dist-fill{width:0;height:100%;border-radius:inherit;transition:width .25s ease}.fill{background:linear-gradient(90deg,var(--primary),#61a4eb)}.fill.warn{background:linear-gradient(90deg,var(--warning),#ffcb66)}.fill.danger{background:linear-gradient(90deg,var(--danger),#f27c7c)}.bar-foot{display:flex;justify-content:space-between;gap:10px;margin-top:8px;font-size:12px;color:var(--muted)}
    .actions{display:flex;flex-wrap:wrap;gap:8px}.btn{border:1px solid var(--border);background:var(--surface);color:var(--text);border-radius:12px;padding:11px 13px;display:inline-flex;align-items:center;gap:8px;cursor:pointer;min-height:42px}.btn.primary{background:var(--primary);border-color:var(--primary);color:#fff}.btn.warn{background:var(--warning-soft);border-color:#f1dfb2;color:#9a6500}.btn.danger{background:var(--danger-soft);border-color:#f2d0d0;color:#af2d2d}
    .event-list{display:grid;gap:10px}.event-card{padding:14px;border-radius:var(--radius-sm);border:1px solid var(--border);background:var(--surface);display:grid;gap:8px}.event-top{display:flex;justify-content:space-between;gap:10px;align-items:flex-start}.event-kind{display:inline-flex;align-items:center;padding:5px 10px;border-radius:999px;background:var(--primary-soft);color:var(--primary);border:1px solid #d6e6f8;font-family:var(--mono);font-size:12px}
    .event-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.event-grid span{display:block;font-size:12px;color:var(--muted);margin-bottom:4px}.event-grid strong{display:block;font-size:14px;word-break:break-word}.dist-list{display:grid;gap:10px}.dist-row{display:grid;grid-template-columns:90px minmax(0,1fr) 32px;gap:10px;align-items:center;font-size:13px}.dist-fill{background:linear-gradient(90deg,var(--primary),#61a4eb)}
    form{display:grid;gap:12px}.field{color:var(--muted);font-size:13px}input,select{width:100%;margin-top:6px;border:1px solid var(--border);border-radius:12px;padding:12px 13px;background:#fff;color:var(--text)}input:focus,select:focus{outline:none;border-color:#95c4f0;box-shadow:0 0 0 4px rgba(6,111,209,.12)}
    .note-box{padding:12px;border-radius:12px;background:var(--surface-muted);border:1px solid var(--border);color:var(--muted);line-height:1.5;font-size:13px}.json{margin:0;padding:12px;border-radius:12px;border:1px solid var(--border);background:#fbfcfd;overflow:auto;max-height:320px;white-space:pre-wrap;font-family:var(--mono);font-size:12px;line-height:1.5}
    .upload-fill{background:linear-gradient(90deg,var(--warning),#ffd06d)}.footer-note{text-align:center;color:var(--muted);font-size:12px;margin:14px 0 0}.toast-stack{position:fixed;right:12px;bottom:12px;z-index:40;display:grid;gap:10px;width:min(320px,calc(100vw - 24px))}.toast{padding:12px 14px;border-radius:14px;border:1px solid var(--border);box-shadow:var(--shadow);background:#fff}.toast strong{display:block;font-size:14px;margin-bottom:4px}.toast p{margin:0;font-size:13px;line-height:1.45;color:var(--muted)}.toast.success{border-color:#ccebd3}.toast.warn{border-color:#f1dfb2}.toast.danger{border-color:#f2d0d0}
    body.pico-ota-focus .section[data-page="/settings"] [data-settings-block]:not([data-settings-block="pico-ota"]){display:none}
    @media (min-width:700px){.container{padding:0 18px}.badge-row{grid-template-columns:repeat(4,minmax(0,1fr))}.metric-grid{grid-template-columns:repeat(4,minmax(0,1fr))}.chip-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.two-col{grid-template-columns:repeat(2,minmax(0,1fr))}.kv-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.form-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.event-grid{grid-template-columns:repeat(4,minmax(0,1fr))}}
  </style>
</head>
<body>
  <svg class="icons" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
    <symbol id="i-home" viewBox="0 0 16 16"><path d="M8.354 1.146a.5.5 0 0 0-.708 0l-6 6A.5.5 0 0 0 2 8h1v6.5a.5.5 0 0 0 .5.5h3.5v-4.5a1 1 0 0 1 1-1h0a1 1 0 0 1 1 1V15h3.5a.5.5 0 0 0 .5-.5V8h1a.5.5 0 0 0 .354-.854z" fill="currentColor"/></symbol>
    <symbol id="i-chart" viewBox="0 0 16 16"><path d="M0 0h1v15h15v1H0zm14.817 3.113a.5.5 0 0 1 .07.704l-4.5 5.5a.5.5 0 0 1-.74.037L7.06 6.767l-3.656 5.027a.5.5 0 1 1-.808-.588l4-5.5a.5.5 0 0 1 .758-.06l2.609 2.61 4.15-5.073a.5.5 0 0 1 .704-.07" fill="currentColor"/></symbol>
    <symbol id="i-list" viewBox="0 0 16 16"><path fill-rule="evenodd" d="M5 11.5a.5.5 0 0 1 .5-.5h9a.5.5 0 0 1 0 1h-9a.5.5 0 0 1-.5-.5m0-4a.5.5 0 0 1 .5-.5h9a.5.5 0 0 1 0 1h-9a.5.5 0 0 1-.5-.5m0-4a.5.5 0 0 1 .5-.5h9a.5.5 0 0 1 0 1h-9a.5.5 0 0 1-.5-.5m-3 1a1 1 0 1 0 0-2 1 1 0 0 0 0 2m0 4a1 1 0 1 0 0-2 1 1 0 0 0 0 2m0 4a1 1 0 1 0 0-2 1 1 0 0 0 0 2" fill="currentColor"/></symbol>
    <symbol id="i-sliders" viewBox="0 0 16 16"><path fill-rule="evenodd" d="M11.5 2a.5.5 0 0 1 .5.5V4h3a.5.5 0 0 1 0 1h-3v1.5a.5.5 0 0 1-1 0V5H1a.5.5 0 0 1 0-1h10V2.5a.5.5 0 0 1 .5-.5m-7 6a.5.5 0 0 1 .5.5V10h10a.5.5 0 0 1 0 1H5v1.5a.5.5 0 0 1-1 0V11H1a.5.5 0 0 1 0-1h3V8.5a.5.5 0 0 1 .5-.5" fill="currentColor"/></symbol>
    <symbol id="i-cpu" viewBox="0 0 16 16"><path d="M5 0a.5.5 0 0 1 .5.5V1H8V.5a.5.5 0 0 1 1 0V1h2.5V.5a.5.5 0 0 1 1 0V1h.5A1.5 1.5 0 0 1 14.5 2.5V3h.5a.5.5 0 0 1 0 1h-.5V6h.5a.5.5 0 0 1 0 1h-.5v2h.5a.5.5 0 0 1 0 1h-.5v1.5A1.5 1.5 0 0 1 13 13h-.5v.5a.5.5 0 0 1-1 0V13H9v.5a.5.5 0 0 1-1 0V13H5.5v.5a.5.5 0 0 1-1 0V13H4A1.5 1.5 0 0 1 2.5 11.5V10H2a.5.5 0 0 1 0-1h.5V7H2a.5.5 0 0 1 0-1h.5V4H2a.5.5 0 0 1 0-1h.5V2.5A1.5 1.5 0 0 1 4 1h.5V.5A.5.5 0 0 1 5 0m-1 2A.5.5 0 0 0 3.5 2.5v9A.5.5 0 0 0 4 12h8a.5.5 0 0 0 .5-.5v-9A.5.5 0 0 0 12 2z" fill="currentColor"/><path d="M5 5h6v4H5z" fill="currentColor"/></symbol>
    <symbol id="i-battery" viewBox="0 0 16 16"><path d="M0 6a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2h1a1 1 0 0 1 1 1v2a1 1 0 0 1-1 1h-1a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2zm2-1a1 1 0 0 0-1 1v4a1 1 0 0 0 1 1h5V5z" fill="currentColor"/></symbol>
    <symbol id="i-wifi" viewBox="0 0 16 16"><path d="M8 13.5a1.5 1.5 0 1 0 0 3 1.5 1.5 0 0 0 0-3M8 10a4.5 4.5 0 0 1 3.182 1.318l.707-.707A5.5 5.5 0 0 0 4.11 10.61l.707.707A4.5 4.5 0 0 1 8 10m0-3.5c2.315 0 4.41.938 5.924 2.452l.707-.707A9.37 9.37 0 0 0 8 5.5a9.37 9.37 0 0 0-6.631 2.745l.707.707A8.37 8.37 0 0 1 8 6.5" fill="currentColor"/></symbol>
    <symbol id="i-upload" viewBox="0 0 16 16"><path d="M.5 9.9a.5.5 0 0 1 .5.5v2.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-2.5a.5.5 0 0 1 1 0v2.5a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2v-2.5a.5.5 0 0 1 .5-.5" fill="currentColor"/><path d="M7.646 1.146a.5.5 0 0 1 .708 0l3 3a.5.5 0 0 1-.708.708L8.5 2.707V11.5a.5.5 0 0 1-1 0V2.707L5.354 4.854a.5.5 0 1 1-.708-.708z" fill="currentColor"/></symbol>
  </svg>
  <div class="toast-stack" id="toast-stack"></div>
  <div class="page">
    <header class="navbar">
      <div class="container navbar-shell">
        <div class="brand">
          <div class="brand-mark"><svg><use href="#i-battery"></use></svg></div>
          <div class="brand-copy">
            <div class="page-pretitle">PowerStation ESP32-S2</div>
            <div class="brand-title">Bridge Dashboard</div>
          </div>
        </div>
        <div class="toolbar">
          <div class="toolbar-pill" id="link-pill"><span class="status-dot" id="link-dot"></span><span data-bind="link">WAIT</span></div>
          <div class="toolbar-pill"><svg><use href="#i-wifi"></use></svg><span data-bind="network-badge">--</span></div>
        </div>
      </div>
      <div class="container">
        <nav class="tabs" aria-label="Navigation">
          <button class="nav-btn active" type="button" data-route="/dashboard"><svg><use href="#i-home"></use></svg>Dashboard</button>
          <button class="nav-btn" type="button" data-route="/telemetry"><svg><use href="#i-chart"></use></svg>Telemetry</button>
          <button class="nav-btn" type="button" data-route="/events"><svg><use href="#i-list"></use></svg>Events</button>
          <button class="nav-btn" type="button" data-route="/settings"><svg><use href="#i-sliders"></use></svg>Settings</button>
          <button class="nav-btn" type="button" data-route="/pico-ota" style="display:none"><svg><use href="#i-upload"></use></svg>Pico OTA</button>
          <button class="nav-btn" type="button" data-route="/system"><svg><use href="#i-cpu"></use></svg>System</button>
        </nav>
      </div>
    </header>
    <div class="page-wrapper">
      <div class="container">
        <section class="page-header-card card">
          <div class="heading-row">
            <div class="heading-copy">
              <div class="page-pretitle" id="page-pretitle">Overview</div>
              <h1 class="page-title" id="page-title">Dashboard</h1>
              <p class="page-copy" id="page-description">Compact Tabler-style dashboard patterns adapted for the ESP bridge.</p>
              <p class="note" id="hero-note">Live telemetry, logs, settings and OTA remain available without external assets.</p>
            </div>
          </div>
          <div class="badge-row">
            <div class="badge"><svg><use href="#i-battery"></use></svg><div><label>Mode</label><strong data-bind="mode">--</strong></div></div>
            <div class="badge"><svg><use href="#i-chart"></use></svg><div><label>Link</label><strong data-bind="link">WAIT</strong></div></div>
            <div class="badge"><svg><use href="#i-wifi"></use></svg><div><label>Network</label><strong data-bind="network-badge">--</strong></div></div>
            <div class="badge"><svg><use href="#i-cpu"></use></svg><div><label>Status</label><strong data-bind="status">Booting</strong></div></div>
          </div>
          <div class="metric-grid">
            <div class="metric"><label>SOC</label><strong data-bind="soc">--</strong><small>Current battery state of charge.</small></div>
            <div class="metric"><label>Power</label><strong data-bind="power">--</strong><small>Live input and output power from RP2040 telemetry.</small></div>
            <div class="metric"><label>Runtime</label><strong data-bind="runtime">--</strong><small>Estimated remaining time.</small></div>
            <div class="metric"><label>Available</label><strong data-bind="available-wh">--</strong><small>Usable energy available right now.</small></div>
          </div>
        </section>
        <main class="page-body">
      <section class="section active" data-page="/dashboard">
        <article class="panel card" data-settings-block="rp">
          <div class="section-title"><div class="icon"><svg><use href="#i-home"></use></svg></div><div><h2>Quick Overview</h2><p class="subtle">Readable summary for a phone screen.</p></div></div>
          <div class="metric-grid">
            <div class="metric"><label>Voltage</label><strong data-bind="voltage">--</strong><small>Pack voltage.</small></div>
            <div class="metric"><label>Current</label><strong data-bind="current">--</strong><small>Charge or discharge current.</small></div>
            <div class="metric"><label>Battery Temp</label><strong data-bind="temp-bat">--</strong><small>Main battery temperature.</small></div>
            <div class="metric"><label>System Temp</label><strong data-bind="temp-sys">--</strong><small>System or inverter temperature.</small></div>
          </div>
        </article>
        <div class="two-col">
          <article class="panel card">
            <div class="mini-title"><div class="icon"><svg><use href="#i-chart"></use></svg></div><div><h3>Live Bars</h3><p class="subtle">Fast visual health checks.</p></div></div>
            <div class="bars">
              <div class="bar"><div class="bar-head"><span>Battery level</span><strong id="bar-soc-label">--</strong></div><div class="track"><div class="fill" id="bar-soc-fill"></div></div><div class="bar-foot"><span>0%</span><span id="bar-soc-note">SOC</span><span>100%</span></div></div>
              <div class="bar"><div class="bar-head"><span>Thermal state</span><strong id="bar-temp-label">--</strong></div><div class="track"><div class="fill" id="bar-temp-fill"></div></div><div class="bar-foot"><span>Cool</span><span id="bar-temp-note">Temps</span><span>Hot</span></div></div>
              <div class="bar"><div class="bar-head"><span>Bridge link</span><strong id="bar-link-label">--</strong></div><div class="track"><div class="fill" id="bar-link-fill"></div></div><div class="bar-foot"><span>Offline</span><span id="bar-link-note">Status</span><span>Online</span></div></div>
            </div>
          </article>
          <article class="panel card">
            <div class="mini-title"><div class="icon"><svg><use href="#i-wifi"></use></svg></div><div><h3>Bridge Snapshot</h3><p class="subtle">Network and runtime status.</p></div></div>
            <div class="kv-grid">
              <div class="kv"><label>RP2040 Mode</label><strong data-bind="mode">--</strong></div>
              <div class="kv"><label>Wi-Fi Mode</label><strong data-bind="wifi-mode">--</strong></div>
              <div class="kv"><label>Hostname</label><strong data-bind="hostname">--</strong></div>
              <div class="kv"><label>UART Link</label><strong data-bind="link">--</strong></div>
              <div class="kv"><label>AP Address</label><strong data-bind="ap-ip">--</strong></div>
              <div class="kv"><label>STA Address</label><strong data-bind="sta-ip">--</strong></div>
            </div>
          </article>
        </div>
        <div class="two-col">
          <article class="panel card"><div class="mini-title"><div class="icon"><svg><use href="#i-chart"></use></svg></div><div><h3>Charge Trend</h3><p class="subtle" id="soc-chart-note">Collecting SOC samples...</p></div></div><div class="chart" id="chart-soc"></div></article>
          <article class="panel card"><div class="mini-title"><div class="icon"><svg><use href="#i-chart"></use></svg></div><div><h3>Power Trend</h3><p class="subtle" id="power-chart-note">Live power history...</p></div></div><div class="chart" id="chart-power"></div></article>
        </div>
        <article class="panel card" data-settings-block="network">
          <div class="mini-title"><div class="icon"><svg><use href="#i-list"></use></svg></div><div><h3>Lifetime Stats</h3><p class="subtle">Compact summary of long-term values.</p></div></div>
          <div class="chip-grid">
            <div class="chip"><label>Cycles</label><strong data-bind="efc-total">--</strong></div>
            <div class="chip"><label>SOH</label><strong data-bind="soh-last">--</strong></div>
            <div class="chip"><label>Energy In</label><strong data-bind="energy-in">--</strong></div>
            <div class="chip"><label>Energy Out</label><strong data-bind="energy-out">--</strong></div>
            <div class="chip"><label>Peak Current</label><strong data-bind="peak-current">--</strong></div>
            <div class="chip"><label>Peak Power</label><strong data-bind="peak-power">--</strong></div>
          </div>
          <div class="actions"><a class="btn" href="/export/stats.json"><svg><use href="#i-upload"></use></svg>Stats JSON</a><a class="btn" href="/export/stats.csv"><svg><use href="#i-upload"></use></svg>Stats CSV</a></div>
        </article>
      </section>
      <section class="section" data-page="/telemetry">
        <article class="panel card" data-settings-block="esp-ota">
          <div class="section-title"><div class="icon"><svg><use href="#i-chart"></use></svg></div><div><h2>Telemetry</h2><p class="subtle" id="telemetry-note">Waiting for live RP2040 data.</p></div></div>
          <div class="metric-grid">
            <div class="metric"><label>SOC</label><strong data-bind="soc">--</strong><small>Battery percentage.</small></div>
            <div class="metric"><label>Available</label><strong data-bind="available-wh">--</strong><small>Remaining energy.</small></div>
            <div class="metric"><label>Power</label><strong data-bind="power">--</strong><small>Instant power.</small></div>
            <div class="metric"><label>Runtime</label><strong data-bind="runtime">--</strong><small>Estimated time left.</small></div>
            <div class="metric"><label>Voltage</label><strong data-bind="voltage">--</strong><small>Pack voltage.</small></div>
            <div class="metric"><label>Current</label><strong data-bind="current">--</strong><small>Current flow.</small></div>
            <div class="metric"><label>Battery Temp</label><strong data-bind="temp-bat">--</strong><small>Battery thermal state.</small></div>
            <div class="metric"><label>System Temp</label><strong data-bind="temp-sys">--</strong><small>System thermal state.</small></div>
          </div>
        </article>
        <div class="two-col">
          <article class="panel card"><div class="mini-title"><div class="icon"><svg><use href="#i-chart"></use></svg></div><div><h3>SOC History</h3><p class="subtle">Recent browser-side samples.</p></div></div><div class="chart" id="chart-soc-telemetry"></div></article>
          <article class="panel card"><div class="mini-title"><div class="icon"><svg><use href="#i-chart"></use></svg></div><div><h3>Thermal History</h3><p class="subtle">Battery and system temperatures.</p></div></div><div class="chart" id="chart-thermal"></div></article>
        </div>
        <article class="panel card" data-settings-block="pico-ota">
          <div class="mini-title"><div class="icon"><svg><use href="#i-battery"></use></svg></div><div><h3>Drive State</h3><p class="subtle">Interpreted mode and charge state.</p></div></div>
          <div class="kv-grid">
            <div class="kv"><label>Reported Mode</label><strong data-bind="telemetry-mode">--</strong></div>
            <div class="kv"><label>Charging</label><strong data-bind="charging-state">--</strong></div>
            <div class="kv"><label>Status</label><strong data-bind="status">--</strong></div>
            <div class="kv"><label>Network</label><strong data-bind="network-badge">--</strong></div>
          </div>
        </article>
      </section>
      <section class="section" data-page="/events">
        <article class="panel card">
          <div class="section-title"><div class="icon"><svg><use href="#i-list"></use></svg></div><div><h2>Events</h2><p class="subtle" id="events-note">Recent log slice from the bridge.</p></div></div>
          <div class="actions">
            <button class="btn primary" type="button" id="refresh-log-btn"><svg><use href="#i-chart"></use></svg>Refresh</button>
            <a class="btn" href="/export/log.json"><svg><use href="#i-upload"></use></svg>Log JSON</a>
            <a class="btn" href="/export/log.csv"><svg><use href="#i-upload"></use></svg>Log CSV</a>
            <button class="btn danger" type="button" onclick="bridgeAction('LOG_RESET')">Clear Log</button>
            <button class="btn warn" type="button" onclick="bridgeAction('STATS_RESET')">Reset Stats</button>
          </div>
          <div class="chip-grid">
            <div class="chip"><label>Total</label><strong id="log-total">--</strong></div>
            <div class="chip"><label>Slice</label><strong id="log-count">--</strong></div>
            <div class="chip"><label>Dominant</label><strong id="log-dominant">--</strong></div>
          </div>
        </article>
        <article class="panel card"><div class="mini-title"><div class="icon"><svg><use href="#i-chart"></use></svg></div><div><h3>Event Distribution</h3><p class="subtle">Simple summary by event type.</p></div></div><div class="dist-list" id="event-dist"><div class="empty">Load a log slice to see distribution.</div></div></article>
        <article class="panel card"><div class="mini-title"><div class="icon"><svg><use href="#i-list"></use></svg></div><div><h3>Recent Entries</h3><p class="subtle">Cards render better on phones than wide tables.</p></div></div><div class="event-list" id="event-list"><div class="empty">No events loaded yet.</div></div></article>
      </section>
      <section class="section" data-page="/settings">
        <article class="panel card">
          <div class="section-title"><div class="icon"><svg><use href="#i-sliders"></use></svg></div><div><h2>RP2040 Settings</h2><p class="subtle">Single-column form for easier mobile editing.</p></div></div>
          <form id="rp-settings-form">
            <div class="form-grid">
              <label class="field">Capacity Ah<input name="capacity_ah" step="0.1"></label>
              <label class="field">Pack Warn V<input name="vbat_warn_v" step="0.1"></label>
              <label class="field">Pack Cut V<input name="vbat_cut_v" step="0.1"></label>
              <label class="field">Cell Warn V<input name="cell_warn_v" step="0.01"></label>
              <label class="field">Cell Cut V<input name="cell_cut_v" step="0.01"></label>
              <label class="field">Bat Warn C<input name="temp_bat_warn_c" step="1"></label>
              <label class="field">Bat Cut C<input name="temp_bat_cut_c" step="1"></label>
              <label class="field">Sound<select name="sound"><option value="full">Full</option><option value="minimal">Minimal</option><option value="silent">Silent</option></select></label>
            </div>
            <div class="actions"><button class="btn primary" type="submit"><svg><use href="#i-sliders"></use></svg>Apply</button><button class="btn" type="button" id="reload-rp-btn">Reload</button></div>
            <div class="note-box" id="rp-note">Changes are sent over UART to the RP2040 bridge.</div>
          </form>
        </article>
        <article class="panel card">
          <div class="section-title"><div class="icon"><svg><use href="#i-wifi"></use></svg></div><div><h2>Network Settings</h2><p class="subtle">Simplified Wi-Fi profile management.</p></div></div>
          <form id="network-form">
            <div class="form-grid">
              <label class="field">Wi-Fi Mode<select name="wifi_mode"><option value="ap">AP</option><option value="sta">STA</option><option value="ap_sta">AP + STA</option></select></label>
              <label class="field">Hostname<input name="hostname"></label>
              <label class="field">AP SSID<input name="ap_ssid"></label>
              <label class="field">AP Password<input name="ap_password" placeholder="Leave blank to keep"></label>
              <label class="field">STA SSID<input name="sta_ssid"></label>
              <label class="field">STA Password<input name="sta_password" placeholder="Leave blank to keep"></label>
              <label class="field">OTA Password<input name="ota_password" placeholder="Leave blank to keep"></label>
            </div>
            <div class="actions"><button class="btn primary" type="submit"><svg><use href="#i-wifi"></use></svg>Save</button><button class="btn" type="button" id="reload-network-btn">Reload</button></div>
            <div class="note-box" id="network-note">Empty password fields keep stored values unchanged.</div>
          </form>
        </article>
        <article class="panel card">
          <div class="section-title"><div class="icon"><svg><use href="#i-upload"></use></svg></div><div><h2>ESP OTA</h2><p class="subtle">Browser upload with progress for native USB workflow.</p></div></div>
          <form id="ota-form">
            <label class="field">Firmware Binary<input type="file" id="ota-file" name="update"></label>
            <div class="actions"><button class="btn warn" type="submit"><svg><use href="#i-upload"></use></svg>Upload OTA</button></div>
            <div class="upload-track"><div class="upload-fill" id="ota-fill"></div></div>
            <div class="note-box" id="ota-note">Switch RP2040 bridge mode to OTA before upload.</div>
            <div class="kv-grid">
              <div class="kv"><label>Bridge Mode</label><strong data-bind="mode">--</strong></div>
              <div class="kv"><label>Current Network</label><strong data-bind="network-badge">--</strong></div>
            </div>
          </form>
        </article>
        <article class="panel card">
          <div class="section-title"><div class="icon"><svg><use href="#i-cpu"></use></svg></div><div><h2>Pico OTA</h2><p class="subtle">Upload the inactive-slot RP2040 image and monitor staged OTA state.</p></div></div>
          <div class="kv-grid">
            <div class="kv"><label>State</label><strong data-bind="pico-ota-state">--</strong></div>
            <div class="kv"><label>Running Slot</label><strong data-bind="pico-ota-running">--</strong></div>
            <div class="kv"><label>Target Slot</label><strong data-bind="pico-ota-target">--</strong></div>
            <div class="kv"><label>Active Slot</label><strong data-bind="pico-ota-active">--</strong></div>
            <div class="kv"><label>Confirmed Slot</label><strong data-bind="pico-ota-confirmed">--</strong></div>
            <div class="kv"><label>Upload</label><strong data-bind="pico-ota-upload">--</strong></div>
          </div>
          <form id="pico-ota-form">
            <label class="field">Pico Slot Firmware (.bin)<input type="file" id="pico-ota-file" name="update" accept=".bin,application/octet-stream"></label>
            <div class="actions"><button class="btn warn" type="submit"><svg><use href="#i-upload"></use></svg>Upload Pico OTA</button><button class="btn" type="button" id="refresh-pico-ota-btn">Refresh Status</button></div>
            <div class="upload-track"><div class="upload-fill" id="pico-ota-fill"></div></div>
            <div class="note-box" id="pico-ota-note">Upload the Pico slot image for the inactive target slot shown above.</div>
          </form>
        </article>
      </section>
      <section class="section" data-page="/system">
        <article class="panel card">
          <div class="section-title"><div class="icon"><svg><use href="#i-cpu"></use></svg></div><div><h2>System Diagnostics</h2><p class="subtle">Raw counters and cached data for troubleshooting.</p></div></div>
          <div class="chip-grid">
            <div class="chip"><label>Hello</label><strong data-bind="hello-counter">--</strong></div>
            <div class="chip"><label>Telemetry</label><strong data-bind="telemetry-counter">--</strong></div>
            <div class="chip"><label>Stats</label><strong data-bind="stats-counter">--</strong></div>
            <div class="chip"><label>Settings</label><strong data-bind="settings-counter">--</strong></div>
            <div class="chip"><label>OTA</label><strong data-bind="ota-counter">--</strong></div>
            <div class="chip"><label>ACK</label><strong data-bind="ack-counter">--</strong></div>
            <div class="chip"><label>Error</label><strong data-bind="error-counter">--</strong></div>
          </div>
        </article>
        <article class="panel card"><div class="mini-title"><div class="icon"><svg><use href="#i-list"></use></svg></div><div><h3>Last Bridge Reply</h3><p class="subtle">Latest ACK and error objects.</p></div></div><pre class="json" id="last-reply">No cached ACK/ERROR yet.</pre></article>
        <article class="panel card"><div class="mini-title"><div class="icon"><svg><use href="#i-cpu"></use></svg></div><div><h3>Raw Cache</h3><p class="subtle">Useful when debugging parsing or transport.</p></div></div><div class="actions"><button class="btn" type="button" id="refresh-cache-btn">Refresh Cache</button></div><pre class="json" id="raw-cache">Waiting...</pre></article>
      </section>
        </main>
        <p class="footer-note">Dashboard shell adapted from Tabler-style layout patterns and embedded locally for offline ESP use.</p>
      </div>
    </div>
  </div>
  <script>
    const ROUTES={'/dashboard':'Dashboard','/telemetry':'Telemetry','/events':'Events','/settings':'Settings','/pico-ota':'Pico OTA','/system':'System'};
    const PAGE_META={
      '/dashboard':{pretitle:'Overview',title:'Dashboard',description:'Compact Tabler-style dashboard patterns adapted for the ESP bridge.'},
      '/telemetry':{pretitle:'Overview',title:'Telemetry',description:'Battery, power and thermal history in a compact monitoring view.'},
      '/events':{pretitle:'Activity',title:'Events',description:'Recent bridge activity, distribution and export actions.'},
      '/settings':{pretitle:'Controls',title:'Settings',description:'RP2040, Wi-Fi and ESP bridge controls tuned for smaller screens.'},
      '/pico-ota':{pretitle:'Recovery',title:'Pico OTA',description:'Dedicated upload flow for the inactive RP2040 slot after power-on while holding DOWN on Pico.'},
      '/system':{pretitle:'Diagnostics',title:'System',description:'Counters, caches and the latest bridge replies for debugging.'}
    };
    const state={route:'/dashboard',telemetry:null,system:null,stats:null,cache:null,logs:null,picoOta:null,lastPicoSafe:null,history:{soc:[],power:[],tempBat:[],tempSys:[]}};
    async function expectJson(url,options){const r=await fetch(url,options);const t=await r.text();if(!r.ok)throw new Error(t||r.statusText);return t?JSON.parse(t):{}}
    function setBind(name,value){document.querySelectorAll('[data-bind="'+name+'"]').forEach((el)=>{el.textContent=value??'--'})}
    function fixed(value,digits,suffix){if(value===undefined||value===null||value==='')return'--';const n=Number(value);if(!Number.isFinite(n))return String(value);return n.toFixed(digits)+suffix}
    function minutesToLabel(value){const m=Number(value);if(!Number.isFinite(m)||m<0)return'--';const h=Math.floor(m/60),mm=Math.round(m%60);return h>0?h+'h '+mm+'m':mm+'m'}
    function clamp(value,min,max){return Math.min(max,Math.max(min,value))}
    function normalizeRoute(path){const clean=(path||'/').replace(/\/+$/,'')||'/';return ROUTES[clean]?clean:'/dashboard'}
    function toast(title,message,tone){const stack=document.getElementById('toast-stack');const item=document.createElement('div');item.className='toast '+(tone||'success');item.innerHTML='<strong>'+title+'</strong><p>'+message+'</p>';stack.prepend(item);setTimeout(()=>item.remove(),3600)}
    function setBar(id,percent,tone){const el=document.getElementById(id);if(!el)return;el.style.width=clamp(percent||0,0,100)+'%';el.classList.remove('warn','danger');if(tone==='warn')el.classList.add('warn');if(tone==='danger')el.classList.add('danger')}
    function addSample(name,value){const n=Number(value);if(!Number.isFinite(n))return;const list=state.history[name];list.push(n);if(list.length>24)list.shift()}
    function renderSparkline(targetId,values,color,minValue,maxValue){const target=document.getElementById(targetId);if(!target)return;if(!values||values.length<2){target.innerHTML='<div class="empty">Collecting samples...</div>';return}const w=320,h=126,p=8,min=minValue!==undefined?minValue:Math.min.apply(null,values),max=maxValue!==undefined?maxValue:Math.max.apply(null,values),span=(max-min)||1;const points=values.map((v,i)=>{const x=p+(i*(w-p*2))/Math.max(values.length-1,1);const y=h-p-((v-min)/span)*(h-p*2);return x.toFixed(1)+','+y.toFixed(1)}).join(' ');const area=p+','+(h-p)+' '+points+' '+(w-p)+','+(h-p),id=targetId.replace(/[^a-z0-9]/gi,'');target.innerHTML='<svg viewBox="0 0 '+w+' '+h+'" preserveAspectRatio="none"><defs><linearGradient id="g'+id+'" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="'+color+'" stop-opacity=".24"></stop><stop offset="100%" stop-color="'+color+'" stop-opacity=".02"></stop></linearGradient></defs><line x1="'+p+'" y1="'+(h-p)+'" x2="'+(w-p)+'" y2="'+(h-p)+'" stroke="#d7e2e5"></line><polygon points="'+area+'" fill="url(#g'+id+')"></polygon><polyline points="'+points+'" fill="none" stroke="'+color+'" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"></polyline></svg>'}
    function renderDualSparkline(targetId,first,second){const target=document.getElementById(targetId);if(!target)return;if(!first||!second||first.length<2||second.length<2){target.innerHTML='<div class="empty">Collecting samples...</div>';return}const w=320,h=126,p=8,all=first.concat(second),min=Math.min.apply(null,all),max=Math.max.apply(null,all),span=(max-min)||1;const pts=(values)=>values.map((v,i)=>{const x=p+(i*(w-p*2))/Math.max(values.length-1,1);const y=h-p-((v-min)/span)*(h-p*2);return x.toFixed(1)+','+y.toFixed(1)}).join(' ');target.innerHTML='<svg viewBox="0 0 '+w+' '+h+'" preserveAspectRatio="none"><line x1="'+p+'" y1="'+(h-p)+'" x2="'+(w-p)+'" y2="'+(h-p)+'" stroke="#d7e2e5"></line><polyline points="'+pts(first)+'" fill="none" stroke="#d98a00" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"></polyline><polyline points="'+pts(second)+'" fill="none" stroke="#2c7be5" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"></polyline></svg>'}
    function setPageMeta(route){const meta=PAGE_META[route]||PAGE_META['/dashboard'];document.getElementById('page-pretitle').textContent=meta.pretitle;document.getElementById('page-title').textContent=meta.title;document.getElementById('page-description').textContent=meta.description;document.title='PowerStation Bridge - '+meta.title}
    function setPicoOtaNavVisible(visible){const button=document.querySelector('.nav-btn[data-route="/pico-ota"]');if(button)button.style.display=visible?'':'none'}
    function syncPicoOtaMode(safeReady){const becameActive=!!safeReady&&state.lastPicoSafe!==true,becameInactive=!safeReady&&state.lastPicoSafe===true;state.lastPicoSafe=!!safeReady;setPicoOtaNavVisible(!!safeReady||state.route==='/pico-ota');if(becameActive&&window.location.pathname!=='/pico-ota'){window.location.assign('/pico-ota');return}if(becameInactive&&window.location.pathname==='/pico-ota'){window.location.assign('/');return}if(becameInactive&&state.route==='/pico-ota')openRoute('/settings')}
    function openRoute(route,push){const normalized=normalizeRoute(route),sectionRoute=normalized==='/pico-ota'?'/settings':normalized;state.route=normalized;document.body.classList.toggle('pico-ota-focus',normalized==='/pico-ota');setPageMeta(normalized);document.querySelectorAll('.section').forEach((section)=>{section.classList.toggle('active',section.dataset.page===sectionRoute)});document.querySelectorAll('.nav-btn').forEach((button)=>{button.classList.toggle('active',button.dataset.route===normalized)});if(push!==false&&normalizeRoute(window.location.pathname)!==normalized)window.history.pushState({},'',normalized);if(normalized==='/events')loadLogs().catch((e)=>toast('Events',e.message,'danger'));if(normalized==='/settings')Promise.allSettled([loadRpSettings(),loadNetworkSettings(),loadPicoOtaStatus()]);if(normalized==='/pico-ota')Promise.allSettled([loadSystem(),loadPicoOtaStatus()]).catch((e)=>toast('Pico OTA',e.message,'danger'));if(normalized==='/system')Promise.allSettled([loadCache(),loadPicoOtaStatus()]).catch((e)=>toast('System',e.message,'danger'))}
    function renderSystem(data){state.system=data;setBind('mode',data.bridge_mode||data.rp_mode||'--');setBind('wifi-mode',data.wifi_mode||'--');setBind('hostname',data.hostname||'--');setBind('ap-ip',data.ap_ip||'--');setBind('sta-ip',data.sta_ip||'--');setBind('status',data.status||'--');setBind('link',data.link_up?'ONLINE':'WAIT');setBind('network-badge',((data.wifi_mode||'--')+' '+(data.ap_ip||data.sta_ip||'')).trim());setBind('hello-counter',data.hello_counter??'--');setBind('telemetry-counter',data.telemetry_counter??'--');setBind('stats-counter',data.stats_counter??'--');setBind('settings-counter',data.settings_counter??'--');setBind('ota-counter',data.ota_counter??'--');setBind('ack-counter',data.ack_counter??'--');setBind('error-counter',data.error_counter??'--');document.getElementById('bar-link-label').textContent=data.link_up?'ONLINE':'WAIT';document.getElementById('bar-link-note').textContent=data.status||'Bridge status';document.getElementById('link-dot').classList.toggle('online',!!data.link_up);document.getElementById('link-pill').classList.toggle('online',!!data.link_up);setBar('bar-link-fill',data.link_up?100:18,data.link_up?'':'warn');if(data.pico_ota_mode!==undefined)syncPicoOtaMode(!!data.pico_ota_mode)}
    function renderTelemetry(data){state.telemetry=data;addSample('soc',data.soc_pct);addSample('power',data.power_w);addSample('tempBat',data.temp_bat_c);addSample('tempSys',data.temp_inv_c);setBind('soc',fixed(data.soc_pct,1,'%'));setBind('power',fixed(data.power_w,0,' W'));setBind('runtime',minutesToLabel(data.time_min));setBind('available-wh',fixed(data.available_wh,1,' Wh'));setBind('voltage',fixed(data.voltage_v,2,' V'));setBind('current',fixed(data.current_a,2,' A'));setBind('temp-bat',fixed(data.temp_bat_c,1,' C'));setBind('temp-sys',fixed(data.temp_inv_c,1,' C'));setBind('telemetry-mode',data.mode||'--');setBind('charging-state',data.charging?'Charging':'Discharging / idle');const soc=Number(data.soc_pct)||0,tempMax=Math.max(Number(data.temp_bat_c)||0,Number(data.temp_inv_c)||0),tempTone=tempMax>=65?'danger':tempMax>=50?'warn':'',socTone=soc<=15?'danger':soc<=35?'warn':'';document.getElementById('bar-soc-label').textContent=fixed(data.soc_pct,1,'%');document.getElementById('bar-soc-note').textContent=data.charging?'Charging headroom':'Discharge reserve';setBar('bar-soc-fill',soc,socTone);document.getElementById('bar-temp-label').textContent=fixed(tempMax,1,' C');document.getElementById('bar-temp-note').textContent='Battery / system thermal load';setBar('bar-temp-fill',clamp((tempMax/80)*100,0,100),tempTone);document.getElementById('hero-note').textContent='Mode '+(data.mode||'--')+', charging='+(data.charging?'yes':'no')+'.';document.getElementById('telemetry-note').textContent='Voltage '+fixed(data.voltage_v,2,' V')+', current '+fixed(data.current_a,2,' A')+'.';renderSparkline('chart-soc',state.history.soc,'#2fb344',0,100);renderSparkline('chart-soc-telemetry',state.history.soc,'#2fb344',0,100);renderSparkline('chart-power',state.history.power,'#066fd1');renderDualSparkline('chart-thermal',state.history.tempBat,state.history.tempSys);document.getElementById('soc-chart-note').textContent='Recent browser-side SOC samples';document.getElementById('power-chart-note').textContent='Positive or negative power trend'}
    function renderStats(data){state.stats=data;setBind('efc-total',fixed(data.efc_total,2,''));setBind('soh-last',fixed(data.soh_last,1,'%'));setBind('energy-in',fixed(data.energy_in_wh,1,' Wh'));setBind('energy-out',fixed(data.energy_out_wh,1,' Wh'));setBind('peak-current',fixed(data.peak_current_a,1,' A'));setBind('peak-power',fixed(data.peak_power_w,0,' W'))}
    function renderLogs(payload){state.logs=payload;document.getElementById('log-total').textContent=payload.total??'--';document.getElementById('log-count').textContent=payload.count??'--';if(!payload.events||!payload.events.length){document.getElementById('event-dist').innerHTML='<div class="empty">No events in the selected slice.</div>';document.getElementById('event-list').innerHTML='<div class="empty">No recent events to show.</div>';document.getElementById('log-dominant').textContent='--';document.getElementById('events-note').textContent='No events returned by the bridge.';return}const counts=payload.events.reduce((acc,item)=>{const key=item.kind||'unknown';acc[key]=(acc[key]||0)+1;return acc},{}),sorted=Object.entries(counts).sort((a,b)=>b[1]-a[1]),max=sorted[0][1]||1;document.getElementById('log-dominant').textContent=sorted[0][0];document.getElementById('events-note').textContent='Showing '+payload.count+' event(s) starting from index '+payload.start+'.';document.getElementById('event-dist').innerHTML=sorted.map(([kind,count])=>'<div class="dist-row"><strong>'+kind+'</strong><div class="dist-track"><div class="dist-fill" style="width:'+((count/max)*100)+'%"></div></div><span>'+count+'</span></div>').join('');document.getElementById('event-list').innerHTML=payload.events.map((event)=>'<div class="event-card"><div class="event-top"><span class="event-kind">'+(event.kind||'--')+'</span><strong>#'+(event.idx??'--')+'</strong></div><div class="event-grid"><div><span>SOC</span><strong>'+((event.soc_pct??'--')+'%')+'</strong></div><div><span>Voltage</span><strong>'+(event.voltage_v??'--')+'</strong></div><div><span>Current</span><strong>'+(event.current_a??'--')+'</strong></div><div><span>Param</span><strong>'+(event.param??'--')+'</strong></div></div></div>').join('')}
    function renderRpSettings(data){const form=document.getElementById('rp-settings-form');['capacity_ah','vbat_warn_v','vbat_cut_v','cell_warn_v','cell_cut_v','temp_bat_warn_c','temp_bat_cut_c'].forEach((key)=>{if(form.elements[key]&&data[key]!==undefined)form.elements[key].value=data[key]});if(form.elements.sound&&data.buzzer_preset!==undefined)form.elements.sound.value=String(data.buzzer_preset)==='2'?'silent':(String(data.buzzer_preset)==='1'?'minimal':'full')}
    function renderNetworkSettings(data){const form=document.getElementById('network-form');form.elements.wifi_mode.value=data.wifi_mode||'ap';form.elements.hostname.value=data.hostname||'';form.elements.ap_ssid.value=data.ap_ssid||'';form.elements.sta_ssid.value=data.sta_ssid||'';form.elements.ap_password.value='';form.elements.sta_password.value='';form.elements.ota_password.value=''}
    function renderCache(data){state.cache=data;document.getElementById('raw-cache').textContent=JSON.stringify(data,null,2);document.getElementById('last-reply').textContent=JSON.stringify({last_ack:data.last_ack||null,last_error:data.last_error||null},null,2)}
    function renderPicoOta(data){state.picoOta=data;const dev=data.device||{},safeReady=!!dev.safe_ready,sent=Number(data.sent_bytes)||0,total=Number(data.total_bytes)||0,deviceWritten=Number(dev.written)||0,deviceSize=Number(dev.size)||0,progress=total>0?clamp((sent/total)*100,0,100):(deviceSize>0?clamp((deviceWritten/deviceSize)*100,0,100):0),uploadLabel=total>0?(sent+' / '+total+' bytes'):(deviceSize>0?(deviceWritten+' / '+deviceSize+' bytes'):(data.upload_active?'Uploading...':'Idle'));syncPicoOtaMode(safeReady);setBind('pico-ota-state',dev.state||'--');setBind('pico-ota-running',dev.running_slot||'--');setBind('pico-ota-target',dev.target_slot||'--');setBind('pico-ota-active',dev.active_slot||'--');setBind('pico-ota-confirmed',dev.confirmed_slot||'--');setBind('pico-ota-upload',uploadLabel);document.getElementById('pico-ota-fill').style.width=progress+'%';const noteParts=[];if(safeReady)noteParts.push('Dedicated Pico OTA mode is active');if(data.message)noteParts.push(data.message);if(data.version)noteParts.push('Version '+data.version);if(dev.state)noteParts.push('Device '+dev.state);if(dev.target_slot)noteParts.push('Target '+dev.target_slot);if(dev.reboot_pending)noteParts.push('Reboot pending');if(dev.error)noteParts.push('Error '+dev.error);document.getElementById('pico-ota-note').textContent=noteParts.length?noteParts.join('. ')+'.':'Upload the Pico slot image for the inactive target slot shown above.'}
    async function loadSystem(){renderSystem(await expectJson('/api/system'))}
    async function loadTelemetry(){renderTelemetry(await expectJson('/api/telemetry'))}
    async function loadStats(){renderStats(await expectJson('/api/stats'))}
    async function loadLogs(){renderLogs(await expectJson('/api/log?start=0&count=16'))}
    async function loadRpSettings(){renderRpSettings(await expectJson('/api/settings/rp2040'))}
    async function loadNetworkSettings(){renderNetworkSettings(await expectJson('/api/settings/network'))}
    async function loadCache(){renderCache(await expectJson('/api/cache'))}
    async function loadPicoOtaStatus(){renderPicoOta(await expectJson('/api/pico-ota/status'))}
    async function refreshCore(silent){const results=await Promise.allSettled([loadSystem(),loadTelemetry(),loadStats()]);const failed=results.find((item)=>item.status==='rejected');if(failed&&!silent)toast('Refresh failed',failed.reason.message||'API error','danger')}
    async function bridgeAction(name){try{const data=await expectJson('/api/actions',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({name})});toast(name,data.message||'Done',data.ok?'success':'danger');await Promise.allSettled([refreshCore(true),loadLogs()])}catch(error){toast(name,error.message,'danger')}}
    function bindNavigation(){document.querySelectorAll('.nav-btn').forEach((button)=>{button.addEventListener('click',()=>{if(button.dataset.route==='/pico-ota'){window.location.assign('/pico-ota');return}openRoute(button.dataset.route)})});window.addEventListener('popstate',()=>openRoute(window.location.pathname,false))}
    function bindForms(){
      document.getElementById('refresh-log-btn').addEventListener('click',async()=>{try{await loadLogs();toast('Events','Log slice refreshed','success')}catch(error){toast('Events',error.message,'danger')}})
      document.getElementById('refresh-cache-btn').addEventListener('click',async()=>{try{await loadCache();toast('Cache','Raw cache refreshed','success')}catch(error){toast('Cache',error.message,'danger')}})
      document.getElementById('reload-rp-btn').addEventListener('click',async()=>{try{await loadRpSettings();toast('RP2040','Settings reloaded','success')}catch(error){toast('RP2040',error.message,'danger')}})
      document.getElementById('reload-network-btn').addEventListener('click',async()=>{try{await loadNetworkSettings();toast('Network','Profile reloaded','success')}catch(error){toast('Network',error.message,'danger')}})
      document.getElementById('refresh-pico-ota-btn').addEventListener('click',async()=>{try{await loadPicoOtaStatus();toast('Pico OTA','Status refreshed','success')}catch(error){toast('Pico OTA',error.message,'danger')}})
      document.getElementById('network-form').addEventListener('submit',async(event)=>{event.preventDefault();const note=document.getElementById('network-note');note.textContent='Saving network profile...';try{const data=await expectJson('/api/settings/network',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(event.currentTarget))});note.textContent=data.message||'Network profile updated.';toast('Network',note.textContent,data.ok?'success':'warn');await loadSystem()}catch(error){note.textContent=error.message;toast('Network',error.message,'danger')}})
      document.getElementById('rp-settings-form').addEventListener('submit',async(event)=>{event.preventDefault();const form=event.currentTarget,note=document.getElementById('rp-note');note.textContent='Applying settings over UART...';try{for(const [key,value] of new FormData(form).entries()){if(value==='')continue;const data=await expectJson('/api/settings/rp2040/apply',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({key,value})});if(!data.ok)throw new Error(data.message||('Failed on '+key))}note.textContent='RP2040 settings updated successfully.';toast('RP2040',note.textContent,'success');await loadRpSettings()}catch(error){note.textContent=error.message;toast('RP2040',error.message,'danger')}})
      document.getElementById('ota-form').addEventListener('submit',(event)=>{event.preventDefault();const fileInput=document.getElementById('ota-file'),note=document.getElementById('ota-note'),fill=document.getElementById('ota-fill');if(!fileInput.files||!fileInput.files.length){note.textContent='Choose a firmware binary first.';toast('OTA',note.textContent,'warn');return}const data=new FormData();data.append('update',fileInput.files[0]);fill.style.width='0%';note.textContent='Uploading OTA firmware...';const xhr=new XMLHttpRequest();xhr.open('POST','/api/ota/upload',true);xhr.upload.onprogress=(evt)=>{if(!evt.lengthComputable)return;const percent=clamp((evt.loaded/evt.total)*100,0,100);fill.style.width=percent+'%';note.textContent='Uploading OTA firmware... '+Math.round(percent)+'%'};xhr.onload=async()=>{let payload={};try{payload=xhr.responseText?JSON.parse(xhr.responseText):{}}catch(e){}if(xhr.status>=200&&xhr.status<300&&payload.ok){fill.style.width='100%';note.textContent=payload.message||'OTA upload complete.';toast('OTA',note.textContent,'success')}else{fill.style.width='0%';note.textContent=payload.message||xhr.responseText||('HTTP '+xhr.status);toast('OTA',note.textContent,'danger')}try{await loadSystem()}catch(e){}};xhr.onerror=()=>{fill.style.width='0%';note.textContent='OTA upload failed due to a network error.';toast('OTA',note.textContent,'danger')};xhr.send(data)})
      document.getElementById('pico-ota-form').addEventListener('submit',async(event)=>{event.preventDefault();const fileInput=document.getElementById('pico-ota-file'),note=document.getElementById('pico-ota-note'),fill=document.getElementById('pico-ota-fill');if(!fileInput.files||!fileInput.files.length){note.textContent='Choose a Pico slot binary first.';toast('Pico OTA',note.textContent,'warn');return}try{const status=await expectJson('/api/pico-ota/status');renderPicoOta(status);const dev=status.device||{};if(status.upload_active){note.textContent='Another Pico OTA upload is already in progress.';toast('Pico OTA',note.textContent,'warn');return}if(!dev.safe_ready){note.textContent='Pico is not in OTA SAFE mode. Power-cycle the system while holding DOWN on Pico, then retry.';toast('Pico OTA',note.textContent,'danger');return}}catch(error){note.textContent='Unable to verify Pico OTA readiness: '+error.message;toast('Pico OTA',note.textContent,'danger');return}const file=fileInput.files[0],data=new FormData();data.append('update',file);fill.style.width='0%';note.textContent='Uploading Pico OTA binary...';const xhr=new XMLHttpRequest();xhr.open('POST','/api/pico-ota/upload?size='+encodeURIComponent(file.size||0),true);xhr.upload.onprogress=(evt)=>{if(!evt.lengthComputable)return;const percent=clamp((evt.loaded/evt.total)*100,0,100);fill.style.width=percent+'%';note.textContent='Uploading Pico OTA binary... '+Math.round(percent)+'%'};xhr.onload=async()=>{let payload={};try{payload=xhr.responseText?JSON.parse(xhr.responseText):{}}catch(e){}if(xhr.status>=200&&xhr.status<300&&payload.ok){fill.style.width='100%';note.textContent=payload.message||'Pico OTA upload complete.';toast('Pico OTA',note.textContent,'success')}else{fill.style.width='0%';note.textContent=payload.message||xhr.responseText||('HTTP '+xhr.status);toast('Pico OTA',note.textContent,'danger')}await Promise.allSettled([loadPicoOtaStatus(),loadSystem()])};xhr.onerror=()=>{fill.style.width='0%';note.textContent='Pico OTA upload failed due to a network error.';toast('Pico OTA',note.textContent,'danger')};xhr.send(data)})
    }
    document.addEventListener('DOMContentLoaded',async()=>{bindNavigation();bindForms();setPicoOtaNavVisible(false);const route=normalizeRoute(window.location.pathname);if(window.location.pathname==='/'||!ROUTES[window.location.pathname])window.history.replaceState({},'',route);openRoute(route,false);await Promise.allSettled([refreshCore(true),loadLogs(),loadRpSettings(),loadNetworkSettings(),loadCache(),loadPicoOtaStatus()]);setInterval(()=>{if(state.route==='/pico-ota')Promise.allSettled([loadSystem(),loadPicoOtaStatus()]);else refreshCore(true)},3000);setInterval(()=>{if(state.route==='/events')loadLogs().catch(()=>{});if(state.route==='/system')loadCache().catch(()=>{});if(state.route==='/settings'||state.route==='/system'||state.route==='/pico-ota')loadPicoOtaStatus().catch(()=>{})},10000)})
  </script>
</body>
</html>
)HTML";
