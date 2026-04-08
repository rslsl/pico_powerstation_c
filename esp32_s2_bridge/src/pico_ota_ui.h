#pragma once

#include <pgmspace.h>

static const char PSTATION_PICO_OTA_UI[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <title>Pico OTA Recovery</title>
  <style>
    :root{--bg:#f4f6f8;--card:#ffffff;--border:#dfe6ec;--text:#162330;--muted:#647280;--warn:#f59f00;--warn-soft:#fff4db;--ok:#2f9e44;--ok-soft:#edf9f0;--danger:#d63939;--danger-soft:#feeeee;--shadow:0 12px 30px rgba(22,35,48,.08);--radius:18px;--font:"Segoe UI Variable Text","Segoe UI",sans-serif}
    *{box-sizing:border-box}html,body{margin:0;min-height:100%}body{font-family:var(--font);color:var(--text);background:radial-gradient(circle at top,#fff7e6 0,#f4f6f8 42%)}
    button,input{font:inherit}main{max-width:760px;margin:0 auto;padding:18px 14px 32px}.hero,.card{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow)}.hero{padding:22px;display:grid;gap:12px}.eyebrow{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}h1,h2,p{margin:0}.title{font-size:clamp(28px,7vw,40px);line-height:1.02}.sub{color:var(--muted);line-height:1.55}
    .grid{display:grid;gap:14px;margin-top:14px}.card{padding:18px;display:grid;gap:14px}.stats{display:grid;gap:10px}.kv{display:flex;justify-content:space-between;gap:10px;padding:12px 14px;border:1px solid var(--border);border-radius:14px;background:#fbfcfd}.kv span{color:var(--muted);font-size:13px}.kv strong{font-size:15px;text-align:right}
    form{display:grid;gap:12px}label{display:grid;gap:6px;color:var(--muted);font-size:13px}input[type=file]{padding:12px;border:1px solid var(--border);border-radius:14px;background:#fff}.actions{display:flex;flex-wrap:wrap;gap:10px}.btn{border:1px solid var(--border);background:#fff;border-radius:14px;padding:11px 14px;cursor:pointer;color:var(--text)}.btn.primary{background:var(--warn-soft);border-color:#f1dfb2;color:#9a6500}
    .track{height:12px;border-radius:999px;background:#e5ebf1;overflow:hidden}.fill{height:100%;width:0;background:linear-gradient(90deg,var(--warn),#ffd06d);transition:width .2s ease}.note{padding:13px 14px;border-radius:14px;border:1px solid var(--border);background:#fbfcfd;color:var(--muted);line-height:1.55}.note.ok{background:var(--ok-soft);border-color:#cfead5;color:#1f6f31}.note.danger{background:var(--danger-soft);border-color:#f0c9c9;color:#9f2626}
    .footer{font-size:12px;color:var(--muted);text-align:center;margin-top:12px}
  </style>
</head>
<body>
  <main>
    <section class="hero">
      <div class="eyebrow">Recovery Mode</div>
      <h1 class="title">Pico OTA</h1>
      <p class="sub">This is a dedicated recovery page. The main dashboard is unavailable while RP2040 recovery mode is active.</p>
    </section>

    <div class="grid">
      <section class="card">
        <h2>Target</h2>
        <div class="stats">
          <div class="kv"><span>Phase</span><strong id="phase">--</strong></div>
          <div class="kv"><span>State</span><strong id="state">--</strong></div>
          <div class="kv"><span>Running Slot</span><strong id="running">--</strong></div>
          <div class="kv"><span>Target Slot</span><strong id="target">--</strong></div>
          <div class="kv"><span>Confirmed Slot</span><strong id="confirmed">--</strong></div>
          <div class="kv"><span>Bridge AP</span><strong id="apip">--</strong></div>
        </div>
      </section>

      <section class="card">
        <h2>Upload</h2>
        <form id="upload-form">
          <label>Pico slot firmware (.bin)
            <input type="file" id="fw-file" name="update" accept=".bin,application/octet-stream">
          </label>
          <div class="actions">
            <button class="btn primary" type="submit">Upload To Pico</button>
            <button class="btn" id="refresh-btn" type="button">Refresh</button>
          </div>
          <div class="track"><div class="fill" id="progress"></div></div>
          <div class="note" id="note">Waiting for Pico OTA status...</div>
        </form>
      </section>
    </div>

    <div class="footer">Power on the system while holding DOWN to enter recovery.</div>
  </main>

  <script>
    const OTA_STAGE_CHUNK_BYTES=4096
    const ui={httpUploading:false,lastStatus:null}
    const $=(id)=>document.getElementById(id)
    async function expectJson(url,options){const r=await fetch(url,options);const t=await r.text();let p={};try{p=t?JSON.parse(t):{}}catch(e){throw new Error(t||('HTTP '+r.status))}if(!r.ok||p.ok===0)throw new Error(p.message||('HTTP '+r.status));return p}
    async function postForm(url,params){return expectJson(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(params||{})})}
    async function postChunk(offset,blob){const data=new FormData();data.append('chunk',blob,'chunk.bin');return expectJson('/api/pico-ota/chunk?offset='+encodeURIComponent(offset)+'&size='+encodeURIComponent(blob.size||0),{method:'POST',body:data})}
    function phaseProgress(data,dev){const phase=data.phase||'idle',total=Number(data.total_bytes)||0,staged=Number(data.staged_bytes)||0,sent=Number(data.sent_bytes)||0,deviceWritten=Number(dev.written)||0,deviceSize=Number(dev.size)||0;if(phase==='staging'&&total>0)return Math.max(0,Math.min(100,(staged/total)*100));if((phase==='queued'||phase==='transferring'||phase==='finalizing'||phase==='done')&&total>0)return Math.max(0,Math.min(100,(sent/total)*100));if(deviceSize>0)return Math.max(0,Math.min(100,(deviceWritten/deviceSize)*100));return 0}
    function render(data){ui.lastStatus=data;const dev=data.device||{};$('phase').textContent=data.phase||'--';$('state').textContent=dev.state||'--';$('running').textContent=dev.running_slot||'--';$('target').textContent=dev.target_slot||'--';$('confirmed').textContent=dev.confirmed_slot||'--';$('apip').textContent=data.ap_ip||'--';$('progress').style.width=phaseProgress(data,dev)+'%';const notes=[];if(dev.safe_ready)notes.push('Recovery mode active');if(data.message)notes.push(data.message);if(data.phase==='staging'&&data.total_bytes)notes.push(String(data.staged_bytes||0)+' / '+String(data.total_bytes)+' bytes staged on ESP');if((data.phase==='queued'||data.phase==='transferring'||data.phase==='finalizing'||data.phase==='done')&&data.total_bytes)notes.push(String(data.sent_bytes||0)+' / '+String(data.total_bytes)+' bytes sent to Pico');if(dev.reboot_pending)notes.push('Pico reboot pending');if(dev.error)notes.push('Error '+dev.error);const note=$('note');note.textContent=notes.length?notes.join('. ')+'.':'Upload the Pico slot image shown for the target slot.';note.className='note'+(data.upload_failed?' danger':((data.phase==='done'&&!data.upload_active)?' ok':''))}
    async function refresh(){if(ui.httpUploading)return;try{render(await expectJson('/api/pico-ota/status'))}catch(error){const note=$('note');note.textContent=error.message;note.className='note danger'}}
    async function cancelSessionSilently(){try{await postForm('/api/pico-ota/abort',{})}catch(error){}}
    $('refresh-btn').addEventListener('click',()=>refresh())
    $('upload-form').addEventListener('submit',async(event)=>{event.preventDefault();const fileInput=$('fw-file'),file=fileInput.files&&fileInput.files[0];if(!file){const note=$('note');note.textContent='Choose a Pico firmware .bin first.';note.className='note danger';return}try{const status=await expectJson('/api/pico-ota/status');render(status);const dev=status.device||{};if(status.upload_active){throw new Error('Another Pico OTA session is already active')}if(!dev.safe_ready){throw new Error('Pico is not in recovery OTA mode')}}catch(error){const note=$('note');note.textContent=error.message;note.className='note danger';return}ui.httpUploading=true;$('progress').style.width='0%';$('note').textContent='Starting Pico OTA staging session...';$('note').className='note';try{await postForm('/api/pico-ota/start',{size:String(file.size||0),version:file.name||'pico-slot'});for(let offset=0;offset<file.size;offset+=OTA_STAGE_CHUNK_BYTES){const blob=file.slice(offset,Math.min(file.size,offset+OTA_STAGE_CHUNK_BYTES));let attempt=0;while(true){try{await postChunk(offset,blob);break}catch(error){attempt+=1;if(attempt>=3)throw error;await new Promise((resolve)=>setTimeout(resolve,attempt*400))}}const pct=Math.max(0,Math.min(100,((offset+blob.size)/file.size)*100));$('progress').style.width=pct+'%';$('note').textContent='Uploading firmware to ESP staging... '+Math.round(pct)+'%'}const commit=await postForm('/api/pico-ota/commit',{});$('note').textContent=commit.message||'Firmware staged on ESP. Transfer to Pico has started.';$('note').className='note ok'}catch(error){await cancelSessionSilently();$('progress').style.width='0%';$('note').textContent=error.message||'Upload failed due to network error.';$('note').className='note danger'}finally{ui.httpUploading=false;await refresh()}})
    document.addEventListener('DOMContentLoaded',async()=>{await refresh();setInterval(refresh,1000)})
  </script>
</body>
</html>
)HTML";
