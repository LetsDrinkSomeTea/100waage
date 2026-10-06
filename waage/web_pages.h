#pragma once
#include <pgmspace.h>

// ── Statische Seiten des Config-AP ────────────────────────────────────────────
// Werden mit send_P ausgeliefert, alle Daten kommen ueber die JSON-API (web.h).
// Kein Internet im AP: alles inline. Werte von Geraet/Nutzer nur per
// textContent/value setzen.

static const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>100-Waage</title><style>
*{box-sizing:border-box}body{font-family:Arial,sans-serif;max-width:500px;margin:16px auto;padding:0 16px;color:#222;background:#fff}
h2,h3{text-align:center;margin-top:0}
.st{background:#f0f4f8;border-radius:8px;padding:10px 14px;margin-bottom:16px;font-size:14px;display:flex;gap:6px 16px;flex-wrap:wrap}.st span{white-space:nowrap}.off{opacity:.5}
.sec{border:1px solid #e0e0e0;border-radius:8px;padding:16px;margin-bottom:16px}
label{display:block;font-weight:bold;font-size:14px;margin-bottom:14px}label i{display:block;font:normal 12px Arial,sans-serif;color:#666;margin:2px 0 4px}
input,select{width:100%;padding:8px;border:1px solid #ccc;border-radius:4px;font-size:16px;margin-top:2px}
.cb{font-weight:normal}.cb input{width:auto;margin:0 6px 0 0}
.bad{border-color:#f44336;background:#fff5f5}.fe{display:block;color:#d32f2f;font-weight:normal;font-size:13px}.w{color:#e65100}
button,.btn{display:block;width:100%;padding:12px;color:#fff;border:0;border-radius:4px;font-size:15px;margin-top:8px;text-align:center;text-decoration:none;cursor:pointer}button:disabled{opacity:.5}
.g{background:#4CAF50}.o{background:#FF9800}
.msg{text-align:center;font-size:14px;margin:8px 0 0;color:#d32f2f}.ok{color:#2e7d32}.inf{color:#1976D2}
.bd{background:#f44336;color:#fff;border-radius:4px;padding:0 5px;font-size:12px}small{color:#666}[hidden]{display:none!important}
</style></head><body>
<h2>⚖️ 100-Waage</h2>
<div class="st" id="st"><span>Gewicht: <b id="w">--</b> g</span><span>Modus: <b id="m">--</b></span><span>Phase: <b id="p">--</b></span><span>Ziel: <b id="z">--</b></span><span>Akku: <b id="a">--</b> <span class="bd" id="al" hidden>schwach</span></span><span>Funk/AP: <b id="f">--</b></span><small id="fw"></small></div>
<form id="cf" class="sec" novalidate><h3>Einstellungen</h3>
<label>Modus<select name="scaleMode"><option value="Game">Game (Trinkspiel)</option><option value="Standard">Standard (Waage)</option></select><i class="w" id="bz" hidden>Spiel läuft – erst Taste drücken</i></label>
<label>Zielgewicht [g]<i>Wie viel soll getrunken werden?</i><input type="number" step="0.1" name="goal"></label>
<label class="cb"><input type="checkbox" name="randomModeEnabled">Zufälliges Zielgewicht</label>
<label>Zufall-Minimum [g]<i>Maximum ist das Zielgewicht</i><input type="number" step="0.1" name="randomMin"></label>
<label>Display-Rotation<select name="displayRotation"><option value="0">Normal (0°)</option><option value="2">Gedreht (180°)</option></select></label>
<button class="g">💾 Speichern</button><p class="msg" id="msg" aria-live="polite"></p></form>
<a href="/admin" class="btn o">🔒 Admin-Einstellungen</a>
<script>
let up=0;
const $=i=>document.getElementById(i),
de=(v,d)=>v==null||!isFinite(v)?'--':(+v).toFixed(d).replace('.',',').replace(/^-(?=[0,]+$)/,''),
live=()=>!up&&document.visibilityState=='visible';
async function api(u,b){const c=new AbortController(),t=setTimeout(()=>c.abort(),5000);let r,j;
try{r=await fetch(u,{method:b?'POST':'GET',body:b,signal:c.signal,cache:'no-store'});j=await r.json()}catch(e){}
clearTimeout(t);return{s:j?r.status:0,j:j||{error:r?'Fehler ('+r.status+')':'Keine Verbindung zur Waage'}}}
function every(f,ms){let t,b=0,a=0;const run=async()=>{clearTimeout(t);if(b){a=1;return}b=1;let go;try{go=await f()}catch(e){}b=0;
if(a){a=0;return run()}if(go!==false&&live())t=setTimeout(()=>live()&&run(),ms)};return run}
function say(e,t,c){t=t||'';if(e.textContent!=t)e.textContent=t;e.className='msg '+(c||'')}
function val(e){return e.type=='checkbox'?(e.checked?'1':'0'):e.value}
function fill(f,c){f.o=f.o||{};for(const k in c){const e=f.elements[k];if(e&&e.name==k){if(e.type=='checkbox')e.checked=!!c[k];else e.value=c[k];f.o[k]=val(e)}}}
function diff(f){const p=new URLSearchParams(),o=f.o||{};for(const e of f.elements){const v=val(e);if(e.name&&!e.disabled&&v!==(o[e.name]==null?'':o[e.name]))p.append(e.name,v)}return p}
function clr(f){f.querySelectorAll('.fe').forEach(e=>e.remove());f.querySelectorAll('.bad').forEach(e=>e.classList.remove('bad'))}
function ferr(f,n,t){const e=f.elements[n],l=e&&e.closest&&e.closest('label');if(!l)return 0;e.classList.add('bad');
const s=document.createElement('span');s.className='fe';s.textContent=t;l.appendChild(s);e.focus();return 1}
function chk(f){for(const e of f.elements)if(e.validity&&e.validity.badInput)return!ferr(f,e.name,'Keine gültige Zahl');return 1}
function res(f,m,r){if(r.s>=200&&r.s<300)return 1;say(m,r.s==400&&r.j.field&&ferr(f,r.j.field,r.j.error||'Ungültig')?'':r.j.error);return 0}
const F=$('cf'),M=$('msg'),S=F.elements.scaleMode,
PH={Idle:'Bereit für Glas',Taring:'Tara…',Ready:'Glas steht',Drinking:'Trinken…',Result:'Ergebnis',Calibration:'Kalibrierung',SensorError:'Sensorfehler'};
const P=every(async()=>{const r=await api('/api/status'),d=r.j,b=d.battery;$('st').classList.toggle('off',r.s!=200);if(r.s!=200)return;
$('w').textContent=de(d.weight,2);$('m').textContent=d.mode||'--';$('p').textContent=PH[d.phase]||d.phase||'--';
$('z').textContent=de(d.goal,1)+' g'+(d.random?' (Zufall)':'');
$('a').textContent=b?de(b.percent,0)+' % ('+de(b.voltage,2)+' V)':'kein Akku';$('al').hidden=!(b&&b.low);
$('f').textContent=(d.radio?'an':'aus')+' / '+(d.ap?'an':'aus');$('fw').textContent=d.fw||'';
S.disabled=!!d.busy;$('bz').hidden=!d.busy;
if(d.mode&&F.o&&F.o.scaleMode!=d.mode&&S.value==F.o.scaleMode){S.value=d.mode;F.o.scaleMode=d.mode}},1000);
async function load(){const r=await api('/api/config');if(r.s==200){if(!F.o)say(M,'');fill(F,r.j)}
else{say(M,'Einstellungen nicht geladen');setTimeout(load,3000)}}
F.onsubmit=async e=>{e.preventDefault();clr(F);if(!F.o)return say(M,'Einstellungen nicht geladen');if(!chk(F))return say(M,'');
const B=F.querySelector('button');B.disabled=true;say(M,'Speichere…','inf');
const r=await api('/api/config',diff(F));B.disabled=false;
if(res(F,M,r)){fill(F,r.j.config||{});say(M,r.j.applied=='next'?'Gespeichert – gilt ab der nächsten Runde':'Gespeichert – gilt sofort','ok');P()}};
document.addEventListener('visibilitychange',()=>{if(live())P()});
load();P();
</script></body></html>)html";

static const char LOGIN_HTML[] PROGMEM = R"html(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Admin-Login – 100-Waage</title><style>
*{box-sizing:border-box}body{font-family:Arial,sans-serif;max-width:500px;margin:16px auto;padding:0 16px;color:#222;background:#fff}
h2,h3{text-align:center;margin-top:0}
.sec{border:1px solid #FFB74D;border-radius:8px;padding:16px;margin-bottom:16px}
label{display:block;font-weight:bold;font-size:14px;margin-bottom:14px}
input{width:100%;padding:8px;border:1px solid #ccc;border-radius:4px;font-size:16px;margin-top:2px}
button,.btn{display:block;width:100%;padding:12px;color:#fff;border:0;border-radius:4px;font-size:15px;margin-top:8px;text-align:center;text-decoration:none;cursor:pointer}
.g{background:#4CAF50}.o{background:#FF9800}
.msg{text-align:center;font-size:14px;margin:0 0 12px;color:#d32f2f}[hidden]{display:none!important}
</style></head><body>
<h2>⚖️ 100-Waage</h2>
<form class="sec" method="post" action="/login"><h3>🔒 Admin-Login</h3><p class="msg" id="e" role="alert" hidden></p>
<label>Passwort<input type="password" name="password" required autofocus autocomplete="current-password"></label>
<button class="o">Einloggen</button></form>
<a href="/" class="btn g">← Zurück</a>
<script>
const m={1:'Falsches Passwort',2:'Zu viele Versuche – bitte 30 s warten'}[new URLSearchParams(location.search).get('e')],e=document.getElementById('e');
if(m){e.textContent=m;e.hidden=false}
</script></body></html>)html";

static const char ADMIN_HTML[] PROGMEM = R"html(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Admin – 100-Waage</title><style>
*{box-sizing:border-box}body{font-family:Arial,sans-serif;max-width:500px;margin:16px auto;padding:0 16px;color:#222;background:#fff}
h2,h3{text-align:center;margin-top:0}p{font-size:14px;margin:6px 0}
.sec{border:1px solid #e0e0e0;border-radius:8px;padding:16px;margin-bottom:16px}.adm{border-color:#FFB74D}
label{display:block;font-weight:bold;font-size:14px;margin-bottom:14px}label i,.h{display:block;font:normal 12px Arial,sans-serif;color:#666;margin:2px 0 4px}
input,select{width:100%;padding:8px;border:1px solid #ccc;border-radius:4px;font-size:16px;margin-top:2px}input+input{margin-top:6px}
.cb{font-weight:normal}.cb input{width:auto;margin:0 6px 0 0}
.bad{border-color:#f44336;background:#fff5f5}.fe{display:block;color:#d32f2f;font-weight:normal;font-size:13px}
button,.btn{display:block;width:100%;padding:12px;color:#fff;border:0;border-radius:4px;font-size:15px;margin-top:8px;text-align:center;text-decoration:none;cursor:pointer}button:disabled{opacity:.5}
.g{background:#4CAF50}.o{background:#FF9800}.b{background:#2196F3}.r{background:#f44336}
.msg{text-align:center;font-size:14px;margin:8px 0 0;color:#d32f2f}.ok{color:#2e7d32}.inf{color:#1976D2}
.bd{background:#f44336;color:#fff;border-radius:4px;padding:0 5px;font-size:12px}[hidden]{display:none!important}
table{width:100%;border-collapse:collapse;font-size:13px}td{padding:3px 4px;border-bottom:1px solid #eee}.me{font-weight:bold}progress{width:100%;margin-top:8px}
</style></head><body>
<h2>⚖️ 100-Waage</h2>
<form id="af" class="sec adm" novalidate><h3>🔒 Admin-Einstellungen</h3>
<label>WLAN-Name (SSID)<i>Standard "100-Waage-Config" = automatisch eindeutig</i><input name="apSSID" maxlength="32" autocomplete="off"><i>Aktiv: <b id="apn">--</b></i></label>
<label>Toleranz [g]<i>Messtoleranz für Start/Stop-Erkennung</i><input type="number" step="0.1" name="tolerance"></label>
<label>Auto-Reset-Bereich [%]<i>Ergebnisse außerhalb dieses Bereichs werden automatisch zurückgesetzt (0–100)</i><input type="number" min="0" max="100" name="autoResetRange"></label>
<label>AP-Auto-Aus [min]<i>0 = nie</i><input type="number" min="0" max="255" name="wifiTimeout"></label>
<label>Deep-Sleep [min]<i>Nach Inaktivität, 0 = nie</i><input type="number" min="0" max="255" name="sleepTimeout"></label>
<label class="cb"><input type="checkbox" name="autoZeroEnabled">Auto-Zero (Nullabgleich bei stabil leerer Waage)</label>
<label>Auto-Zero-Schwelle [g]<i>Maximalgewicht, das als leer gilt</i><input type="number" step="0.1" name="autoZeroThreshold"></label>
<label>Auto-Zero-Verzögerung [s]<i>So lange muss die Waage stabil leer sein</i><input type="number" min="1" max="60" name="autoZeroDelay"></label>
<label>Neues Passwort<i>Leer lassen = unverändert (mind. 4 Zeichen)</i><input type="password" name="newPassword" maxlength="31" autocomplete="new-password"><input type="password" id="pw2" maxlength="31" placeholder="Passwort wiederholen" aria-label="Passwort wiederholen" autocomplete="new-password"></label>
<button class="o">💾 Speichern</button><p class="msg" id="am" aria-live="polite"></p></form>
<div class="sec adm"><h3>🔋 Akku</h3><p>Aktuell: <b id="bv">--</b> <span class="bd" id="bl" hidden>schwach</span></p>
<form id="bf" novalidate><label>Gemessene Akkuspannung [V]<i>Mit dem Multimeter direkt an den Akkupolen messen, während der Funk an ist.</i><input type="number" step="0.01" name="measuredV"></label><button class="b">Abgleichen</button></form>
<button class="o" id="br">Zurücksetzen (2,0)</button><p class="msg" id="bm" aria-live="polite"></p></div>
<div class="sec adm"><h3>🎯 Waage kalibrieren</h3><p class="h" id="fa"></p><p class="msg" id="cs" aria-live="polite"></p>
<form id="cw" novalidate hidden><label>Bekanntes Gewicht [g]<i>Gewicht auflegen, Wert eingeben, dann messen</i><input type="number" step="0.1" name="weight"></label><p>Rohwert-Änderung: <b id="cd">--</b></p><button class="b">Messen</button></form>
<button class="b" id="c0">🎯 Kalibrierung starten</button><button class="r" id="cx" hidden>Abbrechen</button><button class="g" id="ck" hidden>OK</button></div>
<div class="sec adm"><h3>📤 Firmware</h3><p>Version: <b id="fv">--</b> · Protokoll: <b id="pv">--</b></p>
<label>Firmware-Datei (.bin)<input type="file" id="ff" accept=".bin,application/octet-stream"></label><progress id="fp" max="100" value="0" aria-label="Upload" hidden></progress>
<button class="b" id="fu">⬆️ Update starten</button><p class="msg" id="fm" aria-live="polite"></p></div>
<div class="sec"><h3>⚔️ Duell-Debug</h3><div id="du">--</div></div>
<a href="/logout" class="btn r">🔓 Ausloggen</a><a href="/" class="btn g">← Zurück</a>
<script>
let up=0,fw0;
const $=i=>document.getElementById(i),
de=(v,d)=>v==null||!isFinite(v)?'--':(+v).toFixed(d).replace('.',',').replace(/^-(?=[0,]+$)/,''),
live=()=>!up&&document.visibilityState=='visible',
hx=p=>p==null?'--':'0x'+(+p).toString(16).toUpperCase(),
m5=m=>String(m||'--').slice(-5),
post=(u,o)=>aapi(u,new URLSearchParams(o||{}));
async function api(u,b){const c=new AbortController(),t=setTimeout(()=>c.abort(),5000);let r,j;
try{r=await fetch(u,{method:b?'POST':'GET',body:b,signal:c.signal,cache:'no-store'});j=await r.json()}catch(e){}
clearTimeout(t);return{s:j?r.status:0,j:j||{error:r?'Fehler ('+r.status+')':'Keine Verbindung zur Waage'}}}
async function aapi(u,b){const r=await api(u,b);if(r.s==401){up=1;r.j={error:'Nicht eingeloggt – weiter zum Login…'};location.href='/login'}return r}
function every(f,ms){let t,b=0,a=0;const run=async()=>{clearTimeout(t);if(b){a=1;return}b=1;let go;try{go=await f()}catch(e){}b=0;
if(a){a=0;return run()}if(go!==false&&live())t=setTimeout(()=>live()&&run(),ms)};return run}
function say(e,t,c){t=t||'';if(e.textContent!=t)e.textContent=t;e.className='msg '+(c||'')}
function el(t,p,x){const e=document.createElement(t);if(x!=null)e.textContent=x;p.appendChild(e);return e}
function val(e){return e.type=='checkbox'?(e.checked?'1':'0'):e.value}
function fill(f,c){f.o=f.o||{};for(const k in c){const e=f.elements[k];if(e&&e.name==k){if(e.type=='checkbox')e.checked=!!c[k];else e.value=c[k];f.o[k]=val(e)}}}
function diff(f){const p=new URLSearchParams(),o=f.o||{};for(const e of f.elements){const v=val(e);if(e.name&&!e.disabled&&v!==(o[e.name]==null?'':o[e.name]))p.append(e.name,v)}return p}
function clr(f){f.querySelectorAll('.fe').forEach(e=>e.remove());f.querySelectorAll('.bad').forEach(e=>e.classList.remove('bad'))}
function ferr(f,n,t){const e=f.elements[n],l=e&&e.closest&&e.closest('label');if(!l)return 0;e.classList.add('bad');
const s=document.createElement('span');s.className='fe';s.textContent=t;l.appendChild(s);e.focus();return 1}
function chk(f){for(const e of f.elements)if(e.validity&&e.validity.badInput)return!ferr(f,e.name,'Keine gültige Zahl');return 1}
function res(f,m,r){if(r.s>=200&&r.s<300)return 1;say(m,r.s==400&&r.j.field&&ferr(f,r.j.field,r.j.error||'Ungültig')?'':r.j.error);return 0}
const A=$('af'),AM=$('am');
async function lc(){const r=await aapi('/api/admin/config'),j=r.j;if(r.s!=200){say(AM,j.error);if(!up)setTimeout(lc,3000);return}
if(!A.o)say(AM,'');fill(A,j);$('apn').textContent=j.apName||'--';$('fv').textContent=j.fw||'--';$('pv').textContent=hx(j.proto);fw0=j.fw}
A.onsubmit=async e=>{e.preventDefault();clr(A);if(!A.o)return say(AM,'Einstellungen nicht geladen');if(!chk(A))return say(AM,'');
const n=A.elements.newPassword.value,B=A.querySelector('button');
if(n&&n.length<4)return ferr(A,'newPassword','Mindestens 4 Zeichen');
if(n!=$('pw2').value)return ferr(A,'newPassword','Passwörter stimmen nicht überein');
B.disabled=true;say(AM,'Speichere…','inf');const r=await aapi('/api/admin/config',diff(A)),j=r.j;B.disabled=false;
if(!res(A,AM,r))return;
let t='Gespeichert – ohne Neustart übernommen';
if(j.apRestart)t+='. WLAN wird neu gestartet: bitte mit '+j.apName+' neu verbinden';
if(j.relogin){t+='. Bitte neu einloggen…';up=1;setTimeout(()=>location.href='/login',2000)}
say(AM,t,'ok');A.elements.newPassword.value=$('pw2').value='';
A.o={};for(const x of A.elements)if(x.name)A.o[x.name]=val(x);
if(j.apName)$('apn').textContent=j.apName;if(!j.apRestart&&!j.relogin)lc()};
const P=every(async()=>{const r=await api('/api/status'),b=r.j.battery;if(r.s!=200)return;fw0=fw0||r.j.fw;
$('bv').textContent=b?de(b.percent,0)+' % · '+de(b.voltage,2)+' V · Pin '+de(b.pinMv,0)+' mV · Teiler '+de(b.ratio,3):'kein Akku';
$('bl').hidden=!(b&&b.low)},3000);
const BF=$('bf'),BM=$('bm');
async function bc(o){clr(BF);say(BM,'Gleiche ab…','inf');const r=await post('/api/admin/battcal',o),j=r.j;
if(res(BF,BM,r)){say(BM,'Teiler '+de(j.ratio,3)+' – Akku '+de(j.voltage,2)+' V ('+de(j.percent,0)+' %)','ok');P()}}
BF.onsubmit=e=>{e.preventDefault();clr(BF);if(chk(BF))bc({measuredV:BF.elements.measuredV.value});else say(BM,'')};
$('br').onclick=()=>bc({reset:1});
const CS={Prepare:'Waage leeren…',Taring:'Tara…',WaitWeight:'Bekanntes Gewicht auflegen',Measuring:'Messe…'},W=$('cw'),CE=$('cs');
let cp='Off';
const C=every(async()=>{const r=await aapi('/api/admin/cal'),d=r.j,s=d.state;if(r.s!=200||!s)return cp!='Off';
if(s=='Off'){if(cp!='Off'&&cp!='Error'){const k=cp=='Done'||cp=='RemoveWeight'||d.factor!=d.oldFactor;say(CE,k?'Kalibrierung gespeichert':'Kalibrierung abgebrochen',k?'ok':'')}}
else if(s=='Error')say(CE,d.error||'Fehler');
else if(s=='Done'||s=='RemoveWeight')say(CE,'Neuer Faktor: '+de(d.newFactor,4)+' (alt: '+de(d.oldFactor,4)+') – Gewicht entfernen','ok');
else say(CE,CS[s]||s,'inf');
const v=d.liveDeltaCounts;$('cd').textContent=de(v,0)+(v!=null&&Math.abs(v)>=1000?' ✓ Gewicht erkannt':' – noch kein Gewicht');
$('fa').textContent='Aktueller Faktor: '+de(d.factor,4);
W.hidden=s!='WaitWeight';$('c0').hidden=s!='Off';$('ck').hidden=s!='Error';$('cx').hidden=s=='Off'||s=='Error';$('cx').textContent=CS[s]?'Abbrechen':'Fertig – Gewicht ist entfernt';
cp=s;return s!='Off'},500);
$('c0').onclick=async()=>{const B=$('c0');B.disabled=true;say(CE,'Starte…','inf');const r=await post('/api/admin/cal/start');B.disabled=false;if(r.s>=200&&r.s<300)C();else say(CE,r.j.error)};
W.onsubmit=async e=>{e.preventDefault();clr(W);if(!chk(W))return;const B=W.querySelector('button');B.disabled=true;const r=await post('/api/admin/cal/measure',{weight:W.elements.weight.value});B.disabled=false;if(res(W,CE,r))C()};
$('cx').onclick=async()=>{await post('/api/admin/cal/cancel');C()};
$('ck').onclick=async()=>{await post('/api/admin/cal/cancel');say(CE,'');C()};
const FM=$('fm'),FB=$('fu'),FP=$('fp');
function wait(){let down=0,n=0;const t=async()=>{const r=await api('/api/status'),f=r.j.fw;n++;
if(r.s==200&&f&&(f!=fw0||down)){$('fv').textContent=f;FP.hidden=true;return say(FM,(f!=fw0?'Neue Version: ':'Wieder erreichbar – Version: ')+f,'ok')}
if(r.s!=200)down=1;if(n==30)say(FM,'Warte auf die Waage… ggf. WLAN neu verbinden','inf');setTimeout(t,2000)};setTimeout(t,2000)}
function fail(t){up=0;FB.disabled=false;FP.hidden=true;say(FM,t);go()}
FB.onclick=async()=>{const f=$('ff').files[0];if(!f||!/\.bin$/i.test(f.name))return say(FM,'Bitte eine .bin-Datei wählen');
FB.disabled=true;say(FM,'Prüfe…','inf');const a=await aapi('/api/admin/update/allowed');if(a.s!=200){FB.disabled=false;return say(FM,a.j.error)}
up=1;const x=new XMLHttpRequest(),d=new FormData();d.append('update',f,f.name);FP.value=0;FP.hidden=false;
x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded*100/e.total);FP.value=p;say(FM,'Hochladen… '+p+' %','inf')}};
x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
if(x.status==401)return location.href='/login';
if(x.status==200){FP.value=100;say(FM,'Update erfolgreich – Waage startet neu…','ok');wait()}else fail(j.error||'Fehler ('+x.status+')')};
x.onerror=()=>fail('Verbindung unterbrochen');
x.open('POST','/api/admin/update');x.setRequestHeader('X-Update-Size',f.size);x.send(d)};
const PS={Pending:'offen',Forfeit:'aufgegeben'};
function rt(X,r,t,last){if(!r)return;
el('p',X,t+' #'+r.id+' · Ziel '+de(r.target,1)+' g · '+(r.final?'final':last?'nicht final':'läuft ('+de(r.elapsed,1)+' s)'));
const T=el('table',X);for(const p of r.players||[]){const R=el('tr',T);if(p.me)R.className='me';
el('td',R,p.rank?p.rank+'.':'-');el('td',R,m5(p.mac));el('td',R,p.status=='Done'?de(p.result,2)+' g / '+de(p.time,2)+' s':PS[p.status]||p.status)}}
const D=every(async()=>{const r=await aapi('/api/admin/duell'),d=r.j,X=$('du');if(r.s!=200)return;X.textContent='';
if(!d.radio)return void el('p',X,'Funk aus · Protokoll '+hx(d.proto)+' · FW '+(d.fw||'--'));
el('p',X,'Diese Waage: '+m5(d.mac)+' ('+d.phase+') · Protokoll '+hx(d.proto)+' · FW '+(d.fw||'--'));
const ps=d.peers||[];el('p',X,'Peers: '+(ps.length?ps.map(p=>m5(p.mac)+' '+p.phase+' ('+de(p.ago,1)+' s)').join(', '):'keine'));
rt(X,d.round,'Runde',0);rt(X,d.last,'Letzte Runde',1)},2000);
function go(){if(live()){P();D();C()}}
document.addEventListener('visibilitychange',go);
lc();go();
</script></body></html>)html";
