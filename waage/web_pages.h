#pragma once
#include <pgmspace.h>

// ── Statische Seiten des Config-AP ────────────────────────────────────────────
// Werden mit send_P ausgeliefert, alle Daten kommen ueber die JSON-API (web.h).
// Kein Internet im AP: alles inline. Werte von Geraet/Nutzer nur per
// textContent/value setzen. Stil "Bierdeckel": gemeinsames CSS und JS als
// Makros, die per String-Verkettung in jede Seite eingesetzt werden.

// Farben als Tokens, Dunkel per prefers-color-scheme (Flaschengruen)
#define WP_CSS                                                                 \
  R"css(
:root{--pappe:#EFE7D6;--schaum:#FFFBF3;--malz:#3A2416;--malz2:#6E5644;--hopfen:#2E4A2C;--hopfen-ink:#FFFBF3;--bern:#C98A1B;--bern-ink:#875A0A;--rot:#A8281E;--linie:#D8CCB4;--feld:#FFF;--fleck:rgba(58,36,22,.06);--ring:rgba(46,74,44,.4);--ring2:rgba(255,251,243,.45);--strahl:rgba(255,251,243,.22);--fokus:rgba(201,138,27,.3);--ks:#1F331D;--ka:#8E6112;
--serif:"Iowan Old Style","Palatino Linotype",Palatino,"Book Antiqua",Georgia,serif;--sans:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
@media(prefers-color-scheme:dark){:root{--pappe:#141B16;--schaum:#1C251F;--malz:#ECE4D3;--malz2:#A9B0A2;--hopfen:#A8C49B;--hopfen-ink:#141B16;--bern:#D9A441;--bern-ink:#E2B65F;--rot:#E48A7B;--linie:#2F3B33;--feld:#18201B;--fleck:rgba(236,228,211,.025);--ring:rgba(168,196,155,.4);--ring2:rgba(20,27,22,.45);--strahl:rgba(28,37,31,.22);--fokus:rgba(217,164,65,.3);--ks:#6E8A63;--ka:#9C7428;color-scheme:dark}}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;background:var(--pappe);color:var(--malz);font:16px/1.45 var(--sans);background-image:radial-gradient(var(--fleck) 1px,transparent 1.5px),radial-gradient(var(--fleck) 1px,transparent 1.5px);background-size:23px 23px,37px 37px;background-position:0 0,11px 17px}
.page{max-width:30rem;margin:0 auto;padding:20px 16px 40px;display:flex;flex-direction:column;gap:18px}
.leiste{display:flex;align-items:center;justify-content:space-between;gap:12px;padding-bottom:12px;border-bottom:1px solid var(--linie)}
.marke{display:flex;align-items:center;gap:10px;font:600 1.05rem var(--sans);color:var(--malz);margin:0}
.marke::before{content:"";flex:none;width:16px;height:16px;border-radius:50%;background:var(--schaum);box-shadow:0 0 0 1.5px var(--linie),5px -3px 0 1px var(--bern),-4px 3px 0 1px var(--hopfen)}
.wo{font:italic 400 1rem var(--serif);color:var(--bern-ink);margin:0}
.buehne{position:relative;display:grid;place-items:center;padding:22px 0 6px;transition:opacity .3s}.buehne.off{opacity:.45}
.deckel,.hinten{width:min(76vw,280px);aspect-ratio:1;max-width:100%;border-radius:50%}
.deckel{position:relative;display:grid;place-items:center;text-align:center;background:var(--schaum);--r:-3deg;transform:rotate(var(--r));box-shadow:0 0 0 1px var(--linie),inset 0 0 0 12px var(--schaum),inset 0 0 0 13px var(--ring)}
.hinten{position:absolute;left:50%;top:50%}
.h1{background:var(--hopfen);transform:translate(-50%,-50%) translate(-22px,14px) rotate(9deg);box-shadow:inset 0 0 0 10px var(--hopfen),inset 0 0 0 11.5px var(--ring2)}
.h2{background:var(--bern) repeating-conic-gradient(var(--strahl) 0 5deg,transparent 5deg 15deg);transform:translate(-50%,-50%) translate(20px,-10px) rotate(-14deg);-webkit-mask:radial-gradient(circle,#000 69.5%,transparent 70% 72%,#000 72.5%);mask:radial-gradient(circle,#000 69.5%,transparent 70% 72%,#000 72.5%)}
.in{position:relative;display:flex;flex-direction:column;align-items:center;gap:4px;max-width:74%}
.lab{font:italic 1rem/1.2 var(--serif);color:var(--bern-ink)}.lab.rot{color:var(--rot)}
.gross{font:500 3.3rem/1 var(--serif);font-variant-numeric:lining-nums tabular-nums;letter-spacing:-.02em;white-space:nowrap}
.gross small{font-size:1.25rem;margin-left:3px;color:var(--malz2)}.erg .gross{color:var(--hopfen)}
.sub{font-size:.85rem;line-height:1.3;color:var(--malz2);text-wrap:balance}
.wackel{animation:wackel .7s ease-in-out}
@keyframes wackel{0%,100%{transform:rotate(var(--r))}25%{transform:rotate(2deg) translateY(-3px)}55%{transform:rotate(-7deg)}80%{transform:rotate(-2deg)}}
.chips{display:flex;flex-wrap:wrap;justify-content:center;gap:6px}
.chip{font-size:.8rem;padding:3px 10px;border-radius:999px;border:1px solid var(--linie);background:var(--schaum);color:var(--malz2);white-space:nowrap;font-variant-numeric:tabular-nums}
.chip b{color:var(--malz);font-weight:600}.chip.warn,.chip.warn b{border-color:var(--rot);color:var(--rot)}
.karte{background:var(--schaum);border:1px solid var(--linie);border-radius:14px;padding:16px 16px 18px}
.karte h2,summary{font:600 1.2rem/1.2 var(--serif);color:var(--hopfen);margin:0}
.stack{display:flex;flex-direction:column;gap:14px}
i,.hint{font-style:normal;font-weight:400;font-size:.8rem;line-height:1.35;color:var(--malz2)}p{margin:0}
.r{display:grid;grid-template-columns:1fr auto;align-items:center;gap:2px 14px}
.r>span:first-child,.sw>span{font-weight:600;font-size:.92rem;display:flex;flex-direction:column;gap:1px;min-width:0}
.r .ein{width:7.5rem}
.f{display:flex;flex-direction:column;gap:5px;font-weight:600;font-size:.92rem}
.ein{display:flex;border:1.5px solid var(--linie);border-radius:8px;background:var(--feld);overflow:hidden}
.ein input{flex:1;min-width:0;width:100%;border:0;background:none;color:var(--malz);font:400 1rem var(--sans);padding:8px 10px;font-variant-numeric:tabular-nums;outline:0}
.r .ein input{text-align:right;padding-right:6px}
.ein select{flex:1;min-width:0;width:100%;border:0;background:none;color:var(--malz);font:400 1rem var(--sans);padding:8px 10px;outline:0}
.ein span{display:grid;place-items:center;padding:0 10px 0 2px;color:var(--malz2);font-weight:400;font-size:.9rem}
.ein:focus-within{border-color:var(--bern);box-shadow:0 0 0 3px var(--fokus)}
.bad{border-color:var(--rot)!important}.fe{grid-column:1/-1;color:var(--rot);font-weight:400;font-size:.82rem}
.seg{display:flex;border:1px solid var(--linie);border-radius:999px;padding:3px;gap:3px;background:var(--feld)}
.seg label{flex:1;position:relative;text-align:center;font-weight:500;font-size:.92rem;cursor:pointer}
.seg input{position:absolute;opacity:0;inset:0;margin:0;cursor:pointer}
.seg span{display:block;padding:9px 6px;border-radius:999px}
.seg input:checked+span{background:var(--hopfen);color:var(--hopfen-ink)}
.seg input:focus-visible+span{outline:2px solid var(--bern);outline-offset:2px}
.seg.zu{opacity:.5}.seg.zu label,.seg.zu input{cursor:not-allowed}
.sperre{font-size:.82rem;color:var(--rot);font-weight:500}
.sw{display:grid;grid-template-columns:1fr auto;gap:14px;align-items:center;cursor:pointer}
.sw input{appearance:none;-webkit-appearance:none;width:42px;height:24px;border-radius:999px;background:var(--linie);position:relative;margin:0;cursor:pointer;transition:background .15s}
.sw input::after{content:"";position:absolute;top:3px;left:3px;width:18px;height:18px;border-radius:50%;background:var(--schaum);transition:transform .15s}
.sw input:checked{background:var(--hopfen)}.sw input:checked::after{transform:translateX(18px)}
.sw input:focus-visible{outline:2px solid var(--bern);outline-offset:2px}
.gruppe{display:flex;flex-direction:column;gap:12px;padding-left:12px;border-left:2px solid var(--linie)}
.kn{display:block;width:100%;font:600 1rem var(--sans);padding:12px;border:0;border-radius:10px;cursor:pointer;text-align:center;text-decoration:none;background:var(--hopfen);color:var(--hopfen-ink);box-shadow:0 3px 0 var(--ks)}
.kn:active{transform:translateY(2px);box-shadow:0 1px 0 var(--ks)}.kn:disabled{opacity:.5;cursor:default}
.kn:focus-visible,a:focus-visible,summary:focus-visible{outline:2px solid var(--bern);outline-offset:3px}
.kn.am{background:var(--bern);color:#2A1A0C;box-shadow:0 3px 0 var(--ka)}
.kn.rand{background:none;color:var(--hopfen);border:1.5px solid var(--hopfen);box-shadow:none}
.kn.weg{background:none;color:var(--rot);border:1.5px solid var(--rot);box-shadow:none}
.reihe{display:flex;gap:8px}.reihe>*{flex:1}
.msg{text-align:center;font-size:.9rem;color:var(--rot)}.msg:empty{display:none}
.msg.ok{color:var(--hopfen)}.msg.inf{color:var(--bern-ink)}
.msg.ok::before{content:"✓ ";display:inline-block;animation:puls .5s ease-out}
@keyframes puls{0%{transform:scale(.4)}60%{transform:scale(1.4)}100%{transform:scale(1)}}
.fuss{text-align:center;font-size:.88rem}
.leise{color:var(--malz2);text-decoration:underline;text-underline-offset:3px;text-decoration-color:var(--linie)}
.zw{font:600 .95rem var(--serif);color:var(--hopfen);margin:6px 0}
.tab{overflow-x:auto}table{width:100%;border-collapse:collapse;font-size:.85rem;font-variant-numeric:tabular-nums}
td{padding:7px 4px;border-bottom:1px solid var(--linie);white-space:nowrap}td:first-child{color:var(--malz2);font-family:var(--serif)}
td+td+td{text-align:right}.plus{color:var(--rot)}.minus{color:var(--hopfen)}.me td{font-weight:700}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:0;font-size:.9rem}dt{color:var(--malz2)}dd{margin:0;font-variant-numeric:tabular-nums}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
)css"

// Gemeinsame Helfer: Abfragen, Polling, Formular-Diff (auch Radio-Gruppen)
#define WP_JS                                                                  \
  R"js(
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
function el(t,p,x,c){const e=document.createElement(t);if(x!=null)e.textContent=x;if(c)e.className=c;p.appendChild(e);return e}
function val(e){return e.type=='checkbox'?(e.checked?'1':'0'):e.value}
function fill(f,c){f.o=f.o||{};for(const k in c){const e=f.elements[k];if(!e)continue;
if(!e.tagName){e.value=c[k];f.o[k]=e.value}else if(e.name==k){if(e.type=='checkbox')e.checked=!!c[k];else e.value=c[k];f.o[k]=val(e)}}}
function diff(f){const p=new URLSearchParams(),o=f.o||{};for(const e of f.elements){if(!e.name||e.disabled||e.type=='radio'&&!e.checked)continue;
const v=val(e);if(v!==(o[e.name]==null?'':o[e.name]))p.append(e.name,v)}return p}
function clr(f){f.querySelectorAll('.fe').forEach(e=>e.remove());f.querySelectorAll('.bad').forEach(e=>e.classList.remove('bad'))}
function ferr(f,n,t){const e=f.elements[n],l=e&&e.closest&&e.closest('label');if(!l)return 0;(e.closest('.ein')||e).classList.add('bad');
el('span',l,t,'fe');e.focus();return 1}
function chk(f){for(const e of f.elements)if(e.validity&&e.validity.badInput)return!ferr(f,e.name,'Keine gültige Zahl');return 1}
function res(f,m,r){if(r.s>=200&&r.s<300)return 1;say(m,r.s==400&&r.j.field&&ferr(f,r.j.field,r.j.error||'Ungültig')?'':r.j.error);return 0}
)js"

#define WP_HEAD                                                                \
  R"html(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">)html"

// Der Deckelstapel (zwei Deckel dahinter, Inhalt im obersten)
#define WP_STAPEL_AUF                                                          \
  R"html(<span class="hinten h2" aria-hidden="true"></span><span class="hinten h1" aria-hidden="true"></span>)html"

static const char INDEX_HTML[] PROGMEM = WP_HEAD
    R"html(<title>100-Waage</title><style>)html" WP_CSS R"html(
.striche{display:flex;flex-wrap:wrap;gap:6px 14px;padding:10px 4px 14px;border-bottom:1px dashed var(--linie)}
.bund{position:relative;display:flex;gap:5px;height:30px;padding:0 2px}
.bund i{width:2.5px;height:100%;background:var(--malz);border-radius:2px}
.bund i:nth-child(2){transform:rotate(3deg)}.bund i:nth-child(3){transform:rotate(-2deg);height:94%}.bund i:nth-child(4){transform:rotate(4deg)}
.bund.voll::after{content:"";position:absolute;left:-4px;right:-4px;top:50%;height:2.5px;background:var(--bern);border-radius:2px;transform:rotate(-24deg)}
.mehr{align-self:center;font:italic .95rem var(--serif);color:var(--malz2)}
.skopf{display:flex;align-items:baseline;justify-content:space-between;gap:12px}
.skopf b{font:600 2rem/1 var(--serif);font-variant-numeric:lining-nums}.skopf small{font:400 .95rem var(--serif);color:var(--malz2);margin-left:4px}
.wert{display:grid;grid-template-columns:repeat(3,1fr);margin:14px 0}.wert div{text-align:center;padding:2px 4px}.wert div+div{border-left:1px solid var(--linie)}
.wert b{display:block;font:600 1.6rem/1.1 var(--serif);font-variant-numeric:lining-nums}.wert span{font-size:.8rem;color:var(--malz2)}
#sb{margin-bottom:14px}#sb b{font-family:var(--serif);font-size:1.05rem}
</style></head><body><main class="page">
<header class="leiste"><h1 class="marke">100-Waage</h1></header>
<div class="buehne" id="bu">)html" WP_STAPEL_AUF
    R"html(<div class="deckel" id="dk"><div class="in" id="in"><span class="lab" id="hl"></span><span class="gross" id="hg">--</span><span class="sub" id="hs"></span></div></div></div>
<div class="chips"><span class="chip" id="ca" hidden></span><span class="chip" id="fw" hidden></span></div>
<form id="cf" class="karte stack" novalidate><h2>Spiel einstellen</h2>
<div class="stack" style="gap:6px"><div class="seg" id="seg" role="radiogroup" aria-label="Modus"><label><input type="radio" name="scaleMode" value="Game"><span>Trinkspiel</span></label><label><input type="radio" name="scaleMode" value="Duel"><span>Duell</span></label><label><input type="radio" name="scaleMode" value="Standard"><span>Waage</span></label></div>
<span class="sperre" id="bz" hidden>Spiel läuft. Erst die Taste an der Waage drücken, dann wechseln.</span></div>
<label class="sw"><span>Ziel in Prozent<i>Anteil vom Glasinhalt, das Glas wird erkannt (nur im Trinkspiel, Duell bleibt in Gramm)</i></span><input type="checkbox" name="goalPercent" id="gp"></label>
<label class="r" id="zg"><span>Zielgewicht<i>Wie viel soll getrunken werden?</i></span><span class="ein"><input type="number" step="0.1" name="goal"><span>g</span></span></label>
<label class="r" id="zp" hidden><span>Ziel<i>Wie viel vom Inhalt soll getrunken werden? (1–100)</i></span><span class="ein"><input type="number" min="1" max="100" name="goalPct"><span>%</span></span></label>
<label class="sw"><span>Zufälliges Ziel<i>Jede Runde ein neues Ziel zwischen Minimum und Ziel</i></span><input type="checkbox" name="randomModeEnabled"></label>
<div class="gruppe"><label class="r" id="rg"><span>Minimum</span><span class="ein"><input type="number" step="0.1" name="randomMin"><span>g</span></span></label>
<label class="r" id="rp" hidden><span>Minimum</span><span class="ein"><input type="number" min="1" max="100" name="randomMinPct"><span>%</span></span></label></div>
<div class="r"><span>Display</span><div class="seg" role="radiogroup" aria-label="Display-Rotation"><label><input type="radio" name="displayRotation" value="0"><span>Normal</span></label><label><input type="radio" name="displayRotation" value="2"><span>180°</span></label></div></div>
<label class="sw"><span>Statistik im Ruhezustand<i>Ohne Glas wechselt das Display zwischen Statistik und Ziel (Zeiten im Admin-Bereich)</i></span><input type="checkbox" name="statsRotation"></label>
<button class="kn">Speichern</button><p class="msg" id="msg" aria-live="polite"></p></form>
<section class="karte stack"><h2>Glas</h2><p id="gn">--</p>
<label class="f">Glas festlegen<span class="ein"><select id="gs"><option value="0">Automatisch erkennen</option></select></span><i>Ein festgelegtes Glas gilt bis zum Neustart der Waage (Deep-Sleep zählt nicht). Bearbeiten im Admin-Bereich.</i></label>
<p class="msg" id="gm" aria-live="polite"></p><div class="tab"><table id="gt"></table></div></section>
<section class="karte"><div class="skopf"><h2>Strichliste</h2><b id="sn">--</b></div><div id="sx"></div></section>
<p class="fuss"><a class="leise" href="/admin">Admin-Bereich</a></p></main>
<script>)html" WP_JS R"html(
const F=$('cf'),M=$('msg'),S=F.elements.scaleMode,R=[...F.querySelectorAll('[name=scaleMode]')],SG=$('seg'),DK=$('dk');
let SD,rd0=null,ph0;
const sg=v=>(v>0?'+':'')+de(v,2);
function hero(d){const pc=d.goalPercent,z=de(d.goal,pc?0:1),ph=d.phase;let L=d.random?'Zufallsziel':'Ziel',G=z,U=pc?'%':'g',T='',e=0;
if(ph=='SensorError'){L='Sensorfehler';G='--';U='';T='Wägezelle antwortet nicht'}
else if(ph=='Calibration'){L='Kalibrierung';G=de(d.weight,2);T='Läuft im Admin-Bereich'}
else if(d.mode=='Standard'){L='Waage';G=de(d.weight,2)}
else if(ph=='Result'){const x=SD&&(rd0==null||SD.rounds>rd0)&&SD.recent&&SD.recent[0];L='Ergebnis';
if(x){G=sg(x.dev);U='g';T='daneben in '+de(x.time,2)+' s bei '+de(x.goal,2)+' g Ziel'+(x.pct?' ('+x.pct+' %)':'')+(x.duel&&x.rank?' · '+x.rank+'. Platz':'');e=1}else T='Wird ausgewertet…'}
else T={Idle:pc?'Glas auf die Waage stellen, es wird erkannt':'Glas auf die Waage stellen',Taring:'Tara…',Ready:(pc&&d.glass?d.glass+'. ':'Glas steht. ')+'Anheben und trinken!',Drinking:'Trinken…'}[ph]||'';
$('hl').textContent=L;$('hl').className='lab'+(ph=='SensorError'?' rot':'');const g=$('hg');g.textContent=G;if(U)el('small',g,U);
$('hs').textContent=T;$('in').className='in'+(e?' erg':'')}
let LS;
const P=every(async()=>{const r=await api('/api/status'),d=r.j,b=d.battery;$('bu').classList.toggle('off',r.s!=200);if(r.s!=200)return;LS=d;
if(d.phase=='Drinking')rd0=SD?SD.rounds:null;
if(d.phase=='Result'&&ph0&&ph0!='Result'){DK.classList.remove('wackel');void DK.offsetWidth;DK.classList.add('wackel');ST()}
if(d.phase!='Result'&&d.phase!='Drinking')rd0=null;ph0=d.phase;hero(d);
const A=$('ca');A.hidden=!b;if(b){A.textContent='Akku ';el('b',A,de(b.percent,0)+' %');A.append(' · '+de(b.voltage,2)+' V'+(b.low?' · schwach':''));A.className='chip'+(b.low?' warn':'')}
$('fw').hidden=!d.fw;$('fw').textContent=d.fw||'';
R.forEach(i=>i.disabled=!!d.busy);SG.classList.toggle('zu',!!d.busy);$('bz').hidden=!d.busy;
if(d.mode&&F.o&&F.o.scaleMode!=d.mode&&S.value==F.o.scaleMode){S.value=d.mode;F.o.scaleMode=d.mode}},1000);
async function load(){const r=await api('/api/config');if(r.s==200){if(!F.o)say(M,'');fill(F,r.j);unit()}
else{say(M,'Einstellungen nicht geladen');setTimeout(load,3000)}}
F.onsubmit=async e=>{e.preventDefault();clr(F);if(!F.o)return say(M,'Einstellungen nicht geladen');if(!chk(F))return say(M,'');
const B=F.querySelector('button');B.disabled=true;say(M,'Speichere…','inf');
const r=await api('/api/config',diff(F));B.disabled=false;
if(res(F,M,r)){fill(F,r.j.config||{});unit();say(M,r.j.applied=='next'?'Gespeichert. Gilt ab der nächsten Runde':'Gespeichert','ok');P()}};
const GP=$('gp');
function unit(){const p=GP.checked;$('zg').hidden=$('rg').hidden=p;$('zp').hidden=$('rp').hidden=!p}
GP.onchange=unit;
const GS=$('gs'),GM=$('gm'),SRC={auto:'erkannt',same:'wie zuletzt',empty:'leer erkannt',manual:'festgelegt'};
let gsel=null;
const GL=every(async()=>{const r=await api('/api/glasses'),d=r.j;if(r.s!=200)return;
const c=d.current,N=$('gn');N.textContent='';if(c){el('b',N,c.name);N.append(' · '+(SRC[c.source]||c.source))}else N.append(d.weight>5?'Glas nicht erkannt':'Kein Glas auf der Waage');
const k=(d.glasses||[]).map(g=>g.id+':'+g.name).join('|');if(k!==gsel){gsel=k;GS.length=1;for(const g of d.glasses||[]){const o=el('option',GS,g.name);o.value=g.id}}
if(document.activeElement!==GS)GS.value=String(d.manual||0);
const T=$('gt');T.textContent='';for(const g of d.glasses||[]){const R=el('tr',T);if(c&&c.id==g.id)R.className='me';el('td',R,g.name);el('td',R,de(g.empty,1)+' g leer');
const w=d.weight!=null&&d.weight>5?d.weight-g.empty:null;el('td',R,w==null?'':w<-5?'zu leicht':'wäre '+de(Math.max(w,0),0)+' g drin')}},2000);
GS.onchange=async()=>{say(GM,'Speichere…','inf');const r=await api('/api/glasses/select',new URLSearchParams({id:GS.value}));
if(r.s==200){say(GM,GS.value=='0'?'Glas wird automatisch erkannt':'Glas festgelegt','ok');GL()}else say(GM,r.j.error)};
function tally(X,n){const T=el('div',X,null,'striche'),max=8;T.setAttribute('aria-hidden','true');
for(let b=0;b<Math.min(Math.ceil(n/5),max);b++){const k=Math.min(5,n-b*5),d=el('span',T,null,'bund'+(k==5?' voll':''));for(let i=0;i<Math.min(k,4);i++)el('i',d)}
if(n>max*5)el('span',T,'und '+(n-max*5)+' weitere','mehr')}
const ST=every(async()=>{const r=await api('/api/stats'),d=r.j,X=$('sx');if(r.s!=200)return;SD=d;if(LS)hero(LS);X.textContent='';
const N=$('sn');N.textContent=d.rounds;el('small',N,d.rounds==1?'Runde':'Runden');
if(!d.rounds){el('p',X,'Noch keine Runde gespielt. Glas auf die Waage und los!','hint').style.marginTop='10px';return}
tally(X,d.rounds);const W=el('div',X,null,'wert');
[[d.perfect,'Perfekt'],[d.notBad,'Not Bad'],[d.ok,'Ganz ok']].forEach(([v,t])=>{const c=el('div',W);el('b',c,v);el('span',c,t)});
const b=d.best,f=d.fastest,L=el('dl',X);L.id='sb';
const row=(t,v,x)=>{el('dt',L,t);const e=el('dd',L);if(v)el('b',e,v);e.append(x)};
row('Bester Treffer',b&&de(b.dev,2)+' g',b?' daneben · Ziel '+(b.pct?b.pct+' %'+(b.glass?' '+b.glass:''):de(b.goal,1)+' g')+' in '+de(b.time,2)+' s':'noch keiner');
row('Schnellste Zeit',f&&de(f.time,2)+' s',f?' · Ziel '+de(f.goal,1)+' g, '+de(Math.abs(f.dev),2)+' g daneben':'noch keine (max. 10 % daneben)');
if(d.duels)row('Duelle',d.wins+(d.wins==1?' Sieg':' Siege'),' aus '+d.duels);
const rs=d.recent||[];if(!rs.length)return;el('h3',X,'Letzte Runden seit dem Einschalten','zw');
const T=el('table',el('div',X,null,'tab'));rs.forEach((e,i)=>{const R=el('tr',T);el('td',R,'#'+(d.rounds-i));el('td',R,e.duel?(e.rank?e.rank+'. Platz':'Duell'):'Solo');
el('td',R,sg(e.dev)+' g',e.dev>0?'plus':e.dev<0?'minus':'');el('td',R,e.pct?'Ziel '+e.pct+' %':'Ziel '+de(e.goal,1)+' g');el('td',R,de(e.time,2)+' s')})},5000);
document.addEventListener('visibilitychange',()=>{if(live()){P();ST();GL()}});
load();P();ST();GL();
</script></body></html>)html";

static const char LOGIN_HTML[] PROGMEM = WP_HEAD
    R"html(<title>Admin-Login – 100-Waage</title><style>)html" WP_CSS R"html(
.deckel .in{width:70%;max-width:70%}.deckel form{display:flex;flex-direction:column;gap:10px;width:100%}.deckel input{text-align:center}
</style></head><body><main class="page">
<header class="leiste"><span class="marke">100-Waage</span><h1 class="wo">Login</h1></header>
<div class="buehne">)html" WP_STAPEL_AUF
    R"html(<div class="deckel"><form class="in" method="post" action="/login"><h2 class="zw" style="font-size:1.2rem;margin:0">Admin-Login</h2>
<p class="msg" id="e" role="alert" hidden></p>
<span class="ein"><input type="password" name="password" placeholder="Passwort" aria-label="Passwort" required autofocus autocomplete="current-password"></span>
<button class="kn am">Einloggen</button></form></div></div>
<p class="fuss"><a class="leise" href="/">Zurück zur Waage</a></p></main>
<script>
const m={1:'Falsches Passwort',2:'Zu viele Versuche. Bitte 30 s warten'}[new URLSearchParams(location.search).get('e')],e=document.getElementById('e');
if(m){e.textContent=m;e.hidden=false}
</script></body></html>)html";

static const char ADMIN_HTML[] PROGMEM =
    WP_HEAD R"html(<title>Admin – 100-Waage</title><style>)html" WP_CSS R"html(
details.karte{padding:0}summary{list-style:none;cursor:pointer;padding:15px 16px;display:flex;align-items:baseline;gap:10px;border-radius:14px}
summary::-webkit-details-marker{display:none}
summary::before{content:"";flex:none;width:8px;height:8px;border-right:2px solid var(--hopfen);border-bottom:2px solid var(--hopfen);transform:rotate(-45deg);transition:transform .2s;align-self:center}
details[open] summary::before{transform:rotate(45deg)}
summary b{margin-left:auto;font:400 .85rem var(--sans);color:var(--malz2);font-variant-numeric:tabular-nums;text-align:right}
.body{padding:16px 16px 18px;border-top:1px dashed var(--linie)}
fieldset{border:0;padding:0;margin:0;display:flex;flex-direction:column;gap:14px;min-width:0}
fieldset+fieldset{border-top:1px dashed var(--linie);padding-top:16px;margin-top:2px}
legend{font:italic 600 1rem var(--serif);color:var(--bern-ink);padding:0;margin-bottom:12px}
dd{text-align:right}.akt{font-weight:400;font-size:.8rem;color:var(--malz2)}.akt b{color:var(--malz)}
.schritte{display:flex;list-style:none;padding:0;margin:0 0 4px;counter-reset:s}
.schritte li{flex:1;text-align:center;font-size:.78rem;color:var(--malz2);position:relative;counter-increment:s}
.schritte li::before{content:counter(s);display:grid;place-items:center;width:30px;height:30px;margin:0 auto 4px;border-radius:50%;border:1.5px solid var(--linie);background:var(--feld);font:600 .95rem var(--serif);position:relative;z-index:1}
.schritte li+li::after{content:"";position:absolute;top:15px;right:50%;width:100%;height:1.5px;background:var(--linie)}
.schritte .ok::before{content:"✓";background:var(--hopfen);border-color:var(--hopfen);color:var(--hopfen-ink)}
.schritte .jetzt::before{border:2.5px solid var(--bern);color:var(--malz)}.schritte .jetzt{color:var(--malz);font-weight:600}
.schritte .ok+li::after{background:var(--hopfen)}
.roh{display:flex;justify-content:space-between;gap:10px;font-size:.88rem;padding:8px 10px;border-radius:8px;background:var(--pappe)}.roh b{font-variant-numeric:tabular-nums}.roh .ja{color:var(--hopfen)}
progress{width:100%;height:10px;accent-color:var(--hopfen)}
input[type=file]{font:400 .9rem var(--sans);color:var(--malz);max-width:100%}
input[type=file]::file-selector-button{font:inherit;margin-right:10px;padding:7px 12px;border-radius:8px;border:1.5px solid var(--hopfen);background:none;color:var(--hopfen);cursor:pointer}
#du p{font-size:.9rem}
#gt td{white-space:normal;vertical-align:middle}#gt td:first-child{color:var(--malz);font-family:var(--sans);font-weight:600}#gt i{display:block}
#gt td+td{text-align:right}.akts{display:flex;flex-wrap:wrap;gap:6px;justify-content:flex-end}
</style></head><body><main class="page">
<header class="leiste"><span class="marke">100-Waage</span><h1 class="wo">Admin</h1></header>
<details class="karte" open><summary>Einstellungen</summary>
<form id="af" class="body stack" novalidate>
<fieldset><legend>WLAN</legend>
<label class="f">WLAN-Name<span class="ein"><input name="apSSID" maxlength="32" autocomplete="off"></span><span class="akt">Aktiv: <b id="apn">--</b>. Der Standardname „100-Waage-Config“ wird automatisch eindeutig gemacht.</span></label>
<label class="r"><span>Automatisch aus<i>Ohne Zugriff, 0 = nie</i></span><span class="ein"><input type="number" min="0" max="255" name="wifiTimeout"><span>min</span></span></label></fieldset>
<fieldset><legend>Messung</legend>
<label class="r"><span>Toleranz<i>Für die Start/Stop-Erkennung</i></span><span class="ein"><input type="number" step="0.1" name="tolerance"><span>g</span></span></label>
<label class="r"><span>Auto-Reset-Bereich<i>Ergebnisse außerhalb werden automatisch zurückgesetzt (0–100)</i></span><span class="ein"><input type="number" min="0" max="100" name="autoResetRange"><span>%</span></span></label>
<label class="r"><span>Tauschzeit<i>Glaserkennung: steht ein Glas innerhalb dieser Zeit leichter wieder auf der Waage, ist es ein anderes Glas (direkt hintereinander gespielt). Danach gilt: dazwischen getrunken. 0 = aus (0–60)</i></span><span class="ein"><input type="number" min="0" max="60" name="glassSwapMin"><span>min</span></span></label>
<label class="sw"><span>Auto-Zero<i>Nullabgleich, wenn die Waage stabil leer ist</i></span><input type="checkbox" name="autoZeroEnabled"></label>
<div class="gruppe"><label class="r"><span>Gilt als leer bis</span><span class="ein"><input type="number" step="0.1" name="autoZeroThreshold"><span>g</span></span></label>
<label class="r"><span>Stabil leer für</span><span class="ein"><input type="number" min="1" max="60" name="autoZeroDelay"><span>s</span></span></label></div></fieldset>
<fieldset><legend>Energie</legend>
<label class="r"><span>Deep-Sleep nach<i>Ohne Aktivität, 0 = nie</i></span><span class="ein"><input type="number" min="0" max="255" name="sleepTimeout"><span>min</span></span></label>
<label class="sw"><span>Akku vorhanden<i>Aus bei Netzbetrieb: keine Akkumessung, kein Akkusymbol</i></span><input type="checkbox" name="batteryPresent"></label></fieldset>
<fieldset><legend>Statistik auf dem Display</legend>
<label class="r"><span>Erscheint nach<i>So lange ohne Glas (1–255)</i></span><span class="ein"><input type="number" min="1" max="255" name="statsAfterS"><span>s</span></span></label>
<label class="r"><span>Dauer je Bild<i>1–60</i></span><span class="ein"><input type="number" min="1" max="60" name="statsStepS"><span>s</span></span></label>
<label class="r"><span>Ziel dazwischen<i>1–60</i></span><span class="ein"><input type="number" min="1" max="60" name="statsGoalS"><span>s</span></span></label></fieldset>
<fieldset><legend>Passwort</legend>
<label class="f">Neues Passwort<i>Leer lassen = unverändert, mindestens 4 Zeichen</i><span class="ein"><input type="password" name="newPassword" maxlength="31" autocomplete="new-password"></span><span class="ein"><input type="password" id="pw2" maxlength="31" placeholder="Wiederholen" aria-label="Passwort wiederholen" autocomplete="new-password"></span></label></fieldset>
<button class="kn">Speichern</button><p class="msg" id="am" aria-live="polite"></p></form></details>
<details class="karte"><summary>Akku <b id="bs"></b></summary><div class="body stack">
<dl id="bd"><dt>Akku</dt><dd id="bv">--</dd><dt>Spannung am Pin</dt><dd id="bp">--</dd><dt>Teiler</dt><dd id="bt">--</dd></dl><p class="hint" id="bn" hidden>Kein Akku: in den Einstellungen deaktiviert.</p>
<form id="bf" class="stack" novalidate><label class="r"><span>Gemessene Spannung<i>Mit dem Multimeter direkt an den Akkupolen messen, während der Funk an ist</i></span><span class="ein"><input type="number" step="0.01" name="measuredV"><span>V</span></span></label>
<div class="reihe"><button class="kn">Abgleichen</button><button type="button" class="kn rand" id="br">Auf 2,0 zurück</button></div></form>
<p class="msg" id="bm" aria-live="polite"></p></div></details>
<details class="karte" id="kc"><summary>Kalibrieren <b id="fa"></b></summary><div class="body stack">
<ol class="schritte" id="cst" hidden><li>Waage leeren</li><li>Gewicht auflegen</li><li>Gewicht entfernen</li></ol>
<p class="msg" id="cs" aria-live="polite"></p>
<form id="cw" class="stack" novalidate hidden><label class="r"><span>Bekanntes Gewicht<i>Auflegen, Wert eingeben, dann messen</i></span><span class="ein"><input type="number" step="0.1" name="weight"><span>g</span></span></label>
<div class="roh"><span>Rohwert-Änderung</span><b id="cd">--</b></div><button class="kn">Messen</button></form>
<button class="kn" id="c0">Kalibrierung starten</button><button class="kn weg" id="cx" hidden>Abbrechen</button><button class="kn" id="ck" hidden>OK</button></div></details>
<details class="karte" id="gk"><summary>Gläser <b id="gc"></b></summary><div class="body stack">
<p class="hint">Standardgläser kommen mit der Firmware. Änderungen bleiben bei Updates erhalten. Neue Gläser: leer auflegen, „Aktuelles Gewicht“, Name und Füllmenge eintragen.</p>
<div class="tab"><table id="gt"></table></div>
<form id="gf" class="stack" novalidate><h3 class="zw" id="gh">Neues Glas</h3><input type="hidden" name="id" value="0">
<label class="f">Name<i>Bis 12 Zeichen, so steht er auf dem Display</i><span class="ein"><input name="name" maxlength="24" autocomplete="off"></span></label>
<label class="r"><span>Füllmenge<i>Nennfüllung, z. B. 300 für 0,3 l</i></span><span class="ein"><input type="number" step="0.1" name="nominal"><span>g</span></span></label>
<label class="r"><span>Leergewicht<i>Live auf der Waage: <b id="gw">--</b></i></span><span class="ein"><input type="number" step="0.1" name="empty"><span>g</span></span></label>
<button type="button" class="kn rand" id="gu">Aktuelles Gewicht übernehmen</button>
<div class="reihe"><button class="kn">Speichern</button><button type="button" class="kn rand" id="gx">Neu</button></div></form>
<p class="msg" id="gm" aria-live="polite"></p>
<div id="gd" hidden><h3 class="zw">Gelöschte Standardgläser</h3><div class="tab"><table id="gdt"></table></div></div>
<div class="reihe"><a class="kn rand" href="/api/admin/glasses/export" download="glasses_default.h">Als Firmware-Liste exportieren</a><button type="button" class="kn weg" id="ga">Alles auf Standard</button></div></div></details>
<details class="karte"><summary>Statistik</summary><div class="body stack">
<p class="hint">Setzt alle Zähler und Bestwerte zurück.</p><p class="hint" id="sh" hidden>Die Waage wurde neu kalibriert: alte Werte sind mit dem neuen Faktor nicht mehr vergleichbar.</p>
<button class="kn weg" id="sr">Statistik zurücksetzen</button><p class="msg" id="sm" aria-live="polite"></p></div></details>
<details class="karte"><summary>Firmware <b id="fs"></b></summary><div class="body stack">
<dl><dt>Version</dt><dd id="fv">--</dd><dt>Protokoll</dt><dd id="pv">--</dd></dl>
<label class="f">Firmware-Datei (.bin)<input type="file" id="ff" accept=".bin,application/octet-stream"></label><progress id="fp" max="100" value="0" aria-label="Upload" hidden></progress>
<button class="kn" id="fu">Update starten</button><p class="msg" id="fm" aria-live="polite"></p></div></details>
<details class="karte"><summary>Duell-Debug <b id="ds"></b></summary><div class="body stack" id="du">--</div></details>
<div class="reihe"><a href="/" class="kn rand">Zurück</a><a href="/logout" class="kn weg">Ausloggen</a></div></main>
<script>)html" WP_JS R"html(
let fw0;
const hx=p=>p==null?'--':'0x'+(+p).toString(16).toUpperCase(),
m5=m=>String(m||'--').slice(-5),
post=(u,o)=>aapi(u,new URLSearchParams(o||{}));
async function aapi(u,b){const r=await api(u,b);if(r.s==401){up=1;r.j={error:'Nicht eingeloggt – weiter zum Login…'};location.href='/login'}return r}
const A=$('af'),AM=$('am');
function fwv(f){$('fv').textContent=$('fs').textContent=f||'--'}
async function lc(){const r=await aapi('/api/admin/config'),j=r.j;if(r.s!=200){say(AM,j.error);if(!up)setTimeout(lc,3000);return}
if(!A.o)say(AM,'');fill(A,j);$('apn').textContent=j.apName||'--';fwv(j.fw);$('pv').textContent=hx(j.proto);fw0=j.fw}
A.onsubmit=async e=>{e.preventDefault();clr(A);if(!A.o)return say(AM,'Einstellungen nicht geladen');if(!chk(A))return say(AM,'');
const n=A.elements.newPassword.value,B=A.querySelector('button');
if(n&&n.length<4)return ferr(A,'newPassword','Mindestens 4 Zeichen');
if(n!=$('pw2').value)return ferr(A,'newPassword','Passwörter stimmen nicht überein');
B.disabled=true;say(AM,'Speichere…','inf');const r=await aapi('/api/admin/config',diff(A)),j=r.j;B.disabled=false;
if(!res(A,AM,r))return;
let t='Gespeichert';
if(j.apRestart)t+='. WLAN wird neu gestartet: bitte mit '+j.apName+' neu verbinden';
if(j.relogin){t+='. Bitte neu einloggen…';up=1;setTimeout(()=>location.href='/login',2000)}
say(AM,t,'ok');A.elements.newPassword.value=$('pw2').value='';
A.o={};for(const x of A.elements)if(x.name)A.o[x.name]=val(x);
if(j.apName)$('apn').textContent=j.apName;if(!j.apRestart&&!j.relogin)lc()};
const P=every(async()=>{const r=await api('/api/status'),b=r.j.battery;if(r.s!=200)return;fw0=fw0||r.j.fw;
$('bd').hidden=!b;$('bn').hidden=!!b;$('bs').textContent=b?de(b.percent,0)+' % · '+de(b.voltage,2)+' V'+(b.low?' · schwach':''):'kein Akku';
$('bs').style.color=b&&b.low?'var(--rot)':'';
if(b){$('bv').textContent=de(b.percent,0)+' % · '+de(b.voltage,2)+' V';$('bp').textContent=de(b.pinMv,0)+' mV';$('bt').textContent=de(b.ratio,3)}},3000);
const BF=$('bf'),BM=$('bm');
async function bc(o){clr(BF);say(BM,'Gleiche ab…','inf');const r=await post('/api/admin/battcal',o),j=r.j;
if(res(BF,BM,r)){say(BM,'Teiler '+de(j.ratio,3)+' – Akku '+de(j.voltage,2)+' V ('+de(j.percent,0)+' %)','ok');P()}}
BF.onsubmit=e=>{e.preventDefault();clr(BF);if(chk(BF))bc({measuredV:BF.elements.measuredV.value});else say(BM,'')};
$('br').onclick=()=>bc({reset:1});
const CS={Prepare:'Waage leeren…',Taring:'Tara…',WaitWeight:'Bekanntes Gewicht auflegen',Measuring:'Messe…'},
CN={Prepare:0,Taring:0,WaitWeight:1,Measuring:1,Done:2,RemoveWeight:2},W=$('cw'),CE=$('cs');
let cp='Off';
const C=every(async()=>{const r=await aapi('/api/admin/cal'),d=r.j,s=d.state;if(r.s!=200||!s)return cp!='Off';
if(s=='Off'){if(cp!='Off'&&cp!='Error'){const k=cp=='Done'||cp=='RemoveWeight'||d.factor!=d.oldFactor;say(CE,k?'Kalibrierung gespeichert':'Kalibrierung abgebrochen',k?'ok':'');if(k)$('sh').hidden=false}}
else if(s=='Error')say(CE,d.error||'Fehler');
else if(s=='Done'||s=='RemoveWeight')say(CE,'Neuer Faktor '+de(d.newFactor,4)+' (alt '+de(d.oldFactor,4)+'). Gewicht entfernen','ok');
else say(CE,CS[s]||s,'inf');
const n=CN[s],L=$('cst');L.hidden=n==null;[...L.children].forEach((l,i)=>l.className=i<n?'ok':i==n?'jetzt':'');
if(n!=null)$('kc').open=true;
const v=d.liveDeltaCounts,ok=v!=null&&Math.abs(v)>=1000;$('cd').textContent=de(v,0)+(ok?' ✓ erkannt':' – noch kein Gewicht');$('cd').className=ok?'ja':'';
$('fa').textContent='Faktor '+de(d.factor,4);
W.hidden=s!='WaitWeight';$('c0').hidden=s!='Off';$('ck').hidden=s!='Error';$('cx').hidden=s=='Off'||s=='Error';
$('cx').textContent=CS[s]?'Abbrechen':'Fertig, Gewicht ist entfernt';$('cx').className='kn '+(CS[s]?'weg':'rand');
cp=s;return s!='Off'},500);
$('c0').onclick=async()=>{const B=$('c0');B.disabled=true;say(CE,'Starte…','inf');const r=await post('/api/admin/cal/start');B.disabled=false;if(r.s>=200&&r.s<300)C();else say(CE,r.j.error)};
W.onsubmit=async e=>{e.preventDefault();clr(W);if(!chk(W))return;const B=W.querySelector('button');B.disabled=true;const r=await post('/api/admin/cal/measure',{weight:W.elements.weight.value});B.disabled=false;if(res(W,CE,r))C()};
$('cx').onclick=async()=>{await post('/api/admin/cal/cancel');C()};
$('ck').onclick=async()=>{await post('/api/admin/cal/cancel');say(CE,'');C()};
$('sr').onclick=async()=>{if(!confirm('Statistik wirklich zurücksetzen? Alle Zähler und Bestwerte gehen verloren.'))return;
const r=await post('/api/admin/stats/reset');if(r.s==200){say($('sm'),'Statistik zurückgesetzt','ok');$('sh').hidden=true}else say($('sm'),r.j.error)};
const FM=$('fm'),FB=$('fu'),FP=$('fp');
function wait(){let down=0,n=0;const t=async()=>{const r=await api('/api/status'),f=r.j.fw;n++;
if(r.s==200&&f&&(f!=fw0||down)){fwv(f);FP.hidden=true;return say(FM,(f!=fw0?'Neue Version: ':'Wieder erreichbar, Version: ')+f,'ok')}
if(r.s!=200)down=1;if(n==30)say(FM,'Warte auf die Waage… ggf. WLAN neu verbinden','inf');setTimeout(t,2000)};setTimeout(t,2000)}
function fail(t){up=0;FB.disabled=false;FP.hidden=true;say(FM,t);go()}
FB.onclick=async()=>{const f=$('ff').files[0];if(!f||!/\.bin$/i.test(f.name))return say(FM,'Bitte eine .bin-Datei wählen');
FB.disabled=true;say(FM,'Prüfe…','inf');const a=await aapi('/api/admin/update/allowed');if(a.s!=200){FB.disabled=false;return say(FM,a.j.error)}
up=1;const x=new XMLHttpRequest(),d=new FormData();d.append('update',f,f.name);FP.value=0;FP.hidden=false;
x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded*100/e.total);FP.value=p;say(FM,'Hochladen… '+p+' %','inf')}};
x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}
if(x.status==401)return location.href='/login';
if(x.status==200){FP.value=100;say(FM,'Update erfolgreich. Waage startet neu…','ok');wait()}else fail(j.error||'Fehler ('+x.status+')')};
x.onerror=()=>fail('Verbindung unterbrochen');
x.open('POST','/api/admin/update');x.setRequestHeader('X-Update-Size',f.size);x.send(d)};
const GF=$('gf'),GM=$('gm'),OR={default:'Standard',modified:'geändert',custom:'eigenes'};let gw=null;
function gedit(g){clr(GF);fill(GF,g?{id:g.id,name:g.name,nominal:g.nominal,empty:g.empty}:{id:0,name:'',nominal:'',empty:''});$('gh').textContent=g?g.name+' bearbeiten':'Neues Glas';if(g)GF.elements.name.focus()}
function kn(p,t,c,f){const b=el('button',p,t,'kn '+c);b.type='button';b.style.padding='6px 10px';b.style.fontSize='.85rem';b.onclick=f;return b}
async function gpost(u,o,t){const r=await post(u,o);if(r.s==200){say(GM,t,'ok');gshow(r.j)}else say(GM,r.j.error);return r}
function gshow(d){gw=d.weight;$('gw').textContent=d.weight==null?'--':de(d.weight,1)+' g';const gs=d.glasses||[],T=$('gt');T.textContent='';$('gc').textContent=gs.length+(gs.length==1?' Glas':' Gläser');
for(const g of gs){const R=el('tr',T),N=el('td',R,g.name);el('i',N,de(g.empty,1)+' g leer · '+de(g.nominal,0)+' g Inhalt · '+(OR[g.origin]||''));const A=el('div',el('td',R),null,'akts');
kn(A,'Bearbeiten','rand',()=>gedit(g));if(g.origin=='modified')kn(A,'Standard','rand',()=>gpost('/api/admin/glasses/restore',{id:g.id},'Auf Standard zurückgesetzt'));
kn(A,'Löschen','weg',()=>{if(confirm(g.name+' löschen?'))gpost('/api/admin/glasses/delete',{id:g.id},'Gelöscht')})}
const dl=d.deleted||[],D=$('gdt');$('gd').hidden=!dl.length;D.textContent='';for(const g of dl){const R=el('tr',D);el('td',R,g.name);kn(el('td',R),'Wiederherstellen','rand',()=>gpost('/api/admin/glasses/restore',{id:g.id},'Wiederhergestellt'))}}
const GL=every(async()=>{const r=await api('/api/glasses');if(r.s==200){if(document.activeElement&&document.activeElement.closest&&document.activeElement.closest('#gt'))return;gshow(r.j)}},3000);
$('gu').onclick=()=>{if(gw==null)return say(GM,'Kein Gewicht (Waage leer genullt?)');GF.elements.empty.value=de(gw,1).replace(',','.')};
$('gx').onclick=()=>{gedit(null);say(GM,'')};
$('ga').onclick=()=>{if(confirm('Alle Änderungen an der Gläserliste verwerfen? Eigene Gläser werden gelöscht.'))gpost('/api/admin/glasses/restore',{all:1},'Standardliste wiederhergestellt')};
GF.onsubmit=async e=>{e.preventDefault();clr(GF);if(!chk(GF))return say(GM,'');const o={};for(const x of GF.elements)if(x.name)o[x.name]=x.value;
say(GM,'Speichere…','inf');const r=await post('/api/admin/glasses',o);if(res(GF,GM,r)){say(GM,'Gespeichert','ok');gshow(r.j);gedit(null)}};
const PS={Pending:'offen',Forfeit:'aufgegeben'};
function rt(X,r,t,last){if(!r)return;
el('h3',X,t+' #'+r.id+' · Ziel '+de(r.target,1)+' g · '+(r.final?'final':last?'nicht final':'läuft ('+de(r.elapsed,1)+' s)'),'zw');
const T=el('table',el('div',X,null,'tab'));for(const p of r.players||[]){const R=el('tr',T);if(p.me)R.className='me';
el('td',R,p.rank?p.rank+'.':'–');el('td',R,m5(p.mac));el('td',R,p.status=='Done'?de(p.result,2)+' g / '+de(p.time,2)+' s':PS[p.status]||p.status)}}
const D=every(async()=>{const r=await aapi('/api/admin/duell'),d=r.j,X=$('du');if(r.s!=200)return;X.textContent='';
const ps=d.peers||[];$('ds').textContent=d.radio?ps.length+(ps.length==1?' Peer':' Peers'):'Funk aus';
const L=el('dl',X),row=(a,b)=>{el('dt',L,a);el('dd',L,b)};
if(d.radio)row('Diese Waage',m5(d.mac)+' ('+d.phase+')');row('Protokoll',hx(d.proto));row('Firmware',d.fw||'--');if(!d.radio)return;
row('Peers',ps.length?ps.map(p=>m5(p.mac)+' '+p.phase+' ('+de(p.ago,1)+' s)').join(', '):'keine');
rt(X,d.round,'Runde',0);rt(X,d.last,'Letzte Runde',1)},2000);
function go(){if(live()){P();D();C();GL()}}
document.addEventListener('visibilitychange',go);
lc();go();
</script></body></html>)html";
