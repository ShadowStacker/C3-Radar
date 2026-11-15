#pragma once

#include <pgmspace.h>

// Single-page UI served at "/". Kept dependency-free (no CDN): the radar may
// be on a network without internet access for the browser.
static const char kWebPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Plane Radar</title>
<style>
:root{--bg:#070b14;--card:#101827;--line:#1f2b40;--ink:#e8eef8;--dim:#8a99b3;--acc:#40be78;--warn:#ff4030}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:900px;margin:0 auto;padding:16px;display:grid;gap:16px;grid-template-columns:1fr}
@media(min-width:800px){main{grid-template-columns:minmax(0,400px) 1fr;align-items:start}header{grid-column:1/-1}}
header{display:flex;flex-wrap:wrap;align-items:baseline;gap:8px 14px}
h1{font-size:20px;margin:0}
#stat{color:var(--dim);font-variant-numeric:tabular-nums}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px 16px}
.card h2{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--dim);margin:0 0 10px}
.stack{display:grid;gap:16px}
.scope{position:relative;width:100%;max-width:360px;aspect-ratio:1;margin:4px auto 12px;border-radius:50%;
 background:#000;box-shadow:0 0 0 6px #1a2233,0 0 0 8px #2c3850;overflow:hidden}
.scope img{width:100%;height:100%;display:block;border-radius:50%;image-rendering:auto}
.scope .msg{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;text-align:center;
 padding:18%;color:var(--dim)}
.seg{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
button,.btn{font:inherit;color:var(--ink);background:#1a2538;border:1px solid var(--line);border-radius:9px;
 padding:8px 10px;cursor:pointer;text-decoration:none;text-align:center}
button:hover,.btn:hover{border-color:#3a4c6c}
button.on{background:var(--acc);border-color:var(--acc);color:#04130a;font-weight:600}
button.pri{background:#24406a;border-color:#335a94}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:7px 0}
.row+.row{border-top:1px solid var(--line)}
.row small{display:block;color:var(--dim)}
input[type=number],select{font:inherit;color:var(--ink);background:#0b1220;border:1px solid var(--line);
 border-radius:8px;padding:7px 9px;width:130px}
input[type=range]{width:150px;accent-color:var(--acc)}
.sw{position:relative;flex:none;width:46px;height:26px}
.sw input{opacity:0;width:100%;height:100%;margin:0;position:absolute;cursor:pointer;z-index:1}
.sw span{position:absolute;inset:0;border-radius:13px;background:#2a3650;transition:.15s}
.sw span:after{content:"";position:absolute;left:3px;top:3px;width:20px;height:20px;border-radius:50%;background:#cfd8e6;transition:.15s}
.sw input:checked+span{background:var(--acc)}
.sw input:checked+span:after{left:23px;background:#fff}
.sw.mil input:checked+span{background:var(--warn)}
.acts{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
#note{min-height:1.4em;color:var(--acc);margin-top:8px}
#note.err{color:var(--warn)}
</style></head><body><main>
<header><h1>Plane Radar</h1><span id="stat">connecting&hellip;</span></header>

<div class="card">
 <h2>Display</h2>
 <div class="scope"><img id="scr" alt="Live copy of the radar display"><div class="msg" id="scrmsg">Loading&hellip;</div></div>
 <div class="seg" id="ranges"></div>
 <div class="row" style="margin-top:8px"><div>Live view<small>Copies the screen every couple of seconds</small></div>
  <label class="sw"><input type="checkbox" id="live" checked><span></span></label></div>
</div>

<div class="stack">
<div class="card">
 <h2>Aircraft</h2>
 <div class="row"><div>Military aircraft only<small>Hide everything not flagged military</small></div>
  <label class="sw mil"><input type="checkbox" data-k="mil"><span></span></label></div>
 <div class="row"><div>Show airport runways</div>
  <label class="sw"><input type="checkbox" data-k="runways"><span></span></label></div>
 <div class="row"><div>Distances in miles</div>
  <label class="sw"><input type="checkbox" data-k="miles"><span></span></label></div>
</div>

<div class="card">
 <h2>Location</h2>
 <div class="row"><label for="lat">Latitude</label><input type="number" id="lat" step="0.000001" min="-90" max="90"></div>
 <div class="row"><label for="lon">Longitude</label><input type="number" id="lon" step="0.000001" min="-180" max="180"></div>
 <div class="acts"><button class="pri" id="saveloc">Save location</button></div>
</div>

<div class="card">
 <h2>Screen</h2>
 <div class="row"><div>Brightness<small>Needs the display BL pin on GPIO 5</small></div>
  <input type="range" id="brightness" min="5" max="255"></div>
 <div class="row"><label for="rotation">Rotation</label>
  <select id="rotation"><option value="0">0&deg;</option><option value="1">90&deg;</option><option value="2">180&deg;</option><option value="3">270&deg;</option></select></div>
 <div class="row"><div>Invert colours<small>If the picture looks like a negative</small></div>
  <label class="sw"><input type="checkbox" data-k="invert"><span></span></label></div>
 <div class="row"><div>Swap red / blue<small>If reds show as blue</small></div>
  <label class="sw"><input type="checkbox" data-k="swap_rb"><span></span></label></div>
</div>

<div class="card">
 <h2>Network</h2>
 <div class="row"><div><label for="port">Web server port</label><small>This page moves to the new port</small></div>
  <input type="number" id="port" min="1" max="65535"></div>
 <div class="acts"><button class="pri" id="saveport">Change port</button>
  <a class="btn" href="/wifi">Wi-Fi setup</a><a class="btn" href="/info">Device info</a></div>
 <div id="note"></div>
</div>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
let st=null,timer=0;
function note(t,err){const n=$('note');n.textContent=t;n.className=err?'err':''}
async function post(o){
  const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams(o)});
  const j=await r.json();
  if(!j.ok)note(j.error||'Not saved',true);
  return j;
}
function fill(s){
  st=s;
  const age=s.data_age_s<0?'waiting for data':(s.data_age_s>30?'no data for '+s.data_age_s+' s':'live');
  $('stat').textContent=s.aircraft+' in range · '+s.beyond+' beyond · '+age+(s.mil?' · military only':'');
  const box=$('ranges');box.textContent='';
  s.ranges.forEach((label,i)=>{const b=document.createElement('button');b.textContent=label;
    if(i===s.range)b.className='on';b.onclick=()=>post({range:i}).then(load).then(()=>shot(300));box.appendChild(b)});
  document.querySelectorAll('input[data-k]').forEach(c=>c.checked=!!s[c.dataset.k]);
  for(const k of['lat','lon','port','brightness','rotation'])if(document.activeElement!==$(k))$(k).value=s[k];
}
async function load(){try{fill(await(await fetch('/api/state')).json())}catch(e){$('stat').textContent='device not responding'}}
function shot(delay){
  clearTimeout(timer);
  timer=setTimeout(()=>{if($('live').checked&&!document.hidden)$('scr').src='/screen.bmp?t='+Date.now()},delay);
}
$('scr').onload=()=>{$('scrmsg').style.display='none';$('scr').style.visibility='visible';shot(1500)};
$('scr').onerror=()=>{$('scr').style.visibility='hidden';$('scrmsg').style.display='flex';
  $('scrmsg').textContent='The radar is not on screen right now';shot(4000)};
$('live').onchange=()=>shot(0);
document.addEventListener('visibilitychange',()=>shot(0));
document.querySelectorAll('input[data-k]').forEach(c=>c.onchange=()=>
  post({[c.dataset.k]:c.checked?1:0}).then(load).then(()=>shot(300)));
$('brightness').onchange=e=>post({brightness:e.target.value});
$('rotation').onchange=e=>post({rotation:e.target.value});
$('saveloc').onclick=()=>post({lat:$('lat').value,lon:$('lon').value}).then(j=>{
  if(j.ok)note('Location saved');load();shot(300)});
$('saveport').onclick=async()=>{
  const p=$('port').value;
  if(st&&+p===st.port){note('Already on port '+p);return}
  const j=await post({port:p});
  if(!j.ok)return;
  const url=location.protocol+'//'+location.hostname+(+p===80?'':':'+p)+'/';
  note('Web server is restarting on port '+p+'…');
  setTimeout(()=>location.href=url,3500);
};
load();shot(0);setInterval(load,5000);
</script></body></html>
)HTML";
