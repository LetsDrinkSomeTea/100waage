# 100-Waage — Spezifikation

## Hardware

| Komponente     | Details                                      |
|----------------|----------------------------------------------|
| MCU            | ESP32-C3 Super Mini                          |
| Waagezelle     | HX711 Verstärkermodul                        |
| Display        | SSD1306 OLED, 128×32 px, I2C                 |
| Taster         | Reset/Multifunktion, GPIO 5                  |
| Akku (optional)| Li-Ion 3,0 V–4,2 V, Spannungsteiler GPIO 2  |

### Pin-Belegung

| Funktion   | GPIO |
|------------|------|
| OLED SDA   | 8    |
| OLED SCL   | 9    |
| HX711 DAT  | 21   |
| HX711 CLK  | 20   |
| Taster     | 5    |
| Akku ADC   | 2    |

---

## Feature-Flags (Compile-Zeit)

| Flag                | Effekt                                              | Default |
|---------------------|-----------------------------------------------------|---------|
| `BATTERY_CONNECTED` | Aktiviert Akkuspannung-Lesen auf GPIO 2             | aus     |
| `RESET_CONFIG`      | Erlaubt Factory-Reset durch Halten beim Einschalten | ein     |

---

## Konfiguration (EEPROM)

Wird als `WaageConfig`-Struct direkt in EEPROM gespeichert (Struct-Serialisierung).  
Magic-Byte `0xCC` am Anfang des Structs zeigt gültige Konfiguration an.

| Feld               | Typ       | Default           | Beschreibung                              |
|--------------------|-----------|-------------------|-------------------------------------------|
| `magic`            | `uint8_t` | `0xCC`            | Validierungsmarkierung                    |
| `apSSID`           | `char[64]`| `"100-Waage-Config"` | WLAN-Name des Access Points            |
| `scaleFactor`      | `float`   | `708.0`           | Rohwert-zu-Gramm-Faktor der Wiegezelle    |
| `goal`             | `float`   | `100.0`           | Zielgewicht in Gramm                      |
| `tolerance`        | `float`   | `10.0`            | Messtoleranz für Zustandsübergänge [g]    |
| `displayRotation`  | `uint8_t` | `2`               | OLED-Rotation (0 = normal, 2 = 180°)     |
| `adminPassword`    | `char[32]`| `"admin"`         | Passwort für Admin-Bereich im Web-UI      |
| `wifiTimeout`      | `uint8_t` | `10`              | WiFi Auto-Aus nach N Minuten (0 = nie)    |
| `sleepTimeout`     | `uint8_t` | `5`               | Deep-Sleep nach N Minuten Inaktivität (0 = nie) |
| `battDividerRatio` | `float`   | `2.0`             | Spannungsteiler-Faktor am Akku-Pin        |
| `scaleMode`        | `uint8_t` | `0`               | 0 = Game, 1 = Standard                   |
| `autoResetRange`   | `uint8_t` | `10`              | Auto-Reset-Schwelle [%] bei schlechtem Ergebnis |
| `autoZeroEnabled`  | `bool`    | `true`            | Zero-Tracking aktivieren                  |
| `autoZeroThreshold`| `float`   | `2.0`             | Maximalgewicht für Auto-Tare [g]          |
| `autoZeroDelay`    | `uint8_t` | `5`               | Stabile Zeit vor Auto-Tare [Sekunden]     |

> `tareOffset` entfernt — wird nie wiederhergestellt, Nullausgleich erfolgt immer live via `hx711.tare()`.

---

## Zero Tracking (Auto-Tare)

Kompensiert elektronischen Drift und Kriecheffekte der Wiegezelle.

**Bedingung für Auto-Tare:**
1. Zustand ist `Idle`
2. `|weight| < autoZeroThreshold` (Waage ist leer)
3. Diese Bedingung ist seit `autoZeroDelay` Sekunden stabil
4. `autoZeroEnabled == true`

**Effekt:** `hx711.tare(10)` wird aufgerufen → Nullpunkt neu gesetzt.

**Sicherheit:** Der Schwellwert (`autoZeroThreshold`, default 2g) liegt weit unter einem typischen leeren Trinkbecher (~150g), sodass kein versehentliches Tare bei aufgestelltem Glas passiert.

---

## State Machine

```
         Glas ≥ Ziel (Game-Mode)
Idle ──────────────────────────────► Tare
 ▲                                     │
 │ resetState()                        │ Glas abgehoben (< voll − Toleranz)
 │                                     ▼
 │                                  Drinking
 │                                     │
 │ resetState()                        │ Glas aufgestellt (≥ Ziel)
 │                                     ▼
 └──────────────────────────────── Result
         auto-reset oder Taster
```

### Zustandsbeschreibungen

| Zustand    | Anzeige                                      | Übergang                                                  |
|------------|----------------------------------------------|-----------------------------------------------------------|
| `Idle`     | Game: Zielgewicht + Rahmen wenn Gewicht drauf | → `Tare` wenn Gewicht ≥ Ziel                             |
|            | Standard: aktuelles Gewicht                  |                                                           |
| `Tare`     | "Bereit?" → zufälliger Trinkspruch           | → `Drinking` wenn Gewicht < voll − Toleranz               |
| `Drinking` | Lade-Animation (5 Kreise)                    | → `Result` wenn Gewicht ≥ Ziel                            |
| `Result`   | Getrunkene Menge + Bewertung (wechselt alle 3s mit Zeit) | Auto-Reset bei schlechtem Ergebnis + Glasabheben |

### Bewertungsstufen (Result)

| Bedingung                          | Meldung       |
|------------------------------------|---------------|
| Exakt Ziel (auf 0,01 g)            | `Perfekt!`    |
| ±0,1 g                             | `Not Bad!`    |
| ±1,0 g                             | `Ganz ok!`    |
| Zu wenig getrunken                 | `Schuchtern`  |
| Zu viel getrunken                  | `Zu gierig!`  |

---

## Taster-Gesten

| Dauer       | Aktion                                         |
|-------------|------------------------------------------------|
| < 3 s       | Tara / State-Reset                             |
| 3–5 s       | Modus-Vorschau (Game ↔ Standard), commit on release |
| ≥ 5 s       | WiFi-Toggle, commit on release: AP an → Funk + AP aus; sonst → Funk + AP an |

---

## Power Management

- **Deep-Sleep:** Nach `sleepTimeout` Minuten Inaktivität im Idle-Zustand (nur wenn der Config-AP aus ist und keine Duell-Runde/kein Nachlauf aktiv ist; der Duell-Funk wird dabei abgeschaltet)
- **Aufwachen:** GPIO 5 HIGH (Taster-Druck)
- **HX711 Power-Down:** CLK HIGH für >60 µs vor Sleep
- **OLED aus:** vor Sleep
- **Aktivitäts-Tracking:** Jede Gewichtsänderung >2 g, jeder Zustandswechsel, jeder Button-Press

---

## WiFi / Web-Konfiguration

- **Funk und AP getrennt:** Der 5-s-Taster schaltet den Funk (ESP-NOW für das
  Duell, `WIFI_STA`) zusammen mit dem Config-AP (`WIFI_AP_STA`) ein. Beide laufen
  fest auf Kanal 1 (`DUELL_CHANNEL`).
- **Auto-Stop:** nach `wifiTimeout` Minuten ohne HTTP-Aktivität geht **nur der AP**
  aus — das Duell läuft weiter. Erneuter 5-s-Hold schaltet den AP wieder an,
  ein weiterer schaltet alles aus.
- **Modus:** Access Point (kein Internet nötig)
- **SSID:** Steht sie auf dem Default `100-Waage-Config`, wird zur Laufzeit
  `100-Waage-XXXX` (letzte MAC-Bytes) verwendet, damit mehrere Waagen
  unterscheidbar sind. mDNS bleibt `waage.local` (jeder AP ist ein eigenes Netz).
- **Captive Portal:** DNS-Server leitet alle Anfragen auf AP-IP um
- **AP-Stabilität:** Bei Client-Disconnect wird der AP neu gestartet (Workaround
  für unsichtbaren AP); der Event-Handler wird nur einmal registriert und greift
  nur bei laufendem AP.

### Web-API (aktuell, Phase 1)

| Methode | Pfad               | Beschreibung                                  |
|---------|--------------------|-----------------------------------------------|
| GET     | `/`                | Konfigurations-HTML                           |
| POST    | `/save`            | Konfiguration speichern (public + admin)      |
| GET     | `/status`          | JSON: Gewicht, Akku, Modus, WiFi-Status       |
| POST    | `/calibrate`       | Kalibrierung starten (bekanntes Gewicht)      |
| GET     | `/calibrate_result`| Kalibrierungsergebnis anzeigen                |
| *       | `/*`               | 302 Redirect → `/`                            |

### Konfigurierbar ohne Passwort (public)
- Zielgewicht
- Display-Rotation

### Konfigurierbar mit Passwort (admin)
- AP-SSID, Kalibrierfaktor, Tara-Offset, Toleranz
- WiFi/Sleep-Timeouts, Auto-Reset-Bereich
- Spannungsteiler-Verhältnis, Passwort ändern
- Kalibrierung starten

---

## Modulstruktur (Phase 1)

```
waage/
├── waage.ino          — Einstiegspunkt: setup(), loop(), Pins, Button, Sleep
├── types.h            — WaageConfig Struct, State/ScaleMode Enums
├── config.h/.cpp      — EEPROM: loadConfig, saveConfig, defaultConfig
├── display.h/.cpp     — Display-Funktionen, Icons, Animationen, Trinksprüche
├── state.h/.cpp       — State-Machine-Logik
├── duell_core.h/.cpp  — Duell-Protokoll v3, reine Logik (host-testbar)
├── duell.h/.cpp       — ESP-NOW-Hülle um duell_core
└── webconfig.h/.cpp   — Web-Server (AP, Captive Portal, Config, /duell)
test/
├── run.sh             — Host-Tests bauen und ausführen
├── duell_core_test.cpp — Wire-Format, Merge, Ranking
└── duell_sim_test.cpp — Simulation mehrerer Waagen mit Paketverlust
```

---

## Phase 2: Web-Neubau — Spezifikation

### Authentifizierung

Zwei getrennte Seiten mit Session-Cookie:

| Route         | Verhalten                                              |
|---------------|--------------------------------------------------------|
| `GET /`       | Öffentliche Einstellungen (kein Auth nötig)            |
| `GET /admin`  | Admin-Dashboard (redirect zu Login wenn kein Cookie)   |
| `POST /login` | Passwort prüfen → Session-Cookie setzen                |
| `GET /logout` | Cookie löschen → redirect zu `/admin`                  |

Session-Cookie läuft mit der WiFi-Verbindung ab (kein Expiry nötig, AP wird manuell gestoppt).

### Routen-Übersicht

| Methode | Pfad                 | Auth     | Beschreibung                            |
|---------|----------------------|----------|-----------------------------------------|
| GET     | `/`                  | —        | Öffentliche Einstellungen + Live-Status |
| POST    | `/save`              | —        | goal, displayRotation speichern         |
| GET     | `/admin`             | Cookie   | Admin-Dashboard                         |
| POST    | `/admin/save`        | Cookie   | Admin-Felder speichern + Neustart       |
| POST    | `/login`             | —        | Passwort prüfen, Cookie setzen          |
| GET     | `/logout`            | —        | Cookie löschen                          |
| GET     | `/status`            | —        | JSON: Gewicht, Akku, Modus, WiFi        |
| POST    | `/calibrate`         | Cookie   | Kalibrierung starten (async)            |
| GET     | `/calibrate/status`  | Cookie   | `{"state":"running"\|"done","scaleFactor":123.4}` |
| GET     | `/duell`             | —        | JSON: eigene MAC, Protokoll, Peers, aktuelle/letzte Rundentabelle mit Rängen |
| *       | `/*`                 | —        | 302 → `/`                              |

### Öffentliche Seite (`/`)

```
[ Live-Status: 47.2g | Akku 82% | Game-Mode | WiFi: AN ]

Zielgewicht [g]:     [ 100.0 ]
Display-Rotation:    [ 180° ▾]

[ Speichern ]

[ 🔒 Admin-Einstellungen → ]
```

### Admin-Dashboard (`/admin`)

Felder:
- AP-SSID
- Toleranz [g]
- Auto-Reset-Bereich [%]
- WiFi Auto-Aus [min]
- Deep-Sleep nach [min]
- Auto-Zero: aktiviert / Schwellwert [g] / Verzögerung [s]

Nach Save: automatischer Neustart des ESP32.

Kalibrierung (auf Admin-Seite):
- Während einer Duell-Runde abgelehnt (409), weil sie den Loop >10 s blockiert
- Eingabe: bekanntes Gewicht
- Nach Submit: Fortschrittsanzeige (polling `/calibrate/status` alle 500ms)
- Nach Abschluss: neuer Faktor angezeigt, gespeichert

### AP-Stabilität

- `WiFi.onEvent()` auf `ARDUINO_EVENT_WIFI_AP_STADISCONNECTED` → AP-Stack neu starten
- `WiFi.setTxPower(WIFI_POWER_8_5dBm)` für stabileres Signal
- Kein hartes ESP-Restart bei Client-Disconnect

### Nicht konfigurierbar im Web-UI (Hardcode)
- `battDividerRatio` — bleibt 2.0 (hardwareabhängig, selten geändert)
- `scaleFactor` — wird nur über Kalibrierungsflow gesetzt, kein freies Eingabefeld

---

## Duell-Modus (Multiplayer-Protokoll v3)

Mehrere Waagen spielen gemeinsam über **ESP-NOW-Broadcast** (kein Pairing,
Funk an, Game-Mode). Jede Waage kennt die **komplette Rundentabelle** und
berechnet das Ranking selbst — es gibt keinen Master mehr, der die Auswertung
übernimmt. Fällt eine Waage aus, werten die übrigen trotzdem korrekt aus.

> **Wichtig:** Bei Protokolländerungen wird `duell::MAGIC` (aktuell `0xD3`)
> erhöht. Alte und neue Firmware ignorieren sich dann gegenseitig —
> **alle Waagen müssen zusammen geflasht werden.**

Die Logik liegt in `duell_core.h/.cpp` ohne Arduino-Abhängigkeiten und wird
auf dem Host getestet (`test/run.sh`), inkl. Simulation
mehrerer Waagen mit 30 % Paketverlust.

### Nachricht

Eine einzige Nachricht, jede Waage sendet sie periodisch (1 s idle, 400 ms
aktiv, sofort bei Änderung):

```
magic u8 | phase u8 (Idle/Ready/InRound) | goal f32 | roundId u16 (0 = keine)
| elapsedDs u16 (Zehntelsek. seit Rundenstart) | target f32 | n u8
| n × { mac[6] | status u8 (Pending/Forfeit/Done) | result f32 | durationCs u16 }
```

Max. 11 Teilnehmer → 158 Byte. In der Runde (oder im Nachlauf) trägt jede
Nachricht die vollständige Tabelle.

### Ablauf einer Runde

1. Glas ≥ Ziel aufgestellt → Phase `Ready` (Anzeige „Warte… 2/3 bereit“).
2. **Start:** Die Waage mit der höchsten MAC unter allen aktiven Waagen startet
   die Runde, wenn **alle** aktiven Waagen 3 s lang `Ready` sind (Karenz
   beginnt neu, wenn eine Waage dazukommt/wegfällt). Erst 5 s nach Funkstart
   darf eine Waage Leader sein (verhindert Doppelrunden beim Einschalten).
   Teilnehmer werden nach MAC sortiert, das Ziel aus ihren Zielen gewürfelt.
3. **Beitritt:** Jede Nachricht eines Teilnehmers reicht (nicht nur die des
   Leaders), solange die Runde < 10 s alt ist und der eigene Eintrag `Pending` ist.
4. **Ergebnis:** Nach dem Trinken setzt die Waage ihren eigenen Eintrag auf
   `Done` (Gramm + Zeit). Tabellen werden monoton gemerged:
   `Pending < Forfeit < Done`, Done schlägt Forfeit, den eigenen Eintrag setzt
   nur die Waage selbst.
5. **Ranking** lokal auf jeder Waage: Abstand zum Ziel in 0,01 g, bei Gleichstand
   die kürzere Zeit, sonst geteilter Platz (1, 1, 3). Forfeit = kein Platz.
   **Final**, sobald niemand mehr `Pending` ist.

### Anzeige

| Situation | Anzeige |
|---|---|
| Warten auf Start | „Warte…“ / „2/3 bereit“ |
| Rundenstart | „Ziel“ / „100.0g“ |
| Eigenes Ergebnis, andere trinken noch | „12.3g“ / „~2. Platz“ im Wechsel mit „2/3 fertig“ / Zeit |
| Final | „12.3g“ / „2. Platz!“ im Wechsel mit Zeit |
| Harter Rundentimeout vor eigenem Ergebnis | „Zu spaet!“ |

Der finale Platz bleibt stehen, bis der **Taster** gedrückt wird oder ein
**neues Glas** kommt (Glas abheben, dann volles Glas ≥ Ziel aufstellen; frühestens
5 s nach final). Das neue Glas wird dabei nicht weggenullt. Glas-Abheben allein
löscht im Duell nichts mehr.

### Robustheit

| Mechanismus | Wert |
|---|---|
| Peer gilt als aktiv | letzte Nachricht < 5 s |
| Peer wird vergessen | keine Nachricht > 10 s |
| Forfeit: Teilnehmer drückt Taster ohne Ergebnis | sofort (eigener Eintrag) |
| Forfeit: Teilnehmer nachweislich nicht in der Runde (Idle bzw. andere Runde) | sofort |
| Forfeit: Teilnehmer hat den Beitritt verpasst (weiter `Ready`) | nach 15 s |
| Forfeit: Teilnehmer nicht mehr gehört | 30 s |
| Harter Rundentimeout (alle `Pending` → Forfeit) | 180 s |
| Nachlauf: verlassene Runde wird weitergesendet | 20 s |
| `WaitReady`-Timeout (vor dem Start) | 60 s, außerdem Abbruch wenn Gegner weg oder Glas abgehoben → Solo-Spiel |

Es gibt keinen Solo-Fallback mehr, solange die Runde lebt. Geht die Runde
verloren (Funk aus), wird solo gegen das Duell-Ziel bewertet.

Der ESP-NOW-Empfang läuft im WiFi-Task und legt Pakete nur in eine Queue
(32 Pakete); verarbeitet wird ausschließlich im Main Loop (keine Race Conditions).

### Gewichtsmessung (nicht-blockierend)

`updateWeight()` liest pro Loop höchstens ein HX711-Sample und mittelt über
die letzten 10 (gleiche Glättung wie früher `get_units(10)`, blockiert aber
nicht mehr ~1 s). Zustandsübergänge nutzen ein Settle-Fenster von 1,5 s —
länger als das Mittelungsfenster, damit der übernommene Wert sauber ist.
Die Standard-Mode-Anzeige bleibt damit auf 0,1 g genau.
