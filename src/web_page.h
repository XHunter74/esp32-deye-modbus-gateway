#pragma once

#include <Arduino.h>

// Single-page web UI (Status / Settings / Scanner / Firmware), served from flash.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Deye RS232 Gateway</title>
<style>
:root{--bg:#252525;--fg:#eaeaea;--card:#333;--acc:#1fa3ec;--bad:#d43535;--ok:#47c266;--mut:#999}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font-family:verdana,sans-serif;font-size:15px}
.wrap{max-width:720px;margin:0 auto;padding:12px 16px}
h1{font-size:20px;text-align:center;margin:8px 0 2px}.sub{text-align:center;color:var(--mut);font-size:13px;margin-bottom:12px}
nav{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:12px}
nav button{flex:1;min-width:120px}
button{background:var(--acc);color:#fff;border:0;border-radius:4px;padding:9px 12px;font-size:15px;cursor:pointer}
button.sel{background:#0e70a4}button.red{background:var(--bad)}button:disabled{opacity:.5}
.card{background:var(--card);border-radius:6px;padding:12px;margin-bottom:12px}
table{width:100%;border-collapse:collapse}td{padding:4px 2px;vertical-align:top}td:first-child{color:var(--mut);width:45%}
label{display:block;margin:8px 0 3px;color:var(--mut);font-size:13px}
input,select{width:100%;padding:7px;border-radius:4px;border:1px solid #555;background:#ddd;color:#000;font-size:15px}
.row{display:flex;gap:8px}.row>div{flex:1}
.mono{font-family:monospace;word-break:break-all}
.hid{display:none}.ok{color:var(--ok)}.bad{color:var(--bad)}.mut{color:var(--mut)}
#res{max-height:420px;overflow:auto}#res td{font-family:monospace;padding:2px 6px}#res td:first-child{width:auto}
progress{width:100%}
</style></head><body><div class="wrap">
<h1>Deye RS232 Gateway</h1><div class="sub" id="sub">&nbsp;</div>
<nav><button data-t="status" class="sel">Status</button><button data-t="settings">Settings</button>
<button data-t="scanner">Scanner</button><button data-t="firmware">Firmware</button></nav>

<section id="status">
<div class="card"><table id="st"></table></div>
<div class="card"><table>
<tr><td>Last request</td><td class="mono" id="lreq"></td></tr>
<tr><td>Last response</td><td class="mono" id="lresp"></td></tr></table></div>
</section>

<section id="settings" class="hid"><form id="cfg" class="card">
<label>Gateway mode</label><select name="mode">
<option value="0">Modbus RTU over TCP (raw frames, ioBroker.deyeidc RS485 mode)</option>
<option value="1">Modbus TCP (MBAP)</option></select>
<div class="row"><div><label>TCP port</label><input name="port" type="number" min="1" max="65535"></div>
<div><label>Response timeout, ms</label><input name="tmo" type="number" min="50" max="10000"></div></div>
<div class="row"><div><label>Baud rate</label><select name="baud">
<option>1200</option><option>2400</option><option>4800</option><option>9600</option><option>19200</option>
<option>38400</option><option>57600</option><option>115200</option></select></div>
<div><label>Data bits</label><select name="bits"><option>7</option><option>8</option></select></div>
<div><label>Parity</label><select name="par"><option value="N">None</option><option value="E">Even</option><option value="O">Odd</option></select></div>
<div><label>Stop bits</label><select name="stop"><option>1</option><option>2</option></select></div></div>
<div class="row"><div><label>RX pin</label><input name="rx" type="number" min="0" max="39"></div>
<div><label>TX pin</label><input name="tx" type="number" min="0" max="33"></div>
<div><label>Frame gap, ms</label><input name="gap" type="number" min="2" max="500"></div></div>
<label>Hostname</label><input name="host" maxlength="31" pattern="[A-Za-z0-9\-]+">
<label>Web password (user "admin", empty = no password)</label><input name="pass" type="password" maxlength="32" placeholder="unchanged">
<label><input type="checkbox" name="clearpass" value="1" style="width:auto"> remove password</label>
<p><button type="submit">Save &amp; reboot</button></p>
</form>
<div class="card"><button id="reboot">Reboot</button> <button id="wifireset" class="red">Reset WiFi settings</button></div>
</section>

<section id="scanner" class="hid"><form id="scan" class="card">
<div class="row"><div><label>Slave ID</label><input name="unit" type="number" min="0" max="247" value="1"></div>
<div><label>Function</label><select name="fc"><option value="3">03 holding</option><option value="4">04 input</option></select></div>
<div><label>Block size</label><input name="chunk" type="number" min="1" max="125" value="32"></div></div>
<div class="row"><div><label>Start address</label><input name="start" type="number" min="0" max="65535" value="0"></div>
<div><label>Count (max 4096)</label><input name="count" type="number" min="1" max="4096" value="100"></div></div>
<p><button type="submit" id="sgo">Start</button> <button type="button" id="sstop" class="red">Stop</button>
<a href="/api/scan.csv"><button type="button">Download CSV</button></a>
<label><input type="checkbox" id="onlyok" style="width:auto"> show only answered registers</label></p>
<progress id="sprog" value="0" max="1"></progress> <span id="sinfo" class="mut"></span>
</form>
<div class="card" id="res"><table><thead><tr><td>Addr</td><td>Hex</td><td>Value</td><td>Signed</td><td>Hex value</td></tr></thead><tbody id="rows"></tbody></table></div>
</section>

<section id="firmware" class="hid"><form id="fw" class="card">
<label>Firmware file (.bin from PlatformIO: .pio/build/esp32dev/firmware.bin)</label>
<input type="file" name="update" accept=".bin">
<p><button type="submit">Upload &amp; flash</button></p><progress id="fwprog" value="0" max="1"></progress>
<div id="fwmsg" class="mut"></div></form></section>
</div>
<script>
const $=s=>document.querySelector(s);
const tabs=document.querySelectorAll('nav button');
tabs.forEach(b=>b.onclick=()=>{tabs.forEach(x=>x.classList.toggle('sel',x==b));
document.querySelectorAll('section').forEach(s=>s.classList.toggle('hid',s.id!=b.dataset.t));
if(b.dataset.t=='settings')loadCfg();});
function up(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d+'d '+h+'h '+m+'m';}
async function status(){try{const r=await fetch('/api/status');const j=await r.json();
$('#sub').textContent=j.host+' · '+j.ip+' · v'+j.version;
const modes=['RTU over TCP','Modbus TCP (MBAP)'];
const rows=[['Mode',modes[j.mode]+' on port '+j.port],['Serial',j.serial],['WiFi',j.ssid+' ('+j.rssi+' dBm)'],
['Uptime',up(j.uptime)],['Free heap',j.heap+' bytes'],['TCP clients',j.clients.length?j.clients.join('<br>'):'none'],
['Requests',j.requests],['Responses',j.responses],['Exceptions',j.exceptions],
['Timeouts','<span class="'+(j.timeouts?'bad':'')+'">'+j.timeouts+'</span>'],
['CRC errors','<span class="'+(j.crc?'bad':'')+'">'+j.crc+'</span>'],
['Last activity',j.lastAgo<0?'never':j.lastAgo+' s ago']];
$('#st').innerHTML=rows.map(r=>'<tr><td>'+r[0]+'</td><td>'+r[1]+'</td></tr>').join('');
$('#lreq').textContent=j.lastReq;$('#lresp').textContent=j.lastResp;}catch(e){}}
setInterval(status,2000);status();
async function loadCfg(){const j=await (await fetch('/api/config')).json();const f=$('#cfg');
for(const k in j){const e=f.elements[k];if(e&&k!='pass')e.value=j[k];}}
$('#cfg').onsubmit=async e=>{e.preventDefault();const fd=new FormData(e.target);
if(!confirm('Save settings and reboot?'))return;
const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams(fd)});alert(await r.text());};
$('#reboot').onclick=async()=>{if(confirm('Reboot?')){await fetch('/api/reboot',{method:'POST'});alert('Rebooting');}};
$('#wifireset').onclick=async()=>{if(confirm('Erase WiFi credentials? The gateway will open the setup access point.')){
await fetch('/api/wifireset',{method:'POST'});alert('Rebooting into setup AP');}};
let next=0,timer=null;
function h4(v){return '0x'+v.toString(16).toUpperCase().padStart(4,'0');}
const ST={2:'timeout',3:'CRC error',4:'exception '};
async function poll(){const j=await (await fetch('/api/scan?from='+next)).json();
$('#sprog').max=j.count||1;$('#sprog').value=j.done;
$('#sinfo').textContent=j.done+' / '+j.count+(j.running?' scanning…':' done');
const only=$('#onlyok').checked;let html='';
for(const [a,s,v] of j.rows){if(s!=1&&only)continue;
html+=s==1?'<tr><td>'+a+'</td><td>'+h4(a)+'</td><td class="ok">'+v+'</td><td>'+(v>32767?v-65536:v)+'</td><td>'+h4(v)+'</td></tr>'
:'<tr><td>'+a+'</td><td>'+h4(a)+'</td><td class="bad" colspan="3">'+ST[s]+(s==4?v:'')+'</td></tr>';}
$('#rows').insertAdjacentHTML('beforeend',html);next=j.from+j.rows.length;
if(!j.running&&next>=j.done){clearInterval(timer);timer=null;$('#sgo').disabled=false;}}
$('#scan').onsubmit=async e=>{e.preventDefault();
const r=await fetch('/api/scan/start',{method:'POST',body:new URLSearchParams(new FormData(e.target))});
if(!r.ok){alert(await r.text());return;}
$('#rows').innerHTML='';next=0;$('#sgo').disabled=true;if(!timer)timer=setInterval(poll,700);};
$('#sstop').onclick=()=>fetch('/api/scan/stop',{method:'POST'});
$('#fw').onsubmit=e=>{e.preventDefault();const f=e.target.update.files[0];if(!f)return;
const fd=new FormData();fd.append('update',f,f.name);const x=new XMLHttpRequest();
x.upload.onprogress=p=>{$('#fwprog').max=p.total;$('#fwprog').value=p.loaded;};
x.onload=()=>{$('#fwmsg').textContent=x.responseText;};x.onerror=()=>{$('#fwmsg').textContent='Upload failed';};
x.open('POST','/update');x.send(fd);$('#fwmsg').textContent='Uploading…';};
</script></body></html>)HTML";
