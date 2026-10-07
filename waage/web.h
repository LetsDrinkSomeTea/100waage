#pragma once
#include <stdint.h>

// ── Webserver (Config-AP) ─────────────────────────────────────────────────────
// Statische Seiten (web_pages.h) + JSON-API. Schreibende Anfragen kommen als
// application/x-www-form-urlencoded, Antworten sind JSON (Cache-Control:
// no-store). Fehlerformat: {"ok":false,"error":"<Text>","field":"<Feld>"?}.
// Kein Handler stoppt Server/AP oder startet neu; das erledigt der Loop.
//
// Oeffentlich
//   GET  /                      Startseite (Status, Einstellungen)
//   GET  /api/status            {fw, weight|null, mode:"Game"|"Standard",
//                                phase:"Idle"|"Taring"|"Ready"|"Drinking"|"Result"|
//                                      "Calibration"|"SensorError",
//                                busy, goal (Prozent-Modus: %), goalPercent,
//                                glass:"<Name>"|null, random, radio, ap,
//                                scaleOk,
//                                battery:{percent, voltage, pinMv, ratio,
//                                low}|null}
//   GET  /api/config            {goal, randomModeEnabled, randomMin,
//                                goalPercent, goalPct, randomMinPct,
//                                displayRotation, scaleMode, statsRotation,
//                                tolerance}
//   POST /api/config            goal, randomModeEnabled=0|1, randomMin,
//                               goalPercent=0|1, goalPct, randomMinPct,
//                               displayRotation=0|2, scaleMode=Game|Standard
//                               (alle optional) → 200 {ok:true,
//                               applied:"now"|"next", config:{...wie GET}} →
//                               400 {ok:false, error, field} | 409
//                               (Moduswechsel waehrend Spiel)
//   GET  /api/glasses           {weight (absolut)|null, manual:<id>|0,
//                                current:{id, name, source:"auto"|"same"|
//                                "empty"|"manual"}|null,
//                                glasses:[{id, name, empty, nominal,
//                                origin:"default"|"modified"|"custom"}],
//                                deleted:[{id, name}]}
//   POST /api/glasses/select    id=<id> | 0 (automatisch) → 200 wie GET | 400
//   GET  /login                 Login-Seite (?e=1 falsches Passwort, ?e=2
//   gesperrt) POST /login                 password → 302 /admin + Cookie | 302
//   /login?e=1|2 GET  /logout                → 302 /
//
// Admin (Cookie waage_session=<Zufallstoken>; sonst /admin → 302 /login,
// /api/admin/* → 401 {ok:false, error:"login"})
//   GET  /admin                 Admin-Seite
//   GET  /api/admin/config      {apSSID, apName, tolerance, autoResetRange,
//   wifiTimeout,
//                                sleepTimeout, autoZeroEnabled,
//                                autoZeroThreshold, autoZeroDelay,
//                                battDividerRatio, scaleFactor, fw, proto}
//   POST /api/admin/config      apSSID, tolerance, autoResetRange, wifiTimeout,
//   sleepTimeout,
//                               autoZeroEnabled=0|1, autoZeroThreshold,
//                               autoZeroDelay, newPassword (leer =
//                               unveraendert), alle optional → 200 {ok:true,
//                               apRestart:bool, apName, relogin:bool} | 400
//   POST /api/admin/battcal     measuredV=<V> | reset=1
//                               → 200 {ok:true, ratio, voltage, percent} | 400
//                               | 503
//   POST /api/admin/cal/start   → 202 {ok:true} | 409 (Spiel laeuft) | 503
//   (Sensorfehler) POST /api/admin/cal/measure weight=<g> → 202 {ok:true} | 400
//   POST /api/admin/cal/cancel  → 200 {ok:true}
//   GET  /api/admin/cal
//   {state:"Off"|"Prepare"|"Taring"|"WaitWeight"|"Measuring"|
//                                      "Done"|"RemoveWeight"|"Error",
//                                error:null|"<Text>", liveDeltaCounts,
//                                oldFactor, newFactor|null, factor}
//   GET  /api/admin/update/allowed → 200 {ok:true} | 409 {ok:false, error}
//   POST /api/admin/update      multipart, Feld "update" (Firmware .bin),
//   Header
//                               X-Update-Size (Dateigroesse fuer die
//                               Fortschrittsanzeige) → 200 {ok:true} (danach
//                               Neustart) | 409 | 500 {ok:false, error}
//   POST /api/admin/glasses     id (fehlt/0 = neu), name, empty, nominal
//                               → 200 wie /api/glasses | 400 {error, field}
//   POST /api/admin/glasses/delete  id → 200 | 400
//   POST /api/admin/glasses/restore id | all=1 → 200 | 400
//   GET  /api/admin/glasses/export  text/plain: glasses_default.h
//   GET  /api/admin/duell       Duell-Debug: {proto, fw, radio, mac, phase,
//   peers:[...],
//                                round:{id, target, elapsed, final,
//                                players:[{mac, me, status, result?, time?,
//                                rank}]}|null, last:{...}|null}
//
// Sonst: /api/* → 404 JSON, alles andere → 302 / (Captive Portal).

void web_start();
void web_stop();
void web_handle();
uint32_t web_lastActivity();
