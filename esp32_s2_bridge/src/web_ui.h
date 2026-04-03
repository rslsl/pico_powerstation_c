#pragma once

#include <pgmspace.h>

static const char PSTATION_WEB_UI[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>PowerStation Bridge</title>
  <style>
    :root{--bg:#07131a;--panel:#0d1d27cc;--line:#ffffff14;--text:#ecf8f5;--muted:#93b8bb;--accent:#4ae3bc;--info:#6cb8ff;--warn:#ffb85f;--danger:#ff7b7b;--radius:22px;--shadow:0 20px 60px #0005;--sans:"Bahnschrift","Trebuchet MS","Segoe UI",sans-serif;--mono:"Cascadia Mono","Consolas",monospace}
    *{box-sizing:border-box}html,body{min-height:100%}body{margin:0;font-family:var(--sans);color:var(--text);background:radial-gradient(circle at 12% 18%,#4ae3bc33,transparent 24%),radial-gradient(circle at 88% 8%,#ffb85f22,transparent 18%),linear-gradient(180deg,#051017 0%,#07131a 100%)}
    body:before{content:"";position:fixed;inset:0;pointer-events:none;background-image:linear-gradient(#ffffff05 1px,transparent 1px),linear-gradient(90deg,#ffffff05 1px,transparent 1px);background-size:30px 30px;opacity:.25}
    button,input,select{font:inherit;color:inherit}a{color:inherit;text-decoration:none}
    .sprite{position:absolute;width:0;height:0;overflow:hidden}
    .app{max-width:1540px;margin:0 auto;padding:20px;display:grid;grid-template-columns:280px minmax(0,1fr);gap:20px}
    .side,.panel,.page,.toast{backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px)}
    .side{position:sticky;top:20px;align-self:start;min-height:calc(100vh - 40px);padding:20px;border:1px solid #ffffff12;border-radius:28px;background:linear-gradient(180deg,#091720f0,#0b1a22d0);box-shadow:var(--shadow)}
    .brand{display:grid;gap:12px;margin-bottom:18px}.mark{width:56px;height:56px;border-radius:18px;display:grid;place-items:center;background:linear-gradient(135deg,#4ae3bc42,#4ae3bc12);border:1px solid #4ae3bc50;animation:breathe 5.2s ease-in-out infinite}.mark svg{width:28px;height:28px;color:#a9f7df}
    .eyebrow,.mini label,.badge label,.metric label,.chip label,.field,.kv span{font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:#ecf8f599}
    .brand h1,.head h2{margin:0}.brand p,.subtle,.note{margin:0;color:var(--muted);line-height:1.5}
    .nav{display:grid;gap:10px;margin-bottom:18px}.nav button{width:100%;display:grid;grid-template-columns:42px 1fr;gap:12px;align-items:center;padding:12px 14px;border-radius:18px;border:1px solid #ffffff12;background:#ffffff08;text-align:left;cursor:pointer;transition:.18s}.nav button:hover,.nav button.active{transform:translateY(-1px);border-color:#4ae3bc55;background:linear-gradient(135deg,#4ae3bc24,#4ae3bc08)}
    .nav .ico,.cardico,.miniico,.badgeico{display:grid;place-items:center;border-radius:14px;background:#ffffff0d;border:1px solid #ffffff12;color:#a9f7df}.nav .ico{width:42px;height:42px}.nav strong{display:block;font-size:15px}.nav small{display:block;margin-top:4px;color:var(--muted);font-size:12px}
    .mini{padding:16px;border-radius:20px;background:#ffffff08;border:1px solid #ffffff12}.mini h3,.cardhead h3{margin:0 0 10px;font-size:14px;letter-spacing:.08em;text-transform:uppercase}.stack{display:grid;gap:10px}.row{display:flex;justify-content:space-between;gap:10px;padding:10px 0;border-bottom:1px solid #ffffff10}.row:last-child{border-bottom:0}.row span:first-child{color:var(--muted)}
    .main{min-width:0}.head{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:16px;align-items:end;margin-bottom:18px}.head h2{font-size:clamp(30px,4vw,48px);line-height:.98;letter-spacing:-.03em;margin:6px 0 10px}
    .badges{display:flex;flex-wrap:wrap;gap:10px;justify-content:flex-end}.badge{min-width:160px;display:inline-flex;gap:10px;align-items:center;padding:12px 14px;border-radius:18px;background:#ffffff0a;border:1px solid #ffffff12}.badgeico{width:34px;height:34px;flex:0 0 34px}.badge strong{display:block;font-size:14px}
    .pages{min-height:70vh}.page{display:none;gap:16px;animation:rise .24s ease}.page.active{display:grid}.hero{display:grid;grid-template-columns:minmax(0,1.2fr) minmax(260px,.8fr);gap:18px;padding:20px;border-radius:var(--radius);background:linear-gradient(180deg,#0b1a23ee,#0c1b25d0);border:1px solid #ffffff12;box-shadow:var(--shadow)}
    .panel{padding:18px;border-radius:var(--radius);background:linear-gradient(180deg,#0b1a23ee,#0c1b25d0);border:1px solid #ffffff12;box-shadow:var(--shadow)}
    .grid2,.grid3,.grid4,.metrics,.bars,.forms,.kvgrid,.diag{display:grid;gap:16px}.grid2{grid-template-columns:repeat(2,minmax(0,1fr))}.grid3{grid-template-columns:repeat(3,minmax(0,1fr))}.grid4,.metrics{grid-template-columns:repeat(4,minmax(0,1fr))}.bars{grid-template-columns:repeat(2,minmax(0,1fr))}.forms,.kvgrid{grid-template-columns:repeat(2,minmax(0,1fr))}.diag{grid-template-columns:repeat(3,minmax(0,1fr))}
    .hero h3{margin:8px 0 12px;font-size:clamp(24px,3vw,38px);line-height:1.02;letter-spacing:-.02em}.actions{display:flex;flex-wrap:wrap;gap:10px}
    button,.btnlink{display:inline-flex;align-items:center;justify-content:center;gap:8px;padding:11px 14px;border-radius:16px;border:1px solid #4ae3bc55;background:linear-gradient(135deg,#4ae3bc28,#4ae3bc08);cursor:pointer;transition:.18s;box-shadow:inset 0 1px 0 #ffffff14}button:hover,.btnlink:hover{transform:translateY(-1px)}button.alt,.btnlink.alt{border-color:#ffffff1d;background:#ffffff0a}button.warn,.btnlink.warn{border-color:#ffb85f66;background:linear-gradient(135deg,#ffb85f28,#ffb85f0a)}button.danger,.btnlink.danger{border-color:#ff7b7b66;background:linear-gradient(135deg,#ff7b7b28,#ff7b7b0a)}
    .ringwrap{display:grid;place-items:center;margin-bottom:16px}.ring{--value:0;width:220px;height:220px;padding:14px;border-radius:50%;background:radial-gradient(circle at 50% 30%,#ffffff1f,transparent 40%),conic-gradient(var(--accent) calc(var(--value)*1%),#ffffff10 0);box-shadow:inset 0 0 36px #0005,0 16px 40px #0005;animation:glow 16s linear infinite}.ringcore{height:100%;display:grid;place-items:center;border-radius:50%;background:linear-gradient(180deg,#061017f5,#0b1820f2);border:1px solid #ffffff12;text-align:center}.ringcore strong{display:block;font-size:52px;line-height:.9;letter-spacing:-.04em}.ringcore span{display:block;margin-top:8px;color:var(--muted);font-size:13px;letter-spacing:.08em;text-transform:uppercase}
    .minis{display:grid;gap:12px}.minirow,.metric,.chip,.barbox,.kv,.uploadbox{padding:14px;border-radius:18px;background:#ffffff0a;border:1px solid #ffffff12}.minirow{display:grid;grid-template-columns:46px 1fr;gap:12px;align-items:center}.miniico,.cardico{width:44px;height:44px}.minirow strong,.kv strong,.chip strong{display:block;font-size:20px}
    .cardhead{display:flex;justify-content:space-between;gap:12px;align-items:center;margin-bottom:14px}.cardtitle{display:grid;grid-template-columns:44px 1fr;gap:12px;align-items:center}.metric strong{display:block;margin-top:10px;font-size:clamp(28px,2.4vw,36px);line-height:.95;letter-spacing:-.04em}.metric p{margin:10px 0 0;color:var(--muted);font-size:13px}
    .chart{min-height:180px;border-radius:18px;border:1px solid #ffffff12;background:linear-gradient(180deg,#ffffff06,#ffffff03);padding:10px}.chart svg{display:block;width:100%;height:170px}.empty{min-height:150px;display:grid;place-items:center;text-align:center;color:var(--muted);border:1px dashed #ffffff1c;border-radius:16px;padding:18px}
    .proghead{display:flex;justify-content:space-between;gap:10px;align-items:baseline;margin-bottom:10px}.track,.uptrack,.eventtrack{height:12px;border-radius:999px;overflow:hidden;background:#ffffff12}.fill,.upfill,.eventfill{height:100%;width:0;border-radius:inherit;transition:width .3s ease}.fill{background:linear-gradient(90deg,#4ae3bcc0,#a9f7dff5);box-shadow:0 0 20px #4ae3bc33}.fill.warn{background:linear-gradient(90deg,#ffb85fc0,#ffd89af5)}.fill.danger{background:linear-gradient(90deg,#ff7b7bc0,#ffc0c0f5)}.progfoot{margin-top:8px;display:flex;justify-content:space-between;gap:10px;color:var(--muted);font-size:12px}
    .events{display:grid;gap:12px}.eventrow{display:grid;grid-template-columns:140px minmax(0,1fr) auto;gap:12px;align-items:center;font-size:13px}.eventfill{background:linear-gradient(90deg,#6cb8ff,#4ae3bc);animation:grow .28s ease}
    .tablewrap{overflow:auto;border-radius:18px;border:1px solid #ffffff12;background:#ffffff06}table{width:100%;min-width:700px;border-collapse:collapse;font-size:13px}th,td{text-align:left;padding:12px;border-bottom:1px solid #ffffff10;vertical-align:top}th{position:sticky;top:0;background:#091720f0;font-size:11px;letter-spacing:.12em;text-transform:uppercase;color:#ecf8f599}
    code,.pill,.json{font-family:var(--mono);font-size:12px}.pill{display:inline-flex;align-items:center;padding:4px 8px;border-radius:999px;background:#ffffff12;border:1px solid #ffffff14}
    .json{white-space:pre-wrap;overflow:auto;min-height:220px;max-height:420px;margin:0;padding:14px;border-radius:18px;background:#0003;border:1px solid #ffffff12;line-height:1.55}
    input,select{width:100%;padding:12px 13px;border-radius:14px;border:1px solid #ffffff1f;background:#0003}input:focus,select:focus{outline:none;border-color:#4ae3bc66;box-shadow:0 0 0 4px #4ae3bc1a}
    form{display:grid;gap:16px}.hint{padding:12px 14px;border-radius:16px;background:#ffffff0a;border:1px solid #ffffff12;color:var(--muted);font-size:13px;line-height:1.45}.uptrack{height:14px;border:1px solid #ffffff12}.upfill{background:linear-gradient(90deg,#ffb85f,#ffe0a7)}
    .toaststack{position:fixed;top:18px;right:18px;display:grid;gap:10px;z-index:50;width:min(350px,calc(100vw - 28px))}.toast{padding:14px 16px;border-radius:18px;background:#091720f0;border:1px solid #ffffff14;box-shadow:var(--shadow);animation:toast .24s ease}.toast p{margin:4px 0 0;color:var(--muted);font-size:13px}.toast.success{border-color:#4ae3bc55}.toast.warn{border-color:#ffb85f66}.toast.danger{border-color:#ff7b7b66}
    @keyframes breathe{0%,100%{transform:translateY(0)}50%{transform:translateY(-2px)}}@keyframes rise{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:none}}@keyframes glow{from{filter:hue-rotate(0)}to{filter:hue-rotate(8deg)}}@keyframes grow{from{transform:scaleX(0);transform-origin:left}to{transform:scaleX(1);transform-origin:left}}@keyframes toast{from{opacity:0;transform:translateY(-8px)}to{opacity:1;transform:none}}
    @media (max-width:1280px){.hero,.grid2,.grid3,.grid4,.metrics,.bars,.forms,.kvgrid,.diag{grid-template-columns:1fr}}
    @media (max-width:1080px){.app{grid-template-columns:1fr}.side{position:static;min-height:auto}.nav{grid-template-columns:repeat(5,minmax(0,1fr))}.nav button{grid-template-columns:1fr;justify-items:center;text-align:center;min-height:110px}.badges{justify-content:flex-start}}
    @media (max-width:820px){.app{padding:14px}.head{grid-template-columns:1fr}.nav{grid-template-columns:repeat(2,minmax(0,1fr))}.badge{width:100%;min-width:0}.eventrow{grid-template-columns:1fr}.ring{width:184px;height:184px}.ringcore strong{font-size:42px}}
  </style>
</head>
<body>
  <svg class="sprite" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">
    <symbol id="i-home" viewBox="0 0 24 24"><path fill="currentColor" d="M3 13h8V3H3zm10 8h8v-8h-8zM3 21h8v-6H3zm10-10h8V3h-8z"/></symbol>
    <symbol id="i-act" viewBox="0 0 24 24"><path fill="currentColor" d="M4 13h4l2-4 4 8 2-4h4v2h-5.2L14 19l-4-8-1.2 2.4H4z"/></symbol>
    <symbol id="i-log" viewBox="0 0 24 24"><path fill="currentColor" d="M6 4h10l4 4v12H6zm9 1.5V9h3.5zM8 12h10v1.8H8zm0 3.8h10v1.8H8z"/></symbol>
    <symbol id="i-set" viewBox="0 0 24 24"><path fill="currentColor" d="m12 7.5 1-2.8 2.6.8.2 3 2.2 1.2 2.4-1.4 1.3 2.3-2.1 1.9v2.6l2.1 1.9-1.3 2.3-2.4-1.4-2.2 1.2-.2 3-2.6.8-1-2.8h-2l-1 2.8-2.6-.8-.2-3-2.2-1.2-2.4 1.4-1.3-2.3 2.1-1.9v-2.6L1 10.6l1.3-2.3 2.4 1.4 2.2-1.2.2-3 2.6-.8 1 2.8zm0 2.4A2.1 2.1 0 1 0 12 14a2.1 2.1 0 0 0 0-4.1"/></symbol>
    <symbol id="i-sys" viewBox="0 0 24 24"><path fill="currentColor" d="M3 5h18v10H3zm2 2v6h14V7zm4 10h6v2H9z"/></symbol>
    <symbol id="i-bolt" viewBox="0 0 24 24"><path fill="currentColor" d="M13 2 6 13h5l-1 9 8-12h-5l0-8z"/></symbol>
    <symbol id="i-bat" viewBox="0 0 24 24"><path fill="currentColor" d="M5 7h13v10H5z"/><path fill="currentColor" d="M19 10h2v4h-2z"/><path fill="#07131a" d="M7 9h9v6H7z"/></symbol>
    <symbol id="i-temp" viewBox="0 0 24 24"><path fill="currentColor" d="M10 4a2 2 0 1 1 4 0v8.4a4 4 0 1 1-4 0zm2 1.5a.5.5 0 0 0-.5.5v7.2l-.4.3a2.5 2.5 0 1 0 1.8 0l-.4-.3V6a.5.5 0 0 0-.5-.5"/></symbol>
    <symbol id="i-wifi" viewBox="0 0 24 24"><path fill="currentColor" d="M12 18.5a1.5 1.5 0 1 0 0 3 1.5 1.5 0 0 0 0-3M12 13a6 6 0 0 1 4.2 1.7l1.4-1.4A8 8 0 0 0 6.4 13.3l1.4 1.4A6 6 0 0 1 12 13m0-4.5a10.5 10.5 0 0 1 7.4 3l1.4-1.4A12.5 12.5 0 0 0 3.2 10l1.4 1.4a10.5 10.5 0 0 1 7.4-2.9"/></symbol>
    <symbol id="i-exp" viewBox="0 0 24 24"><path fill="currentColor" d="M12 3l4 4h-3v6h-2V7H8zm-7 9h2v6h10v-6h2v8H5z"/></symbol>
    <symbol id="i-up" viewBox="0 0 24 24"><path fill="currentColor" d="M12 3 7 8h3v6h4V8h3zm-7 13h14v5H5z"/></symbol>
    <symbol id="i-shield" viewBox="0 0 24 24"><path fill="currentColor" d="M12 2 4 5v6c0 5.3 3.4 10 8 11 4.6-1 8-5.7 8-11V5zm-1 6h2v5h-2zm0 7h2v2h-2z"/></symbol>
    <symbol id="i-chip" viewBox="0 0 24 24"><path fill="currentColor" d="M7 7h10v10H7zM4 9h2v2H4zm0 4h2v2H4zm14-4h2v2h-2zm0 4h2v2h-2zM9 4h2v2H9zm4 0h2v2h-2zM9 18h2v2H9zm4 0h2v2h-2z"/></symbol>
    <symbol id="i-clock" viewBox="0 0 24 24"><path fill="currentColor" d="M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2m1 5v5.4l3.2 1.9-1 1.7L11 13V7z"/></symbol>
  </svg>
  <div class="toaststack" id="toast-stack"></div>
  <div class="app">
    <aside class="side">
      <div class="brand">
        <div class="mark"><svg><use href="#i-bolt"></use></svg></div>
        <div>
          <p class="eyebrow">ESP32-S2 Control Layer</p>
          <h1>PowerStation Bridge</h1>
          <p>Structured console for telemetry, history, OTA, and diagnostics.</p>
        </div>
      </div>
      <nav class="nav">
        <button class="active" type="button" data-route="/dashboard"><span class="ico"><svg><use href="#i-home"></use></svg></span><span><strong>Dashboard</strong><small>Mission control overview</small></span></button>
        <button type="button" data-route="/telemetry"><span class="ico"><svg><use href="#i-act"></use></svg></span><span><strong>Telemetry</strong><small>Live battery and power</small></span></button>
        <button type="button" data-route="/events"><span class="ico"><svg><use href="#i-log"></use></svg></span><span><strong>Events</strong><small>Recent logs and exports</small></span></button>
        <button type="button" data-route="/settings"><span class="ico"><svg><use href="#i-set"></use></svg></span><span><strong>Settings</strong><small>RP2040, Wi-Fi, OTA</small></span></button>
        <button type="button" data-route="/system"><span class="ico"><svg><use href="#i-sys"></use></svg></span><span><strong>System</strong><small>Bridge health and cache</small></span></button>
      </nav>
      <div class="stack">
        <div class="mini">
          <h3>Live Pulse</h3>
          <div class="stack">
            <div class="row"><span>Battery</span><strong id="sidebar-soc">--</strong></div>
            <div class="row"><span>Power</span><strong id="sidebar-power">--</strong></div>
            <div class="row"><span>Runtime</span><strong id="sidebar-runtime">--</strong></div>
          </div>
        </div>
        <div class="mini">
          <h3>Bridge Snapshot</h3>
          <div class="stack">
            <div class="row"><span>RP2040 Mode</span><strong id="sys-rp-mode">--</strong></div>
            <div class="row"><span>Wi-Fi Mode</span><strong id="sys-wifi-mode">--</strong></div>
            <div class="row"><span>UART Link</span><strong id="sys-link">--</strong></div>
            <div class="row"><span>Hostname</span><strong id="sys-hostname">--</strong></div>
          </div>
        </div>
        <div class="mini">
          <h3>Last Sync</h3>
          <p class="note" id="last-sync">Booting bridge and waiting for telemetry...</p>
          <p class="subtle" style="margin-top:10px" id="route-indicator">Route: /dashboard</p>
        </div>
      </div>
    </aside>
    <main class="main">
      <header class="head">
        <div>
          <p class="eyebrow" id="page-kicker">Structured navigation</p>
          <h2 id="page-title">Mission Control</h2>
          <p class="note" id="page-summary">High-level station health, quick actions, and animated live visuals for the bridge.</p>
        </div>
        <div class="badges">
          <div class="badge"><span class="badgeico"><svg><use href="#i-chip"></use></svg></span><div><label>Mode</label><strong id="badge-mode-text">--</strong></div></div>
          <div class="badge"><span class="badgeico"><svg><use href="#i-act"></use></svg></span><div><label>UART Link</label><strong id="badge-link-text">WAIT</strong></div></div>
          <div class="badge"><span class="badgeico"><svg><use href="#i-wifi"></use></svg></span><div><label>Network</label><strong id="badge-net-text">--</strong></div></div>
          <div class="badge"><span class="badgeico"><svg><use href="#i-sys"></use></svg></span><div><label>Status</label><strong id="badge-status-text">Booting</strong></div></div>
        </div>
      </header>
      <div class="pages">
        <section class="page active" data-page="/dashboard">
          <div class="hero">
            <div>
              <p class="eyebrow">Power orchestration</p>
              <h3>Multi-page mission console with bars, charts, icons, and responsive motion.</h3>
              <p class="note" id="telemetry-mode">Waiting for RP2040 telemetry...</p>
              <div class="actions" style="margin-top:18px">
                <button type="button" data-go="/telemetry"><svg><use href="#i-act"></use></svg>Open Telemetry</button>
                <button type="button" class="alt" data-go="/settings"><svg><use href="#i-set"></use></svg>Tune Settings</button>
                <button type="button" class="alt" id="refresh-all-btn"><svg><use href="#i-clock"></use></svg>Sync Now</button>
              </div>
              <div class="grid3" style="margin-top:18px">
                <div class="chip"><label>Available</label><strong id="available-wh">--</strong></div>
                <div class="chip"><label>Runtime</label><strong id="runtime">--</strong></div>
                <div class="chip"><label>Current Power</label><strong id="power">--</strong></div>
              </div>
            </div>
            <div>
              <div class="ringwrap"><div class="ring" id="soc-ring" style="--value:0"><div class="ringcore"><div><strong id="soc-ring-value">--</strong><span>Battery SOC</span></div></div></div></div>
              <div class="minis">
                <div class="minirow"><div class="miniico"><svg><use href="#i-bat"></use></svg></div><div><label>Voltage</label><strong id="voltage">--</strong></div></div>
                <div class="minirow"><div class="miniico"><svg><use href="#i-bolt"></use></svg></div><div><label>Current</label><strong id="current">--</strong></div></div>
                <div class="minirow"><div class="miniico"><svg><use href="#i-temp"></use></svg></div><div><label>Battery Temp</label><strong id="tbat">--</strong></div></div>
              </div>
            </div>
          </div>
          <div class="grid2">
            <article class="panel">
              <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-shield"></use></svg></div><div><h3>Safety Bars</h3><p class="subtle">Charge, thermal, and bridge readiness</p></div></div></div>
              <div class="stack">
                <div class="barbox">
                  <div class="proghead"><span class="eyebrow" style="color:#ecf8f599">Battery capacity</span><strong id="soc-progress-label">--</strong></div>
                  <div class="track"><div class="fill" id="soc-progress-fill"></div></div>
                  <div class="progfoot"><span>0%</span><span id="soc-progress-footer">Tracking live SOC</span><span>100%</span></div>
                </div>
                <div class="barbox">
                  <div class="proghead"><span class="eyebrow" style="color:#ecf8f599">Thermal load</span><strong id="thermal-progress-label">--</strong></div>
                  <div class="track"><div class="fill" id="thermal-progress-fill"></div></div>
                  <div class="progfoot"><span>Cool</span><span id="thermal-progress-footer">Battery and system temps</span><span>Critical</span></div>
                </div>
                <div class="barbox">
                  <div class="proghead"><span class="eyebrow" style="color:#ecf8f599">Bridge readiness</span><strong id="link-progress-label">--</strong></div>
                  <div class="track"><div class="fill" id="link-progress-fill"></div></div>
                  <div class="progfoot"><span>Offline</span><span id="link-progress-footer">UART and network state</span><span>Online</span></div>
                </div>
              </div>
            </article>
            <article class="panel">
              <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-sys"></use></svg></div><div><h3>Activity Feed</h3><p class="subtle">Recent UI and bridge events</p></div></div></div>
              <div class="stack" id="activity-feed"><div class="empty">Activity log will appear here after the first refresh.</div></div>
            </article>
          </div>
          <div class="grid4">
            <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-bat"></use></svg></div><div><h3>Charge Trend</h3><p class="subtle" id="soc-chart-caption">Collecting recent SOC samples</p></div></div></div><div class="chart" id="chart-soc"></div></article>
            <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-bolt"></use></svg></div><div><h3>Power Pulse</h3><p class="subtle" id="power-chart-caption">Live load and charge activity</p></div></div></div><div class="chart" id="chart-power"></div></article>
            <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-temp"></use></svg></div><div><h3>Thermal Sweep</h3><p class="subtle">Battery vs system temperature</p></div></div></div><div class="chart" id="chart-thermal"></div></article>
            <article class="panel">
              <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-exp"></use></svg></div><div><h3>Fast Exports</h3><p class="subtle">Quick downloads and routing</p></div></div></div>
              <div class="actions">
                <a class="btnlink alt" href="/export/stats.json"><svg><use href="#i-exp"></use></svg>Stats JSON</a>
                <a class="btnlink alt" href="/export/stats.csv"><svg><use href="#i-exp"></use></svg>Stats CSV</a>
                <a class="btnlink alt" href="/export/log.json"><svg><use href="#i-exp"></use></svg>Log JSON</a>
                <a class="btnlink alt" href="/export/log.csv"><svg><use href="#i-exp"></use></svg>Log CSV</a>
                <button type="button" class="warn" data-go="/events"><svg><use href="#i-log"></use></svg>Inspect Events</button>
              </div>
              <div class="hint" style="margin-top:16px">Use exports for large histories. Live pages stay fast by sampling only recent bridge data.</div>
            </article>
          </div>
        </section>
        <section class="page" data-page="/telemetry">
          <div class="metrics">
            <article class="metric"><label>SOC</label><strong id="soc">--</strong><p>Primary battery fill level from RP2040 telemetry.</p></article>
            <article class="metric"><label>Power</label><strong id="telemetry-power">--</strong><p>Instant pack power for spike and charge transition tracking.</p></article>
            <article class="metric"><label>Battery Temp</label><strong id="telemetry-tbat">--</strong><p>Battery thermal envelope for protection monitoring.</p></article>
            <article class="metric"><label>System Temp</label><strong id="tsys">--</strong><p>Internal system or inverter temperature from RP2040.</p></article>
            <article class="metric"><label>Voltage</label><strong id="telemetry-voltage">--</strong><p>Pack voltage with 2-decimal precision.</p></article>
            <article class="metric"><label>Current</label><strong id="telemetry-current">--</strong><p>Charge or discharge current from live telemetry.</p></article>
            <article class="metric"><label>Available Energy</label><strong id="telemetry-available">--</strong><p>Estimated usable energy remaining in Wh.</p></article>
            <article class="metric"><label>Remaining Runtime</label><strong id="telemetry-runtime">--</strong><p>Derived time remaining under current conditions.</p></article>
          </div>
          <div class="grid3">
            <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-act"></use></svg></div><div><h3>Telemetry Wave</h3><p class="subtle">SOC trail with route-preserved history</p></div></div></div><div class="chart" id="telemetry-chart-soc"></div></article>
            <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-bolt"></use></svg></div><div><h3>Load Swing</h3><p class="subtle">Power history with polarity awareness</p></div></div></div><div class="chart" id="telemetry-chart-power"></div></article>
            <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-temp"></use></svg></div><div><h3>Thermal Overlay</h3><p class="subtle">Battery and system temperatures in tandem</p></div></div></div><div class="chart" id="telemetry-chart-thermal"></div></article>
          </div>
          <div class="bars">
            <article class="panel">
              <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-shield"></use></svg></div><div><h3>Operating Bars</h3><p class="subtle">Fast visual understanding of live limits</p></div></div></div>
              <div class="stack">
                <div class="barbox"><div class="proghead"><span class="eyebrow" style="color:#ecf8f599">Voltage window</span><strong id="bar-voltage-label">--</strong></div><div class="track"><div class="fill" id="bar-voltage-fill"></div></div><div class="progfoot"><span>0 V</span><span id="bar-voltage-foot">Target pack window</span><span>60 V</span></div></div>
                <div class="barbox"><div class="proghead"><span class="eyebrow" style="color:#ecf8f599">Current activity</span><strong id="bar-current-label">--</strong></div><div class="track"><div class="fill" id="bar-current-fill"></div></div><div class="progfoot"><span>Idle</span><span id="bar-current-foot">Normalized to 50 A</span><span>Busy</span></div></div>
                <div class="barbox"><div class="proghead"><span class="eyebrow" style="color:#ecf8f599">Temperature headroom</span><strong id="bar-temp-label">--</strong></div><div class="track"><div class="fill" id="bar-temp-fill"></div></div><div class="progfoot"><span>Ambient</span><span id="bar-temp-foot">Normalized to 80 C</span><span>Critical</span></div></div>
              </div>
            </article>
            <article class="panel">
              <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-chip"></use></svg></div><div><h3>Drive State</h3><p class="subtle">Interpreted operating mode and charge context</p></div></div></div>
              <div class="kvgrid">
                <div class="kv"><span>Reported Mode</span><strong id="telemetry-mode-flag">--</strong></div>
                <div class="kv"><span>Charging</span><strong id="telemetry-charge-flag">--</strong></div>
                <div class="kv"><span>Bridge Status</span><strong id="telemetry-status-flag">--</strong></div>
                <div class="kv"><span>Network Snapshot</span><strong id="telemetry-network-flag">--</strong></div>
              </div>
              <div class="hint" style="margin-top:16px">Charts build up locally in the browser, so the interface feels richer without increasing ESP-side storage.</div>
            </article>
          </div>
        </section>
        <section class="page" data-page="/events">
          <div class="grid2">
            <article class="panel">
              <div class="cardhead">
                <div class="cardtitle"><div class="cardico"><svg><use href="#i-log"></use></svg></div><div><h3>Recent Events</h3><p class="subtle" id="events-summary">Slice of the most recent log records.</p></div></div>
                <div class="actions">
                  <button type="button" id="refresh-log-btn"><svg><use href="#i-clock"></use></svg>Refresh Slice</button>
                  <a class="btnlink alt" href="/export/log.json"><svg><use href="#i-exp"></use></svg>JSON</a>
                  <a class="btnlink alt" href="/export/log.csv"><svg><use href="#i-exp"></use></svg>CSV</a>
                  <button type="button" class="danger" onclick="bridgeAction('LOG_RESET')"><svg><use href="#i-log"></use></svg>Clear Log</button>
                  <button type="button" class="warn" onclick="bridgeAction('STATS_RESET')"><svg><use href="#i-exp"></use></svg>Reset Stats</button>
                </div>
              </div>
              <div class="diag">
                <div class="chip"><label>Total Logged</label><strong id="log-total">--</strong></div>
                <div class="chip"><label>Slice Count</label><strong id="log-count">--</strong></div>
                <div class="chip"><label>Dominant Kind</label><strong id="log-dominant">--</strong></div>
              </div>
              <div class="tablewrap" style="margin-top:16px">
                <table>
                  <thead><tr><th>Idx</th><th>Type</th><th>SOC</th><th>Voltage</th><th>Current</th><th>Param</th></tr></thead>
                  <tbody id="log-body"><tr><td colspan="6" class="subtle">No log slice loaded yet.</td></tr></tbody>
                </table>
              </div>
            </article>
            <div class="grid2" style="grid-template-columns:1fr">
              <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-home"></use></svg></div><div><h3>Event Spectrum</h3><p class="subtle">Live distribution for the fetched slice</p></div></div></div><div class="events" id="event-bars"><div class="empty">Load a log slice to see event distribution bars.</div></div></article>
              <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-act"></use></svg></div><div><h3>Action Lane</h3><p class="subtle">Quick bridge tasks for operators</p></div></div></div><div class="actions"><button type="button" id="sync-events-btn"><svg><use href="#i-clock"></use></svg>Sync All APIs</button><button type="button" class="alt" data-go="/system"><svg><use href="#i-sys"></use></svg>Open Diagnostics</button><button type="button" class="alt" data-go="/settings"><svg><use href="#i-set"></use></svg>Open Settings</button></div><div class="hint" style="margin-top:16px">The live slice stays intentionally small. Use export buttons when you need full history instead of the recent window.</div></article>
            </div>
          </div>
        </section>
        <section class="page" data-page="/settings">
          <div class="grid2">
            <article class="panel">
              <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-set"></use></svg></div><div><h3>RP2040 Settings</h3><p class="subtle">Protected thresholds and audio profile</p></div></div></div>
              <form id="rp-settings-form">
                <div class="forms">
                  <label class="field">Capacity Ah<input name="capacity_ah" step="0.1"></label>
                  <label class="field">Pack Warn V<input name="vbat_warn_v" step="0.1"></label>
                  <label class="field">Pack Cut V<input name="vbat_cut_v" step="0.1"></label>
                  <label class="field">Cell Warn V<input name="cell_warn_v" step="0.01"></label>
                  <label class="field">Cell Cut V<input name="cell_cut_v" step="0.01"></label>
                  <label class="field">Bat Warn C<input name="temp_bat_warn_c" step="1"></label>
                  <label class="field">Bat Cut C<input name="temp_bat_cut_c" step="1"></label>
                  <label class="field">Sound<select name="sound"><option value="full">Full</option><option value="minimal">Minimal</option><option value="silent">Silent</option></select></label>
                </div>
                <div class="actions"><button type="submit"><svg><use href="#i-set"></use></svg>Apply To RP2040</button><button type="button" class="alt" id="reload-rp-settings"><svg><use href="#i-clock"></use></svg>Reload</button></div>
                <div class="hint" id="rp-settings-note">Changes are sent over UART using the PowerStation settings sanitizer.</div>
              </form>
            </article>
            <article class="panel">
              <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-wifi"></use></svg></div><div><h3>Wi-Fi / Host Profile</h3><p class="subtle">AP, STA, hostname, and OTA credential storage</p></div></div></div>
              <form id="network-form">
                <div class="forms">
                  <label class="field">Wi-Fi Mode<select name="wifi_mode"><option value="ap">AP</option><option value="sta">STA</option><option value="ap_sta">AP + STA</option></select></label>
                  <label class="field">Hostname<input name="hostname"></label>
                  <label class="field">AP SSID<input name="ap_ssid"></label>
                  <label class="field">AP Password<input name="ap_password" placeholder="Leave blank to keep"></label>
                  <label class="field">STA SSID<input name="sta_ssid"></label>
                  <label class="field">STA Password<input name="sta_password" placeholder="Leave blank to keep"></label>
                  <label class="field" style="grid-column:1 / -1">OTA Password<input name="ota_password" placeholder="Leave blank to keep"></label>
                </div>
                <div class="actions"><button type="submit"><svg><use href="#i-wifi"></use></svg>Save Network Profile</button><button type="button" class="alt" id="reload-network-settings"><svg><use href="#i-clock"></use></svg>Reload</button></div>
                <div class="hint" id="network-note">AP and STA settings are stored inside ESP preferences. Empty password fields keep the current stored value.</div>
              </form>
            </article>
          </div>
          <article class="panel">
            <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-up"></use></svg></div><div><h3>OTA Upload Deck</h3><p class="subtle">Browser upload with progress bar and mode guard</p></div></div></div>
            <div class="grid2">
              <form id="ota-form">
                <label class="field">Firmware Binary<input type="file" name="update" id="ota-file"></label>
                <div class="actions"><button type="submit" class="warn"><svg><use href="#i-up"></use></svg>Upload OTA Binary</button></div>
                <div class="uptrack"><div class="upfill" id="ota-progress-fill"></div></div>
                <div class="hint" id="ota-note">Select a firmware binary and switch the RP2040 bridge into OTA mode before uploading.</div>
              </form>
              <div class="stack">
                <div class="kvgrid">
                  <div class="kv"><span>RP2040 Mode</span><strong id="ota-mode">--</strong></div>
                  <div class="kv"><span>Network Route</span><strong id="ota-network">--</strong></div>
                </div>
                <div class="hint" id="ota-hint">When OTA is active, progress is echoed back to the PowerStation via UART STATUS messages.</div>
              </div>
            </div>
          </article>
        </section>
        <section class="page" data-page="/system">
          <div class="grid2">
            <article class="panel">
              <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-sys"></use></svg></div><div><h3>Bridge Diagnostics</h3><p class="subtle">ESP32-S2 link state, IPs, cached services, and latest status</p></div></div></div>
              <div class="kvgrid">
                <div class="kv"><span>AP Address</span><strong id="sys-ap-ip">--</strong></div>
                <div class="kv"><span>STA Address</span><strong id="sys-sta-ip">--</strong></div>
                <div class="kv"><span>Status</span><strong id="sys-status">--</strong></div>
                <div class="kv"><span>RP2040 Mode</span><strong id="sys-rp-mode-system">--</strong></div>
                <div class="kv"><span>Link Detail</span><strong id="system-link-detail">--</strong></div>
                <div class="kv"><span>Active Route</span><strong id="system-route">--</strong></div>
              </div>
              <div class="diag" style="margin-top:16px">
                <div class="chip"><label>Hello Frames</label><strong id="diag-hello">--</strong></div>
                <div class="chip"><label>Telemetry Frames</label><strong id="diag-telemetry">--</strong></div>
                <div class="chip"><label>Stats Frames</label><strong id="diag-stats">--</strong></div>
                <div class="chip"><label>Settings Frames</label><strong id="diag-settings">--</strong></div>
                <div class="chip"><label>ACK Frames</label><strong id="diag-ack">--</strong></div>
                <div class="chip"><label>Error Frames</label><strong id="diag-error">--</strong></div>
              </div>
            </article>
            <article class="panel">
              <div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-log"></use></svg></div><div><h3>Response Cache</h3><p class="subtle">Raw cached JSON snapshots for debugging</p></div></div><div class="actions"><button type="button" class="alt" id="refresh-cache-btn"><svg><use href="#i-clock"></use></svg>Refresh Raw JSON</button></div></div>
              <pre class="json" id="raw-cache">Waiting...</pre>
            </article>
          </div>
          <div class="grid2">
            <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-chip"></use></svg></div><div><h3>Last Bridge Reply</h3><p class="subtle">Latest ACK and error cache entries</p></div></div></div><div class="json" id="system-last-reply">No ACK/ERROR payload cached yet.</div></article>
            <article class="panel"><div class="cardhead"><div class="cardtitle"><div class="cardico"><svg><use href="#i-act"></use></svg></div><div><h3>Operator Feed</h3><p class="subtle">Same live activity feed, anchored in diagnostics</p></div></div></div><div class="stack" id="system-activity-feed"><div class="empty">Waiting for activity entries...</div></div></article>
          </div>
        </section>
      </div>
    </main>
  </div>
  <script>
const ROUTES={
  "/dashboard":{title:"Mission Control",kicker:"Structured navigation",summary:"High-level station health, quick actions, and animated live visuals for the bridge."},
  "/telemetry":{title:"Live Telemetry",kicker:"Operational data",summary:"Streaming pack metrics, history charts, and normalized power bars for fast decisions."},
  "/events":{title:"Events & History",kicker:"Operational history",summary:"Recent log slices, distribution bars, and export shortcuts for deeper investigations."},
  "/settings":{title:"Configuration",kicker:"Control surfaces",summary:"RP2040 thresholds, Wi-Fi profile, and OTA upload workflow in dedicated panels."},
  "/system":{title:"Bridge Diagnostics",kicker:"ESP32-S2 internals",summary:"Bridge status, cache inspection, frame counters, and service-level diagnostics."}
};
const state={route:"/dashboard",history:{soc:[],power:[],tempBat:[],tempSys:[]},lastSystem:null,lastTelemetry:null,lastStats:null,lastSettings:null,lastCache:null,lastLog:null,activity:[]};
async function expectJson(url,options){const r=await fetch(url,options);const t=await r.text();if(!r.ok)throw new Error(t||r.statusText);return t?JSON.parse(t):{}}
function setText(id,value){const el=document.getElementById(id);if(el)el.textContent=value??"--"}
function clamp(v,min,max){return Math.min(max,Math.max(min,v))}
function fixed(v,d=1,s=""){if(v===undefined||v===null||v==="")return"--";const n=Number(v);return Number.isNaN(n)?v:`${n.toFixed(d)}${s}`}
function minutesToLabel(v){const m=Number(v);if(!Number.isFinite(m)||m<0)return"--";const h=Math.floor(m/60);const mm=m%60;return h>0?`${h}h ${mm}m`:`${mm}m`}
function nowLabel(){return new Date().toLocaleTimeString([],{hour:"2-digit",minute:"2-digit",second:"2-digit"})}
function normalizeRoute(path){const clean=(path||"/").replace(/\/+$/,"")||"/";return ROUTES[clean]?clean:"/dashboard"}
function pushHistory(name,value){const n=Number(value);if(!Number.isFinite(n)||!state.history[name])return;state.history[name].push(n);if(state.history[name].length>28)state.history[name].shift()}
function updateHeader(route){const meta=ROUTES[route]||ROUTES["/dashboard"];setText("page-title",meta.title);setText("page-kicker",meta.kicker);setText("page-summary",meta.summary);setText("route-indicator",`Route: ${route}`);setText("system-route",route)}
function openRoute(route,push=true){const r=normalizeRoute(route);state.route=r;updateHeader(r);document.querySelectorAll(".page").forEach(p=>p.classList.toggle("active",p.dataset.page===r));document.querySelectorAll(".nav button").forEach(b=>b.classList.toggle("active",b.dataset.route===r));if(push&&normalizeRoute(location.pathname)!==r)history.pushState({}, "", r);maybeRefreshRoute(r)}
function pushActivity(title,detail,tone="info"){const top=state.activity[0];if(top&&top.title===title&&top.detail===detail)return;state.activity.unshift({title,detail,tone,time:nowLabel()});state.activity=state.activity.slice(0,10);renderFeed("activity-feed");renderFeed("system-activity-feed")}
function renderFeed(id){const el=document.getElementById(id);if(!el)return;if(!state.activity.length){el.innerHTML='<div class="empty">Activity log will appear here after the first refresh.</div>';return}el.innerHTML=state.activity.map(x=>`<div class="row"><span>${x.title}</span><div style="text-align:right"><strong>${x.time}</strong><div class="subtle">${x.detail}</div></div></div>`).join("")}
function toast(title,message,tone="success"){const stack=document.getElementById("toast-stack");const el=document.createElement("div");el.className=`toast ${tone}`;el.innerHTML=`<strong>${title}</strong><p>${message}</p>`;stack.prepend(el);setTimeout(()=>{el.style.opacity="0";el.style.transform="translateY(-6px)"},3400);setTimeout(()=>el.remove(),4000)}
function reportError(title,error){const message=error?.message||String(error);toast(title,message,"danger");pushActivity(`${title} failed`,message,"danger")}
function setProgress(id,percent,tone=""){const el=document.getElementById(id);if(!el)return;el.style.width=`${clamp(percent||0,0,100)}%`;el.classList.remove("warn","danger");if(tone==="warn")el.classList.add("warn");if(tone==="danger")el.classList.add("danger")}
function setRing(percent,label){const p=clamp(Number(percent)||0,0,100);const el=document.getElementById("soc-ring");if(el)el.style.setProperty("--value",p);setText("soc-ring-value",label||`${Math.round(p)}%`)}
function updateLastSync(label){setText("last-sync",`${label} at ${nowLabel()}`)}
function sparkline(id,values,opt={}){const el=document.getElementById(id);if(!el)return;if(!values||values.length<2){el.innerHTML='<div class="empty">Collecting samples for this chart...</div>';return}const w=360,h=170,p=10,min=opt.min!==undefined?opt.min:Math.min(...values),max=opt.max!==undefined?opt.max:Math.max(...values),span=max-min||1;const pts=values.map((v,i)=>{const x=p+i*(w-p*2)/Math.max(values.length-1,1);const y=h-p-((v-min)/span)*(h-p*2);return`${x.toFixed(1)},${y.toFixed(1)}`}).join(" ");const area=`${p},${h-p} ${pts} ${w-p},${h-p}`;const last=values[values.length-1],lx=p+(values.length-1)*(w-p*2)/Math.max(values.length-1,1),ly=h-p-((last-min)/span)*(h-p*2);const uid=id.replace(/[^a-z0-9]/gi,""),c=opt.color||"#4ae3bc",f=opt.fill||"#4ae3bc18";el.innerHTML=`<svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none"><defs><linearGradient id="g-${uid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="${c}" stop-opacity=".34"></stop><stop offset="100%" stop-color="${c}" stop-opacity=".02"></stop></linearGradient></defs><line x1="${p}" y1="${h-p}" x2="${w-p}" y2="${h-p}" stroke="#ffffff12"></line><polygon points="${area}" fill="url(#g-${uid})"></polygon><polyline points="${pts}" fill="none" stroke="${c}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"></polyline><circle cx="${lx}" cy="${ly}" r="5" fill="${c}" stroke="#07131a" stroke-width="2"></circle></svg>`}
function dualSparkline(id,a,b,c1="#ffb85f",c2="#6cb8ff"){const el=document.getElementById(id);if(!el)return;if(!a||!b||a.length<2||b.length<2){el.innerHTML='<div class="empty">Collecting paired temperature samples...</div>';return}const w=360,h=170,p=10,all=[...a,...b],min=Math.min(...all),max=Math.max(...all),span=max-min||1,proj=arr=>arr.map((v,i)=>{const x=p+i*(w-p*2)/Math.max(arr.length-1,1);const y=h-p-((v-min)/span)*(h-p*2);return`${x.toFixed(1)},${y.toFixed(1)}`}).join(" ");el.innerHTML=`<svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none"><line x1="${p}" y1="${h-p}" x2="${w-p}" y2="${h-p}" stroke="#ffffff12"></line><polyline points="${proj(a)}" fill="none" stroke="${c1}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"></polyline><polyline points="${proj(b)}" fill="none" stroke="${c2}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"></polyline></svg>`}
function renderLogs(payload){const prev=state.lastLog;state.lastLog=payload;setText("log-total",payload.total??"--");setText("log-count",payload.count??"--");const body=document.getElementById("log-body");if(!payload.events||!payload.events.length){body.innerHTML='<tr><td colspan="6" class="subtle">No events in selected slice.</td></tr>';document.getElementById("event-bars").innerHTML='<div class="empty">No events in the selected slice.</div>';setText("log-dominant","--");setText("events-summary","No events were returned for the requested slice.");return}body.innerHTML=payload.events.map(ev=>`<tr><td>${ev.idx??""}</td><td><span class="pill">${ev.kind??""}</span></td><td>${ev.soc_pct??""}%</td><td>${ev.voltage_v??""}</td><td>${ev.current_a??""}</td><td>${ev.param??""}</td></tr>`).join("");const counts=payload.events.reduce((a,ev)=>{const k=ev.kind||"unknown";a[k]=(a[k]||0)+1;return a},{});const sorted=Object.entries(counts).sort((a,b)=>b[1]-a[1]);const max=sorted[0][1];document.getElementById("event-bars").innerHTML=sorted.map(([k,c])=>`<div class="eventrow"><strong>${k}</strong><div class="eventtrack"><div class="eventfill" style="width:${c/max*100}%"></div></div><span>${c}</span></div>`).join("");setText("log-dominant",sorted[0][0]);setText("events-summary",`Showing ${payload.count} event(s) starting from index ${payload.start}.`);if(!prev||prev.total!==payload.total||prev.count!==payload.count)pushActivity("Log slice refreshed",`Received ${payload.count} event(s) from bridge log cache`)}
function renderSystem(data){const prev=state.lastSystem;state.lastSystem=data;setText("badge-mode-text",data.rp_mode||"--");setText("badge-link-text",data.link_up?"ONLINE":"WAIT");setText("badge-net-text",`${data.wifi_mode||"--"} ${data.ap_ip||data.sta_ip||""}`.trim());setText("badge-status-text",data.status||"--");setText("sys-rp-mode",data.rp_mode||"--");setText("sys-rp-mode-system",data.rp_mode||"--");setText("sys-wifi-mode",data.wifi_mode||"--");setText("sys-ap-ip",data.ap_ip||"--");setText("sys-sta-ip",data.sta_ip||"--");setText("sys-hostname",data.hostname||"--");setText("sys-link",data.link_up?"ONLINE":"WAITING");setText("sys-status",data.status||"--");setText("system-link-detail",data.link_up?"UART telemetry alive":"Awaiting bridge frames");setText("telemetry-status-flag",data.status||"--");setText("telemetry-network-flag",`${data.wifi_mode||"--"} / ${data.ap_ip||data.sta_ip||"--"}`);setText("ota-mode",data.rp_mode||"--");setText("ota-network",data.ap_ip||data.sta_ip||"--");setText("diag-hello",data.hello_counter??"--");setText("diag-telemetry",data.telemetry_counter??"--");setText("diag-stats",data.stats_counter??"--");setText("diag-settings",data.settings_counter??"--");setText("diag-ack",data.ack_counter??"--");setText("diag-error",data.error_counter??"--");setText("link-progress-label",data.link_up?"ONLINE":"WAITING");setText("link-progress-footer",data.status||"Awaiting fresh bridge frames");setProgress("link-progress-fill",data.link_up?100:22,data.link_up?"":"warn");setText("ota-hint",data.rp_mode==="OTA"?"OTA mode is active. Browser uploads and ArduinoOTA are enabled.":"RP2040 is not in OTA mode. Browser upload will be rejected until OTA mode is selected from PowerStation.");if(!prev||prev.status!==data.status||prev.link_up!==data.link_up||prev.rp_mode!==data.rp_mode)pushActivity("Bridge status update",data.status||"No status reported",data.link_up?"info":"warn")}
function renderTelemetry(data){state.lastTelemetry=data;pushHistory("soc",data.soc_pct);pushHistory("power",data.power_w);pushHistory("tempBat",data.temp_bat_c);pushHistory("tempSys",data.temp_inv_c);const soc=Number(data.soc_pct)||0,pwr=Number(data.power_w)||0,volt=Number(data.voltage_v)||0,curr=Number(data.current_a)||0,tb=Number(data.temp_bat_c)||0,ts=Number(data.temp_inv_c)||0,tempPeak=Math.max(tb,ts),tempPct=clamp(tempPeak/80*100,0,100);setText("soc",fixed(data.soc_pct,1,"%"));setText("available-wh",fixed(data.available_wh,1," Wh"));setText("runtime",minutesToLabel(data.time_min));setText("power",fixed(data.power_w,0," W"));setText("voltage",fixed(data.voltage_v,2," V"));setText("current",fixed(data.current_a,2," A"));setText("tbat",fixed(data.temp_bat_c,1," C"));setText("tsys",fixed(data.temp_inv_c,1," C"));setText("sidebar-soc",fixed(data.soc_pct,1,"%"));setText("sidebar-power",fixed(data.power_w,0," W"));setText("sidebar-runtime",minutesToLabel(data.time_min));setText("telemetry-power",fixed(data.power_w,0," W"));setText("telemetry-tbat",fixed(data.temp_bat_c,1," C"));setText("telemetry-voltage",fixed(data.voltage_v,2," V"));setText("telemetry-current",fixed(data.current_a,2," A"));setText("telemetry-available",fixed(data.available_wh,1," Wh"));setText("telemetry-runtime",minutesToLabel(data.time_min));setText("telemetry-mode-flag",data.mode||"--");setText("telemetry-charge-flag",data.charging?"Charging":"Discharging / idle");setText("telemetry-mode",`RP2040 reports mode ${data.mode||"--"} and charging=${data.charging?"yes":"no"}.`);setRing(soc,fixed(data.soc_pct,0,"%"));setText("soc-progress-label",fixed(data.soc_pct,1,"%"));setText("soc-progress-footer",data.charging?"Charging headroom available":"Discharge reserve");setProgress("soc-progress-fill",soc,soc<=15?"danger":soc<=35?"warn":"");setText("thermal-progress-label",`${fixed(data.temp_bat_c,0," C")} / ${fixed(data.temp_inv_c,0," C")}`);setText("thermal-progress-footer",tempPeak>=50?"Thermal caution band":"Healthy thermal envelope");setProgress("thermal-progress-fill",tempPct,tempPeak>=65?"danger":tempPeak>=50?"warn":"");setText("bar-voltage-label",fixed(data.voltage_v,2," V"));setText("bar-voltage-foot","Normalized against 60 V reference");setProgress("bar-voltage-fill",clamp(volt/60*100,0,100),volt<42?"warn":"");setText("bar-current-label",fixed(data.current_a,2," A"));setText("bar-current-foot",data.charging?"Charge current detected":"Load current detected");setProgress("bar-current-fill",clamp(Math.abs(curr)/50*100,0,100),Math.abs(curr)>=35?"warn":"");setText("bar-temp-label",`${fixed(data.temp_bat_c,1," C")} / ${fixed(data.temp_inv_c,1," C")}`);setText("bar-temp-foot",tempPeak>=50?"Rising thermal load":"Thermal margin available");setProgress("bar-temp-fill",tempPct,tempPeak>=65?"danger":tempPeak>=50?"warn":"");sparkline("chart-soc",state.history.soc,{color:"#4ae3bc",min:0,max:100});sparkline("chart-power",state.history.power,{color:pwr>=0?"#6cb8ff":"#ffb85f"});dualSparkline("chart-thermal",state.history.tempBat,state.history.tempSys);sparkline("telemetry-chart-soc",state.history.soc,{color:"#4ae3bc",min:0,max:100});sparkline("telemetry-chart-power",state.history.power,{color:pwr>=0?"#6cb8ff":"#ffb85f"});dualSparkline("telemetry-chart-thermal",state.history.tempBat,state.history.tempSys);setText("soc-chart-caption",state.history.soc.length>1?`Latest ${state.history.soc.length} browser-side samples`:"Collecting recent SOC samples");setText("power-chart-caption",pwr>=0?"Positive value suggests output load":"Negative value suggests charging");updateLastSync("Telemetry refreshed")}
function renderStats(data){const prev=state.lastStats;state.lastStats=data;if(!prev||prev.soh_last!==data.soh_last||prev.efc_total!==data.efc_total)pushActivity("Stats snapshot updated",`SOH ${fixed(data.soh_last,1,"%")} | EFC ${fixed(data.efc_total,2)}`)}
function renderRpSettings(data){state.lastSettings=data;const f=document.getElementById("rp-settings-form");["capacity_ah","vbat_warn_v","vbat_cut_v","cell_warn_v","cell_cut_v","temp_bat_warn_c","temp_bat_cut_c"].forEach(k=>{if(f.elements[k]&&data[k]!==undefined)f.elements[k].value=data[k]});if(f.elements.sound&&data.buzzer_preset!==undefined)f.elements.sound.value=String(data.buzzer_preset)==="2"?"silent":String(data.buzzer_preset)==="1"?"minimal":"full"}
function renderNetworkSettings(data){const f=document.getElementById("network-form");f.elements.wifi_mode.value=data.wifi_mode||"ap";f.elements.hostname.value=data.hostname||"";f.elements.ap_ssid.value=data.ap_ssid||"";f.elements.sta_ssid.value=data.sta_ssid||"";f.elements.ap_password.value="";f.elements.sta_password.value="";f.elements.ota_password.value=""}
function renderCache(data){state.lastCache=data;document.getElementById("raw-cache").textContent=JSON.stringify(data,null,2);document.getElementById("system-last-reply").textContent=JSON.stringify({last_ack:data.last_ack||null,last_error:data.last_error||null},null,2)}
async function loadSystem(){const d=await expectJson("/api/system");renderSystem(d);return d}
async function loadTelemetry(){const d=await expectJson("/api/telemetry");renderTelemetry(d);return d}
async function loadStats(){const d=await expectJson("/api/stats");renderStats(d);return d}
async function loadBridgeSettings(){const d=await expectJson("/api/settings/rp2040");renderRpSettings(d);return d}
async function loadNetworkSettings(){const d=await expectJson("/api/settings/network");renderNetworkSettings(d);return d}
async function loadRawCache(manual=false){const d=await expectJson("/api/cache");renderCache(d);if(manual)toast("Cache refreshed","Raw cached JSON has been updated.");return d}
async function loadLogs(manual=false){const d=await expectJson("/api/log?start=0&count=16");renderLogs(d);if(manual)toast("Log slice refreshed",`Fetched ${d.count||0} events.`);return d}
async function bridgeAction(name){try{const d=await expectJson("/api/actions",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({name})});toast(`Action ${name}`,d.message||"Completed",d.ok?"success":"danger");pushActivity(`Bridge action ${name}`,d.message||"Completed",d.ok?"info":"danger");await Promise.allSettled([loadLogs(),loadStats(),loadSystem()])}catch(error){toast(`Action ${name}`,error.message,"danger");pushActivity(`Bridge action ${name}`,error.message,"danger")}}
async function refreshAll(manual=false){const res=await Promise.allSettled([loadSystem(),loadTelemetry(),loadStats()]);const fail=res.find(x=>x.status==="rejected");if(fail){toast("Refresh failed",fail.reason?.message||String(fail.reason),"danger");pushActivity("Refresh warning",fail.reason?.message||"Refresh failed","warn");return}if(manual)toast("Bridge synced","System, telemetry, and stats are up to date.")}
async function maybeRefreshRoute(route){if(route==="/events")await Promise.allSettled([loadLogs(),loadStats()]);else if(route==="/settings")await Promise.allSettled([loadBridgeSettings(),loadNetworkSettings(),loadSystem()]);else if(route==="/system")await Promise.allSettled([loadSystem(),loadRawCache(),loadLogs()])}
function bindRoutes(){document.querySelectorAll(".nav button").forEach(b=>b.addEventListener("click",()=>openRoute(b.dataset.route)));document.querySelectorAll("[data-go]").forEach(b=>b.addEventListener("click",()=>openRoute(b.dataset.go)));addEventListener("popstate",()=>openRoute(location.pathname,false))}
function bindForms(){document.getElementById("refresh-all-btn").addEventListener("click",()=>refreshAll(true));document.getElementById("refresh-log-btn").addEventListener("click",()=>loadLogs(true).catch(error=>reportError("Log slice",error)));document.getElementById("refresh-cache-btn").addEventListener("click",()=>loadRawCache(true).catch(error=>reportError("Cache refresh",error)));document.getElementById("sync-events-btn").addEventListener("click",()=>refreshAll(true));document.getElementById("reload-rp-settings").addEventListener("click",async()=>{try{await loadBridgeSettings();toast("RP2040 settings","Latest settings reloaded from the bridge.")}catch(error){reportError("RP2040 settings",error)}});document.getElementById("reload-network-settings").addEventListener("click",async()=>{try{await loadNetworkSettings();toast("Network profile","Stored Wi-Fi settings reloaded.")}catch(error){reportError("Network profile",error)}});document.getElementById("network-form").addEventListener("submit",async e=>{e.preventDefault();const form=e.currentTarget,note=document.getElementById("network-note");note.textContent="Saving and applying network profile...";try{const d=await expectJson("/api/settings/network",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams(new FormData(form))});note.textContent=d.message||"Network profile updated.";toast("Network profile",note.textContent,d.ok?"success":"warn");pushActivity("Network profile saved",note.textContent,d.ok?"info":"warn");await loadSystem()}catch(error){note.textContent=error.message;reportError("Network profile",error)}});document.getElementById("rp-settings-form").addEventListener("submit",async e=>{e.preventDefault();const form=e.currentTarget,note=document.getElementById("rp-settings-note");note.textContent="Applying fields over UART...";try{for(const [key,value] of new FormData(form).entries()){if(value==="")continue;const d=await expectJson("/api/settings/rp2040/apply",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({key,value})});if(!d.ok)throw new Error(d.message||`Failed on ${key}`)}note.textContent="RP2040 settings updated successfully.";toast("RP2040 settings",note.textContent);pushActivity("RP2040 settings saved",note.textContent);await loadBridgeSettings()}catch(error){note.textContent=error.message;reportError("RP2040 settings",error)}});document.getElementById("ota-form").addEventListener("submit",e=>{e.preventDefault();const input=document.getElementById("ota-file"),note=document.getElementById("ota-note"),fill=document.getElementById("ota-progress-fill");if(!input.files||!input.files.length){note.textContent="Choose a firmware binary first.";toast("OTA upload",note.textContent,"warn");return}const payload=new FormData();payload.append("update",input.files[0]);fill.style.width="0%";note.textContent="Uploading OTA binary...";const xhr=new XMLHttpRequest();xhr.open("POST","/api/ota/upload",true);xhr.upload.onprogress=evt=>{if(!evt.lengthComputable)return;const p=clamp(evt.loaded/evt.total*100,0,100);fill.style.width=`${p}%`;note.textContent=`Uploading OTA binary... ${Math.round(p)}%`};xhr.onload=async()=>{let d={};try{d=xhr.responseText?JSON.parse(xhr.responseText):{}}catch(err){}if(xhr.status>=200&&xhr.status<300&&d.ok){fill.style.width="100%";note.textContent=d.message||"OTA upload complete, rebooting.";toast("OTA upload",note.textContent);pushActivity("OTA upload complete",note.textContent)}else{fill.style.width="0%";const msg=d.message||xhr.responseText||`HTTP ${xhr.status}`;note.textContent=msg;reportError("OTA upload",msg)}try{await loadSystem()}catch(error){reportError("System refresh",error)}};xhr.onerror=()=>{fill.style.width="0%";note.textContent="OTA upload failed due to a network error.";reportError("OTA upload",note.textContent)};xhr.send(payload)})}
document.addEventListener("DOMContentLoaded",async()=>{bindRoutes();bindForms();const route=normalizeRoute(location.pathname);if(location.pathname==="/"||!ROUTES[location.pathname])history.replaceState({}, "", route);openRoute(route,false);await Promise.allSettled([refreshAll(),loadLogs(),loadBridgeSettings(),loadNetworkSettings(),loadRawCache()]);setInterval(()=>{refreshAll()},2500);setInterval(()=>{const jobs=[];if(state.route==="/events")jobs.push(loadLogs());if(state.route==="/system")jobs.push(loadRawCache());if(jobs.length)Promise.allSettled(jobs)},9000)});
  </script>
</body>
</html>
)HTML";
