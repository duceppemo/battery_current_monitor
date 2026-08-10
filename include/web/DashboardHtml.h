#pragma once

#include <Arduino.h>

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Battery Monitor</title>
<style>
:root{color-scheme:dark;--bg:#0b1016;--panel:#151c24;--panel2:#1b2530;--text:#eef4fa;--muted:#91a1b2;--accent:#53c8ff;--good:#48d597;--bad:#ff646e;--border:#283645}
*{box-sizing:border-box} body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:var(--bg);color:var(--text)}
.container{max-width:900px;margin:auto;padding:20px} header{margin-bottom:20px} h1{margin:0;font-size:1.7rem}.subtitle{margin-top:6px;color:var(--muted);font-size:.9rem}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}.card{background:var(--panel);border:1px solid var(--border);border-radius:14px;padding:18px}
.metric-name{color:var(--muted);text-transform:uppercase;letter-spacing:.08em;font-size:.75rem}.metric-value{margin-top:8px;font-size:2rem;font-weight:600;font-variant-numeric:tabular-nums}
.extrema{margin-top:14px;display:grid;grid-template-columns:1fr 1fr;gap:8px}.extrema div{background:var(--panel2);border-radius:8px;padding:8px 10px}.label{display:block;color:var(--muted);font-size:.7rem;text-transform:uppercase}.extrema-value{display:block;margin-top:3px;font-variant-numeric:tabular-nums}
.status-panel{margin-top:14px;display:flex;flex-wrap:wrap;gap:10px}.status{background:var(--panel);border:1px solid var(--border);border-radius:20px;padding:7px 12px;font-size:.82rem}.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;background:var(--muted)}.good .dot{background:var(--good)}.bad .dot{background:var(--bad)}
.energy-grid{margin-top:14px;display:grid;grid-template-columns:1fr 1fr;gap:8px}.energy-grid div{background:var(--panel2);border-radius:8px;padding:8px 10px}.energy-value{display:block;margin-top:3px;font-variant-numeric:tabular-nums}
.actions{margin-top:20px;display:flex;gap:10px;flex-wrap:wrap}button{appearance:none;border:1px solid var(--border);background:var(--panel2);color:var(--text);padding:11px 16px;border-radius:9px;font-size:.9rem;cursor:pointer}button:hover{border-color:var(--accent)}footer{margin-top:24px;color:var(--muted);font-size:.75rem}
</style>
</head>
<body>
<div class="container">
<header><h1>Battery Monitor</h1><div class="subtitle">INA228 · ESP32-C3 · Live telemetry</div></header>
<div class="grid">
<div class="card"><div class="metric-name">Voltage</div><div class="metric-value" id="voltage">--</div><div class="extrema"><div><span class="label">Minimum</span><span class="extrema-value" id="voltage-min">--</span></div><div><span class="label">Maximum</span><span class="extrema-value" id="voltage-max">--</span></div></div></div>
<div class="card"><div class="metric-name">Current</div><div class="metric-value" id="current">--</div><div class="extrema"><div><span class="label">Minimum</span><span class="extrema-value" id="current-min">--</span></div><div><span class="label">Maximum</span><span class="extrema-value" id="current-max">--</span></div></div></div>
<div class="card"><div class="metric-name">Power</div><div class="metric-value" id="power">--</div><div class="extrema"><div><span class="label">Minimum</span><span class="extrema-value" id="power-min">--</span></div><div><span class="label">Maximum</span><span class="extrema-value" id="power-max">--</span></div></div></div>
<div class="card"><div class="metric-name">Temperature</div><div class="metric-value" id="temperature">--</div><div class="extrema"><div><span class="label">Minimum</span><span class="extrema-value" id="temperature-min">--</span></div><div><span class="label">Maximum</span><span class="extrema-value" id="temperature-max">--</span></div></div></div>
<div class="card"><div class="metric-name">Shunt voltage</div><div class="metric-value" id="shuntVoltage">--</div><div class="extrema"><div><span class="label">Minimum</span><span class="extrema-value" id="shuntVoltage-min">--</span></div><div><span class="label">Maximum</span><span class="extrema-value" id="shuntVoltage-max">--</span></div></div></div>
<div class="card"><div class="metric-name">Session energy</div><div class="energy-grid"><div><span class="label">Net charge</span><span class="energy-value" id="net-ah">--</span></div><div><span class="label">Net energy</span><span class="energy-value" id="net-wh">--</span></div><div><span class="label">Discharged</span><span class="energy-value" id="discharged-ah">--</span></div><div><span class="label">Charged</span><span class="energy-value" id="charged-ah">--</span></div></div></div>
</div>
<div class="status-panel">
<div class="status" id="sensor-status"><span class="dot"></span>INA228</div>
<div class="status" id="ble-status"><span class="dot"></span>BLE</div>
<div class="status" id="wifi-status"><span class="dot"></span>Wi-Fi AP</div>
<div class="status" id="i2c-status"><span class="dot"></span>I2C</div>
<div class="status" id="display-status"><span class="dot"></span>Display</div>
</div>
<div class="actions"><button onclick="resetExtrema()">Reset min / max</button><button onclick="resetSession()">Reset Ah / Wh</button><button id="display-toggle" onclick="toggleDisplay()">Turn display off</button></div>
<footer><span id="uptime">Uptime --</span> · <span id="clients">0 Wi-Fi clients</span> · <span id="reset-reason">Last reset: --</span></footer>
</div>
<script>
const num=v=>typeof v==='number'&&Number.isFinite(v);
const fmtV=v=>num(v)?v.toFixed(3)+' V':'--';
const fmtI=v=>!num(v)?'--':Math.abs(v)<1?(v*1000).toFixed(3)+' mA':v.toFixed(3)+' A';
const fmtP=v=>!num(v)?'--':Math.abs(v)<1?(v*1000).toFixed(3)+' mW':v.toFixed(2)+' W';
const fmtT=v=>num(v)?v.toFixed(1)+' °C':'--';
const fmtS=v=>num(v)?(v*1000).toFixed(4)+' mV':'--';
const fmtAh=v=>!num(v)?'--':Math.abs(v)<1?(v*1000).toFixed(3)+' mAh':v.toFixed(4)+' Ah';
const fmtWh=v=>!num(v)?'--':Math.abs(v)<1?(v*1000).toFixed(3)+' mWh':v.toFixed(4)+' Wh';
function metric(name,data,fmt){document.getElementById(name).textContent=fmt(data.value);document.getElementById(name+'-min').textContent=fmt(data.min);document.getElementById(name+'-max').textContent=fmt(data.max)}
function energy(data){document.getElementById('net-ah').textContent=fmtAh(data.netAh);document.getElementById('net-wh').textContent=fmtWh(data.netWh);document.getElementById('discharged-ah').textContent=fmtAh(data.dischargedAh);document.getElementById('charged-ah').textContent=fmtAh(data.chargedAh)}
function status(id,ok,text){const e=document.getElementById(id);e.classList.remove('good','bad');e.classList.add(ok?'good':'bad');e.innerHTML='<span class="dot"></span>'+text}
function uptime(s){s=Math.floor(s);const d=Math.floor(s/86400);s%=86400;const h=Math.floor(s/3600);s%=3600;const m=Math.floor(s/60);return d?d+'d '+h+'h':h?h+'h '+m+'m':m+'m'}
async function refresh(){try{const r=await fetch('/api/telemetry',{cache:'no-store'});if(!r.ok)throw Error();const d=await r.json();metric('voltage',d.voltage,fmtV);metric('current',d.current,fmtI);metric('power',d.power,fmtP);metric('temperature',d.temperature,fmtT);metric('shuntVoltage',d.shuntVoltage,fmtS);energy(d.energy);status('sensor-status',d.sensorOK,d.sensorOK?'INA228 OK':'INA228 error');status('ble-status',d.bleConnected||d.bleAdvertising,d.bleConnected?'BLE connected':d.bleAdvertising?'BLE advertising':'BLE unavailable');status('wifi-status',d.accessPointReady,d.accessPointReady?'Wi-Fi AP ready':'Wi-Fi AP restarting');status('i2c-status',d.failedSamples===0,'Incomplete samples: '+d.failedSamples);status('display-status',d.displayOn,d.displayOn?'Display on':'Display off');document.getElementById('display-toggle').textContent=d.displayOn?'Turn display off':'Turn display on';document.getElementById('uptime').textContent='Uptime '+uptime(d.uptimeSeconds);document.getElementById('clients').textContent=d.wifiClients+(d.wifiClients===1?' Wi-Fi client':' Wi-Fi clients');document.getElementById('reset-reason').textContent='Last reset: '+d.resetReason}catch(e){status('sensor-status',false,'Connection lost')}}
async function resetExtrema(){await fetch('/api/reset-extrema',{method:'POST',cache:'no-store'});refresh()}
async function resetSession(){await fetch('/api/reset-session',{method:'POST',cache:'no-store'});setTimeout(refresh,100)}
async function toggleDisplay(){await fetch('/api/toggle-display',{method:'POST',cache:'no-store'});setTimeout(refresh,100)}
refresh();setInterval(refresh,500);
</script>
</body>
</html>
)HTML";
