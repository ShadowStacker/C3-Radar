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
.scope{touch-action:none;cursor:grab;user-select:none}
.chip{display:inline-flex;margin:0 6px 6px 0}
.chip button{border-radius:9px 0 0 9px;padding:6px 10px}
.chip button+button{border-radius:0 9px 9px 0;border-left:0;padding:6px 9px;color:var(--dim)}
.chip.on button:first-child{background:var(--acc);border-color:var(--acc);color:#04130a;font-weight:600}
.scope img{pointer-events:none;width:100%;height:100%;display:block;border-radius:50%;image-rendering:auto}
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
input[type=number],input[type=text],select{font:inherit;color:var(--ink);background:#0b1220;border:1px solid var(--line);
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
.swatches{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px 12px;margin-top:10px}
.swatches label{display:flex;align-items:center;gap:8px}
input[type=color]{width:38px;height:28px;padding:0;border:1px solid var(--line);border-radius:6px;background:none;cursor:pointer}
input[type=file]{max-width:100%;color:var(--dim)}
#note{min-height:1.4em;color:var(--acc);margin-top:8px}
#note.err{color:var(--warn)}
</style></head><body><main>
<header><h1 id="title">Plane Radar</h1><span id="stat">connecting&hellip;</span>
<div id="alert" style="display:none;flex-basis:100%;background:#5a0d0a;border:1px solid var(--warn);border-radius:10px;padding:8px 12px;font-weight:600"></div>
<div id="safe" style="display:none;flex-basis:100%;background:#4a3200;border:1px solid #ffbe46;border-radius:10px;padding:10px 12px">
 <b>Safe mode.</b> <span id="safetext"></span>
 <div class="acts"><button id="safemaps">Remove all maps</button><button class="pri" id="saferestart">Restart normally</button></div></div></header>

<div class="card">
 <h2>Display</h2>
 <div class="scope"><img id="scr" alt="Live copy of the radar display"><div class="msg" id="scrmsg">Loading&hellip;</div></div>
 <div style="text-align:center;color:var(--dim);font-size:13px;margin:-4px 0 10px">Drag the picture to move the radar&rsquo;s centre</div>
 <div class="seg" id="ranges"></div>
 <div class="row" style="margin-top:8px"><div>Live view<small>Copies the screen every couple of seconds</small></div>
  <label class="sw"><input type="checkbox" id="live" checked><span></span></label></div>
</div>

<div class="stack">
<div class="card">
 <h2>Aircraft</h2>
 <div class="row"><div>Military aircraft only<small>Hide everything not flagged military</small></div>
  <label class="sw mil"><input type="checkbox" data-k="mil"><span></span></label></div>
 <div class="row"><div>Show airport runways<small>All runways and airport codes on the radar</small></div>
  <label class="sw"><input type="checkbox" data-k="runways"><span></span></label></div>
 <div class="row"><div>Distances in miles</div>
  <label class="sw"><input type="checkbox" data-k="miles"><span></span></label></div>
 <div class="row"><div>Callsign</div><label class="sw"><input type="checkbox" data-abit="1"><span></span></label></div>
 <div class="row"><div>Aircraft type</div><label class="sw"><input type="checkbox" data-abit="2"><span></span></label></div>
 <div class="row"><div>Altitude</div><label class="sw"><input type="checkbox" data-abit="4"><span></span></label></div>
 <div class="row"><div>Speed line<small>The line ahead of each aircraft</small></div><label class="sw"><input type="checkbox" data-abit="8"><span></span></label></div>
 <div class="row"><div>Trail<small>The dots behind each aircraft</small></div><label class="sw"><input type="checkbox" data-abit="16"><span></span></label></div>
 <div id="altkey" style="margin-top:12px">
  <div style="color:var(--dim);font-size:13px;margin-bottom:4px">Aircraft colour by altitude (feet)</div>
  <div id="altbar" style="height:14px;border-radius:7px;border:1px solid var(--line)"></div>
  <div id="altticks" style="position:relative;height:1.5em;font-size:12px;color:var(--dim);font-variant-numeric:tabular-nums"></div>
  <div style="font-size:12px;color:var(--dim)"><span style="display:inline-block;width:10px;height:10px;border-radius:50%;background:#ff4030;vertical-align:-1px" id="mildot"></span> military
   &nbsp; <span style="display:inline-block;width:10px;height:10px;border-radius:50%;background:#a5afbe;vertical-align:-1px"></span> altitude unknown</div>
 </div>
 <div id="altoff" style="display:none;margin-top:10px;font-size:13px;color:var(--dim)">This theme draws all aircraft in one colour.</div>
</div>

<div class="card">
 <h2>Map</h2>
 <div id="mapstat" style="color:var(--dim)">&hellip;</div>
 <div class="acts"><button class="pri" id="mapget">Download map for this location</button>
  <button id="mapdel">Remove map</button></div>
 <div id="mapnote" style="min-height:1.4em;margin:8px 0 2px;color:var(--acc)"></div>
 <div class="row"><div>Coastline and lakes</div><label class="sw"><input type="checkbox" data-bit="1"><span></span></label></div>
 <div class="row"><div>Rivers and canals</div><label class="sw"><input type="checkbox" data-bit="2"><span></span></label></div>
 <div class="row"><div>Borders<small>Country and state / province</small></div><label class="sw"><input type="checkbox" data-bit="4"><span></span></label></div>
 <div class="row"><div>Motorways</div><label class="sw"><input type="checkbox" data-bit="8"><span></span></label></div>
 <div class="row"><div>Main roads<small>Trunk roads that are not motorways</small></div><label class="sw"><input type="checkbox" data-bit="16"><span></span></label></div>
 <div class="row"><div>Town names<small>Cities and towns at every range; villages too at 5 and 10 km</small></div><label class="sw"><input type="checkbox" data-bit="32"><span></span></label></div>
 <div class="row"><div>Airfields<small>Smaller airfields from the map, too (needs Show airport runways)</small></div><label class="sw"><input type="checkbox" data-bit="64"><span></span></label></div>
 <small style="display:block;color:var(--dim);margin-top:8px">Your browser fetches the map from OpenStreetMap (it needs internet access)
  and sends it to the radar. Map data &copy; OpenStreetMap contributors.</small>
</div>

<div class="card">
 <h2>Colours</h2>
 <div class="row"><label for="theme">Theme</label><select id="theme"></select></div>
 <div id="custom" style="display:none">
  <div class="row"><div>Colour aircraft by altitude<small>Off: all aircraft use the Aircraft colour</small></div>
   <label class="sw"><input type="checkbox" id="calt"><span></span></label></div>
  <div class="swatches" id="swatches"></div>
 </div>
</div>

<div class="card">
 <h2>Location</h2>
 <div class="row"><label for="lat">Latitude</label><input type="text" inputmode="decimal" id="lat" autocomplete="off" placeholder="44.2541"></div>
 <div class="row"><label for="lon">Longitude</label><input type="text" inputmode="decimal" id="lon" autocomplete="off" placeholder="-121.1500"></div>
 <small style="display:block;color:var(--dim);margin-top:6px">Tip: paste a pair copied from a map, like <b>44.2541, -121.1500</b>, into the Latitude box.</small>
 <div class="acts"><button class="pri" id="saveloc">Move radar here</button></div>
 <div id="locnote" style="min-height:1.4em;margin:6px 0;color:var(--acc)"></div>
 <h2 style="margin-top:6px">Saved locations</h2>
 <div id="places" style="color:var(--dim)"></div>
 <div class="acts"><input type="text" id="pname" maxlength="19" placeholder="Name for this spot" style="flex:1;min-width:120px">
  <button id="padd">Save to list</button></div>
</div>

<div class="card">
 <h2>Screen</h2>
 <div class="row"><div>Brightness</div>
  <input type="range" id="brightness" min="5" max="255"></div>
 <div class="row"><label for="rotation">Rotation</label>
  <select id="rotation"><option value="0">0&deg;</option><option value="1">90&deg;</option><option value="2">180&deg;</option><option value="3">270&deg;</option></select></div>
 <div class="row"><div>Invert colours<small>If the picture looks like a negative</small></div>
  <label class="sw"><input type="checkbox" data-k="invert"><span></span></label></div>
</div>

<div class="card">
 <h2>Network</h2>
 <div class="row"><div><label for="name">Device name</label><small id="hostinfo">Sets its network address</small></div>
  <input type="text" id="name" maxlength="24"></div>
 <div class="row"><div><label for="port">Web server port</label><small>This page moves to the new port</small></div>
  <input type="number" id="port" min="1" max="65535"></div>
 <div class="acts"><button class="pri" id="savename">Save name</button><button class="pri" id="saveport">Change port</button>
  <a class="btn" href="/wifi">Wi-Fi setup</a><a class="btn" href="/info">Device info</a></div>
 <div id="note"></div>
</div>
<div class="card">
 <h2>Device health</h2>
 <table id="health" style="width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums"></table>
</div>
<div class="card">
 <h2>Firmware update</h2>
 <div style="color:var(--dim)">Installed version: <span id="ver">&hellip;</span></div>
 <div class="acts"><input type="file" id="fwfile" accept=".bin"><button class="pri" id="fwgo">Install update</button></div>
 <div id="fwnote" style="min-height:1.4em;margin-top:8px;color:var(--acc)"></div>
 <small style="display:block;color:var(--dim)">Choose the release file ending in <b>-update.bin</b>. Wi-Fi, location, settings and the map are kept.</small>
</div>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
let st=null,timer=0;
function note(t,err){const n=$('note');n.textContent=t;n.className=err?'err':''}
async function post(o){
  const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams(Object.assign({v:1},o))});
  const j=await r.json();
  if(!j.ok)note(j.error||'Not saved',true);
  return j;
}
function fill(s){
  st=s;
  document.title=s.name;$('title').textContent=s.name;
  $('hostinfo').textContent='Address: '+s.host+(s.port===80?'':':'+s.port);
  document.querySelectorAll('input[data-bit]').forEach(c=>c.checked=!!(s.map_layers&+c.dataset.bit));
  $('ver').textContent=s.version;fillTheme(s);
  document.querySelectorAll('input[data-abit]').forEach(c=>c.checked=!!(s.ac_show&+c.dataset.abit));
  // Altitude key: the same colour stops the radar uses, on a linear scale.
  const cur=s.themes.find(t=>t.id===s.theme),k=s.alt_key,top=k[k.length-1][0];
  if(cur)$('mildot').style.background=cur.colors[13];
  $('altkey').style.display=cur&&cur.alt?'':'none';$('altoff').style.display=cur&&cur.alt?'none':'';
  $('altbar').style.background='linear-gradient(90deg,'+k.map(e=>e[1]+' '+(e[0]*100/top).toFixed(1)+'%').join(',')+')';
  const tk=$('altticks');tk.textContent='';
  k.forEach((e,i)=>{const d=document.createElement('span');const pct=e[0]*100/top;
    d.textContent=i===k.length-1?(e[0]/1000)+'k+':e[0]?(e[0]/1000)+'k':'0';
    d.style.cssText='position:absolute;top:2px;left:'+pct+'%;transform:translateX('+(i===0?'0':i===k.length-1?'-100%':'-50%')+')';
    tk.appendChild(d)});
  const m=s.map;
  const kb=n=>Math.max(1,Math.round(n/1024))+' KB',others=m.count-(m.present?1:0);
  $('mapstat').textContent=(m.present?'Map for this location: '+kb(m.bytes)+(m.offset_km>5?' (made '+m.offset_km+' km away)':''):
    'No map for this location')+(others>0?' · '+others+' more stored for other locations':'')+
    ' · '+kb(m.free)+' free of '+kb(m.capacity);
  fillPlaces(s);
  $('mapdel').style.display=m.present?'':'none';
  const age=s.data_age_s<0?'waiting for data':(s.data_age_s>30?'no data for '+s.data_age_s+' s':'live');
  $('stat').textContent=s.aircraft+' in range · '+s.beyond+' beyond · '+age+(s.mil?' · military only':'')+
    (s.health.source!=='adsb.fi'?' · backup feed '+s.health.source:'');
  // Emergency banner and device health.
  const al=$('alert');
  if(s.alerts.length){al.style.display='';al.textContent='⚠ Emergency: '+s.alerts.map(a=>(a.callsign||'unknown')+' squawking '+a.squawk).join(', ')}
  else al.style.display='none';
  const h=s.health,kbs=n=>Math.round(n/1024)+' KB',up=h.uptime_s;
  const rows=[
    ['Board',h.chip+' ('+h.cores+(h.cores>1?' cores':' core')+')'+(h.psram?', '+(h.psram/1048576).toFixed(0)+' MB extra memory':'')],
    ['Last restart',h.reset+(h.streak?' ('+h.streak+' abnormal in a row)':'')],
    ['Running for',Math.floor(up/86400)+' d '+Math.floor(up%86400/3600)+' h '+Math.floor(up%3600/60)+' min'],
    ['Free memory',kbs(h.heap)+' now, '+kbs(h.heap_min)+' at lowest, largest block '+kbs(h.heap_block)],
    ['Screen redraw',h.frame_ms?h.frame_ms+' ms per frame':'—'],
    ['Wi-Fi signal',h.rssi+' dBm'+(h.rssi>-60?' (strong)':h.rssi>-72?' (good)':h.rssi>-82?' (weak)':' (very weak)')],
    ['Aircraft data',h.source+', last request '+h.fetch_ms+' ms'+(h.fetch_status&&h.fetch_status!==200?' (status '+h.fetch_status+')':'')],
    ['Requests',h.fetch_ok+' ok, '+h.fetch_fail+' failed'],
    ['Firmware',s.version+', '+Math.round(h.fw_used*100/h.fw_slot)+'% of its slot']];
  $('safe').style.display=h.safe?'':'none';
  $('safetext').textContent='The radar restarted abnormally '+h.streak+' times in a row (last cause: '+h.reset+'), so stored maps are not loaded'+
    (h.no_fetch?' and aircraft data is switched off':'')+'. Remove the maps or install other firmware, then restart.';
  const tb=$('health');tb.textContent='';
  for(const r of rows){const tr=tb.insertRow();const a=tr.insertCell(),b=tr.insertCell();
    a.textContent=r[0];b.textContent=r[1];a.style.cssText='color:var(--dim);padding:5px 12px 5px 0;white-space:nowrap;vertical-align:top';b.style.padding='5px 0'}

  const box=$('ranges');box.textContent='';
  s.ranges.forEach((label,i)=>{const b=document.createElement('button');b.textContent=label;
    if(i===s.range)b.className='on';b.onclick=()=>post({range:i}).then(load).then(()=>shot(300));box.appendChild(b)});
  document.querySelectorAll('input[data-k]').forEach(c=>c.checked=!!s[c.dataset.k]);
  for(const k of['lat','lon','port','brightness','rotation','name'])if(!dirty.has(k)&&document.activeElement!==$(k))$(k).value=s[k];
}
async function load(){try{fill(await(await fetch('/api/state')).json())}catch(e){$('stat').textContent='device not responding'}}
function shot(delay){
  clearTimeout(timer);
  timer=setTimeout(()=>{if($('live').checked&&!document.hidden)$('scr').src='/screen.bmp?t='+Date.now()},delay);
}
$('scr').onload=()=>{if(panning){panning=false;$('scr').style.transform=''}$('scrmsg').style.display='none';$('scr').style.visibility='visible';shot(1500)};
$('scr').onerror=()=>{$('scr').style.visibility='hidden';$('scrmsg').style.display='flex';
  $('scrmsg').textContent='The radar is not on screen right now';shot(4000)};
$('live').onchange=()=>shot(0);
document.addEventListener('visibilitychange',()=>shot(0));
document.querySelectorAll('input[data-k]').forEach(c=>c.onchange=()=>
  post({[c.dataset.k]:c.checked?1:0}).then(load).then(()=>shot(300)));
$('brightness').onchange=e=>post({brightness:e.target.value});
$('rotation').onchange=e=>post({rotation:e.target.value});
// Boxes the user has typed in but not saved yet: the 5-second refresh must
// not overwrite them.
const dirty=new Set();
for(const k of['lat','lon','port','name'])$(k).addEventListener('input',()=>dirty.add(k));
function locnote(t,err){const n=$('locnote');n.textContent=t;n.style.color=err?'var(--warn)':'var(--acc)'}
async function setLocation(o,msg){
  const j=await post(o);
  if(!j.ok){locnote(j.error||'Not saved',true);return j}
  dirty.delete('lat');dirty.delete('lon');locnote(msg||'');
  await load();shot(300);return j;
}
// Reads the two boxes into "lat,lon". Tolerates what people paste from maps:
// both numbers in one box, spaces, a trailing comma, a degree sign, a decimal comma.
function readPos(){
  const num=t=>{t=t.trim().replace(/[\u00b0,;]+$/,'').trim();if(/^[-+]?\d+,\d+$/.test(t))t=t.replace(',','.');
    return /^[-+]?(\d+\.?\d*|\.\d+)$/.test(t)?t:null};
  let a=$('lat').value,b=$('lon').value;
  const pair=a.trim().match(/^([-+]?[\d.]+)\s*\u00b0?\s*(?:[,;]\s+|\s+|;|,(?=[-+]?\d*\.))\s*([-+]?[\d.]+)\s*\u00b0?$/);
  if(pair){a=pair[1];b=pair[2]}
  const lat=num(a),lon=num(b);
  if(lat===null||Math.abs(+lat)>90){locnote('Latitude should be a number from -90 to 90, like 44.2541',true);return null}
  if(lon===null||Math.abs(+lon)>180){locnote('Longitude should be a number from -180 to 180, like -121.1500',true);return null}
  $('lat').value=lat;$('lon').value=lon;
  return lat+','+lon;
}
$('lat').addEventListener('paste',()=>setTimeout(()=>{
  const m=$('lat').value.trim().match(/^([-+]?[\d.]+)\s*\u00b0?\s*(?:[,;]\s+|\s+|;|,(?=[-+]?\d*\.))\s*([-+]?[\d.]+)\s*\u00b0?$/);
  if(m){$('lat').value=m[1];$('lon').value=m[2];dirty.add('lat');dirty.add('lon')}
},0));
$('saveloc').onclick=()=>{const pos=readPos();if(pos)setLocation({pos},'Radar moved')};
for(const k of['lat','lon'])$(k).addEventListener('keydown',e=>{if(e.key==='Enter')$('saveloc').click()});
$('padd').onclick=async()=>{
  const pos=readPos();if(!pos)return;
  const j=await setLocation({pos,place_add:$('pname').value},'Saved to the list');
  if(j.ok)$('pname').value='';
};
function fillPlaces(s){
  const box=$('places');box.textContent='';
  if(!s.places.length){box.textContent='No saved locations yet.';return}
  s.places.forEach((p,i)=>{
    const w=document.createElement('span');w.className='chip'+(p.active?' on':'');
    const go=document.createElement('button');go.textContent=p.name+(p.map?' · map':'');
    go.title=p.lat+', '+p.lon;go.onclick=()=>setLocation({place_go:i},'Radar moved to '+p.name);
    const del=document.createElement('button');del.textContent='×';del.title='Remove '+p.name;
    del.onclick=()=>post({place_del:i}).then(load);
    w.append(go,del);box.appendChild(w);
  });
}
// Drag the live picture to move the radar's centre.
const scope=document.querySelector('.scope');let drag=null,panning=false;
scope.onpointerdown=e=>{if(!st||e.button>0)return;drag={x:e.clientX,y:e.clientY};scope.setPointerCapture(e.pointerId);clearTimeout(timer)};
scope.onpointermove=e=>{if(drag)$('scr').style.transform='translate('+(e.clientX-drag.x)+'px,'+(e.clientY-drag.y)+'px)'};
scope.onpointercancel=()=>{drag=null;$('scr').style.transform='';shot(0)};
scope.onpointerup=e=>{
  if(!drag)return;
  const dx=e.clientX-drag.x,dy=e.clientY-drag.y;drag=null;
  if(Math.hypot(dx,dy)<5){$('scr').style.transform='';shot(0);return}
  // Screen pixels -> kilometres at the current range -> degrees.
  const km=st.outer_km/st.ring_px*360/scope.clientWidth;
  const lat=Math.max(-89.9,Math.min(89.9,+st.lat+dy*km/111));
  let lon=+st.lon-dx*km/(111*Math.cos(+st.lat*Math.PI/180));
  lon=((lon+540)%360)-180;
  panning=true;
  setLocation({pos:lat.toFixed(6)+','+lon.toFixed(6)},'Radar moved');
};

// After a port or name change the web server restarts: follow it there.
function follow(j,msg){
  const h=location.hostname.endsWith('.local')?j.host:location.hostname;
  note(msg);
  setTimeout(()=>location.href='http://'+h+(j.port===80?'':':'+j.port)+'/',3500);
}
$('saveport').onclick=async()=>{
  const p=$('port').value;
  if(st&&+p===st.port){note('Already on port '+p);return}
  const j=await post({port:p});
  if(j.ok)dirty.delete('port');
  if(j.ok)follow(j,'Web server is restarting on port '+p+'\u2026');
};
$('savename').onclick=async()=>{
  const j=await post({name:$('name').value});
  if(!j.ok)return;
  dirty.delete('name');
  if(st&&j.host!==st.host)follow(j,'Saved. New address: '+j.host);
  else{note('Name saved');load()}
};
document.querySelectorAll('input[data-bit]').forEach(c=>c.onchange=()=>{
  let mask=0;document.querySelectorAll('input[data-bit]').forEach(x=>{if(x.checked)mask|=+x.dataset.bit});
  post({map_layers:mask}).then(load).then(()=>shot(300));
});
document.querySelectorAll('input[data-abit]').forEach(c=>c.onchange=()=>{
  let mask=0;document.querySelectorAll('input[data-abit]').forEach(x=>{if(x.checked)mask|=+x.dataset.abit});
  post({ac_show:mask}).then(load).then(()=>shot(300));
});
function mapnote(t,err){const n=$('mapnote');n.textContent=t;n.style.color=err?'var(--warn)':'var(--acc)'}
$('mapget').onclick=async()=>{
  if(!st)return;
  $('mapget').disabled=true;
  try{
    const t0=Date.now();
    const r=await buildMap(+st.lat,+st.lon,Math.max(4096,(st.map.room||262144)-4096),mapnote);
    const t1=Date.now();
    mapnote('Sending '+Math.round(r.blob.length/1024)+' KB to the radar\u2026');
    const fd=new FormData();fd.append('map',new Blob([r.blob]),'map.bin');
    const j=await(await fetch('/api/map?size='+r.blob.length,{method:'POST',body:fd})).json();
    if(!j.ok)throw new Error(j.error);
    const secs=ms=>Math.max(1,Math.round(ms/1000))+' s';
    mapnote('Map installed ('+Math.round(r.blob.length/1024)+' KB; download '+secs(t1-t0)+', sending '+secs(Date.now()-t1)+')'+(r.skipped.length?' (could not get: '+r.skipped.join(', ')+')':''),r.skipped.length>0);
  }catch(e){mapnote('Map download failed: '+(e.message||e),true)}
  $('mapget').disabled=false;load();shot(300);
};
$('mapdel').onclick=async()=>{if(!confirm('Remove the stored map for this location?'))return;
  await fetch('/api/map/delete',{method:'POST'});mapnote('Map removed');load();shot(300)};

const SLOTS=['Background','Scope','Grid','Compass text','Aircraft','Callsign','Aircraft type','Altitude text',
  'Airports','Water','Roads','Borders','Town names','Military'];
const CUSTOM=255;
SLOTS.forEach((n,i)=>{const l=document.createElement('label');l.innerHTML='<input type="color" data-slot="'+i+'"><span></span>';
  l.lastChild.textContent=n;$('swatches').appendChild(l)});
const themeOf=id=>st.themes.find(t=>t.id===id);
function sendCustom(extra){
  const list=[...document.querySelectorAll('input[data-slot]')].map(c=>c.value.slice(1)).join(',');
  return post(Object.assign({custom:list,custom_alt:$('calt').checked?1:0},extra||{})).then(load).then(()=>shot(300));
}
function fillTheme(s){
  const sel=$('theme');
  if(document.activeElement!==sel){
    sel.textContent='';
    s.themes.forEach(t=>{const o=document.createElement('option');o.value=t.id;o.textContent=t.name;sel.appendChild(o)});
    sel.value=s.theme;
  }
  $('custom').style.display=s.theme===CUSTOM?'':'none';
  const c=s.themes.find(t=>t.id===CUSTOM);
  document.querySelectorAll('input[data-slot]').forEach(i=>{if(document.activeElement!==i)i.value=c.colors[+i.dataset.slot]});
  $('calt').checked=c.alt;
}
$('theme').onchange=e=>{
  const id=+e.target.value;
  if(id!==CUSTOM||!st){post({theme:id}).then(load).then(()=>shot(300));return}
  // Start the custom palette from whatever theme was showing.
  const from=themeOf(st.theme)||themeOf(CUSTOM);
  document.querySelectorAll('input[data-slot]').forEach(i=>i.value=from.colors[+i.dataset.slot]);
  $('calt').checked=from.alt;
  sendCustom({theme:CUSTOM});
};
document.querySelectorAll('input[data-slot]').forEach(i=>i.onchange=()=>sendCustom());
$('calt').onchange=()=>sendCustom();

$('safemaps').onclick=async()=>{if(!confirm('Remove every stored map?'))return;
  await fetch('/api/map/delete?all=1',{method:'POST'});load()};
$('saferestart').onclick=async()=>{await fetch('/api/restart',{method:'POST'});
  $('safetext').textContent='Restarting\u2026';setTimeout(()=>location.reload(),12000)};
function fwnote(t,err){const n=$('fwnote');n.textContent=t;n.style.color=err?'var(--warn)':'var(--acc)'}
$('fwgo').onclick=()=>{
  const f=$('fwfile').files[0];
  if(!f){fwnote('Choose the update file first',true);return}
  const fd=new FormData();fd.append('firmware',f,f.name);
  const x=new XMLHttpRequest();
  $('fwgo').disabled=true;clearTimeout(timer);$('live').checked=false;
  x.upload.onprogress=e=>{if(e.lengthComputable)fwnote('Uploading… '+Math.round(e.loaded*100/e.total)+'%')};
  x.onerror=()=>{fwnote('Connection lost during the update',true);$('fwgo').disabled=false};
  x.onload=()=>{
    let j={};try{j=JSON.parse(x.responseText)}catch(e){}
    if(j.ok){fwnote('Update installed. The radar is restarting…');setTimeout(()=>location.reload(),15000)}
    else{fwnote(j.error||'Update failed',true);$('fwgo').disabled=false}
  };
  x.open('POST','/api/update');x.send(fd);
};

/*MAPGEN-BEGIN*/
// Builds the radar's map file from OpenStreetMap (Overpass API). Everything
// heavy happens here in the browser; the radar only stores and draws the result.
const MAP_SERVERS=['https://overpass-api.de/api/interpreter','https://overpass.private.coffee/api/interpreter',
  'https://maps.mail.ru/osm/tools/overpass/api/interpreter'];
const MAP_KM=40,MAP_UNIT_M=2,MAP_CHUNK=48;
async function osmQuery(q){
  const body='data='+encodeURIComponent('[out:json][timeout:90];'+q);
  let err=new Error('no map server reachable');
  for(let pass=0;pass<2;pass++)for(const u of MAP_SERVERS){
    try{
      const r=await fetch(u,{method:'POST',body,headers:{'Content-Type':'application/x-www-form-urlencoded'}});
      if(r.ok)return(await r.json()).elements||[];
      err=new Error('map server said '+r.status);
      if(r.status===400){err.fatal=true;throw err}
    }catch(e){if(e.fatal)throw e;err=e}
    await new Promise(res=>setTimeout(res,1500));
  }
  throw err;
}
function mapCrc32(a,from){
  let c=~0;
  for(let i=from;i<a.length;i++){c^=a[i];for(let k=0;k<8;k++)c=c&1?(c>>>1)^0xEDB88320:c>>>1}
  return~c>>>0;
}
// Douglas-Peucker; pts are [x,y] in map units.
function mapSimplify(pts,tol){
  if(pts.length<3)return pts;
  const keep=new Uint8Array(pts.length);keep[0]=keep[pts.length-1]=1;
  const stack=[[0,pts.length-1]];
  while(stack.length){
    const[a,b]=stack.pop();let worst=0,idx=-1;
    const ax=pts[a][0],ay=pts[a][1],dx=pts[b][0]-ax,dy=pts[b][1]-ay,len=Math.hypot(dx,dy);
    for(let i=a+1;i<b;i++){
      const d=len<1e-9?Math.hypot(pts[i][0]-ax,pts[i][1]-ay):Math.abs((pts[i][0]-ax)*dy-(pts[i][1]-ay)*dx)/len;
      if(d>worst){worst=d;idx=i}
    }
    if(worst>tol){keep[idx]=1;stack.push([a,idx],[idx,b])}
  }
  return pts.filter((p,i)=>keep[i]);
}
function mapEncode(lines,labels,tol,lat,lon){
  const lim=MAP_KM*1000/MAP_UNIT_M+1000,recs=[];let lineBytes=0;
  for(const ln of[...lines].sort((a,b)=>a.layer-b.layer)){
    // Cut the line into runs that lie inside the map area.
    const runs=[];let run=[];
    for(const p of ln.pts){
      if(Math.abs(p[0])<=lim&&Math.abs(p[1])<=lim)run.push(p);
      else{if(run.length>1)runs.push(run);run=[]}
    }
    if(run.length>1)runs.push(run);
    for(const r of runs){
      const pts=[];
      for(const p of mapSimplify(r,tol)){
        const q=[Math.round(p[0]),Math.round(p[1])],l=pts[pts.length-1];
        if(!l||l[0]!==q[0]||l[1]!==q[1])pts.push(q);
      }
      // Short pieces keep each record's bounding box tight for fast culling.
      for(let i=0;i<pts.length-1;i+=MAP_CHUNK-1){
        const part=pts.slice(i,i+MAP_CHUNK);
        recs.push({layer:ln.layer,part});lineBytes+=12+part.length*4;
      }
    }
  }
  let labelBytes=0;for(const l of labels)labelBytes+=7+l.text.length;
  const out=new Uint8Array(32+lineBytes+labelBytes),dv=new DataView(out.buffer);
  out.set([67,51,77,80,1]);                       // "C3MP", version 1
  dv.setUint32(8,out.length,true);
  dv.setInt32(16,Math.round(lat*1e7),true);dv.setInt32(20,Math.round(lon*1e7),true);
  dv.setUint32(24,lineBytes,true);
  let o=32;
  for(const r of recs){
    let x0=32767,y0=32767,x1=-32768,y1=-32768;
    for(const p of r.part){x0=Math.min(x0,p[0]);y0=Math.min(y0,p[1]);x1=Math.max(x1,p[0]);y1=Math.max(y1,p[1])}
    out[o]=r.layer;dv.setUint16(o+2,r.part.length,true);
    dv.setInt16(o+4,x0,true);dv.setInt16(o+6,y0,true);dv.setInt16(o+8,x1,true);dv.setInt16(o+10,y1,true);
    o+=12;
    for(const p of r.part){dv.setInt16(o,p[0],true);dv.setInt16(o+2,p[1],true);o+=4}
  }
  for(const l of labels){
    out[o]=l.layer;out[o+1]=l.rank;dv.setInt16(o+2,l.x,true);dv.setInt16(o+4,l.y,true);out[o+6]=l.text.length;
    for(let i=0;i<l.text.length;i++)out[o+7+i]=l.text.charCodeAt(i);
    o+=7+l.text.length;
  }
  dv.setUint32(12,mapCrc32(out,32),true);
  return out;
}
async function buildMap(lat,lon,maxBytes,say){
  const dlat=MAP_KM/111,dlon=MAP_KM/(111*Math.cos(lat*Math.PI/180));
  const B='('+[lat-dlat,lon-dlon,lat+dlat,lon+dlon].map(v=>v.toFixed(5)).join(',')+')';
  // Same flat projection as the firmware: metres east / north of the centre.
  const k=111000/MAP_UNIT_M,cosLat=Math.cos(lat*Math.PI/180);
  const P=p=>[(p.lon-lon)*k*cosLat,(p.lat-lat)*k];
  const lines=[],labels=[],skipped=[];
  const geoms=e=>e.type==='way'?(e.geometry?[e.geometry]:[]):
    (e.members||[]).filter(m=>m.geometry&&m.role!=='inner').map(m=>m.geometry);
  const add=(layer,els,minLen)=>{for(const e of els)for(const g of geoms(e)){
    const pts=g.filter(Boolean).map(P);
    if(minLen){let len=0;for(let i=1;i<pts.length;i++)len+=Math.hypot(pts[i][0]-pts[i-1][0],pts[i][1]-pts[i-1][1]);
      if(len<minLen)continue}
    lines.push({layer,pts})}};
  const step=async(label,fn)=>{say('Downloading '+label+'\u2026');try{await fn()}catch(e){skipped.push(label)}};
  const ascii=t=>(t||'').normalize('NFD').replace(/[\u0300-\u036f]/g,'').replace(/[^\x20-\x7E]/g,'').trim().slice(0,19);

  await step('coastline',async()=>add(0,await osmQuery('way["natural"="coastline"]'+B+';out geom;')));
  await step('lakes',async()=>{
    let w;
    try{w=await osmQuery('(way["natural"="water"](if:length()>4000)'+B+';relation["natural"="water"](if:length()>6000)'+B+';);out geom;')}
    catch(e){w=await osmQuery('(way["natural"="water"]["name"]'+B+';relation["natural"="water"]["name"]'+B+';);out geom;')}
    add(1,w,1500);                                 // outlines of 3 km or more
  });
  await step('rivers',async()=>add(2,await osmQuery('way["waterway"~"^(river|canal)$"]'+B+';out geom;')));
  await step('borders',async()=>add(3,await osmQuery(
    'relation["boundary"="administrative"]["admin_level"~"^(2|4)$"]'+B+';way(r)'+B+'["maritime"!="yes"];out geom;')));
  await step('roads',async()=>{
    for(const e of await osmQuery('way["highway"~"^(motorway|trunk)$"]'+B+';out geom;'))
      add(e.tags&&e.tags.highway==='motorway'?5:4,[e]);
  });
  await step('towns and airfields',async()=>{
    const els=await osmQuery('(node["place"~"^(city|town|village)$"]["name"]'+B+';way["aeroway"="runway"]'+B+
      ';nwr["aeroway"="aerodrome"]'+B+';);out geom;');
    const towns=[],fields=[];
    for(const e of els){
      const t=e.tags||{};
      if(t.aeroway==='runway'){add(6,[e]);continue}
      const pos=e.type==='node'?e:(e.bounds?{lat:(e.bounds.minlat+e.bounds.maxlat)/2,lon:(e.bounds.minlon+e.bounds.maxlon)/2}:null);
      if(!pos)continue;
      const xy=P(pos).map(Math.round);
      if(Math.abs(xy[0])>20000||Math.abs(xy[1])>20000)continue;
      if(t.aeroway==='aerodrome'){
        const code=ascii(t.icao||t.faa||t.ref||t.local_ref).toUpperCase();
        if(/^[A-Z0-9]{2,6}$/.test(code))fields.push({layer:8,rank:0,x:xy[0],y:xy[1],text:code});
      }else if(t.place){
        const text=ascii(t['name:en']||t.name);
        if(text)towns.push({layer:7,rank:t.place==='city'?3:t.place==='town'?2:1,x:xy[0],y:xy[1],text,
          pop:parseInt(t.population)||0});
      }
    }
    towns.sort((a,b)=>b.rank-a.rank||b.pop-a.pop);
    labels.push(...fields.slice(0,80),...towns.slice(0,400));
  });
  if(!lines.length&&!labels.length)throw new Error('no map data received (is this browser online?)');
  say('Preparing map\u2026');
  // Coarsen the outlines until the file fits the radar's storage.
  for(let tol=20;;tol*=2){
    const blob=mapEncode(lines,labels,tol,lat,lon);
    if(blob.length<=maxBytes)return{blob,skipped};
    if(tol>320)throw new Error('this area has too much detail to fit');
  }
}
/*MAPGEN-END*/

load();shot(0);setInterval(load,5000);
</script></body></html>
)HTML";
