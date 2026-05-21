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
| ≥ 5 s       | WiFi-Toggle (AN ↔ AUS), commit on release      |

---

## Power Management

- **Deep-Sleep:** Nach `sleepTimeout` Minuten Inaktivität im Idle-Zustand (nur wenn WiFi aus)
- **Aufwachen:** GPIO 5 HIGH (Taster-Druck)
- **HX711 Power-Down:** CLK HIGH für >60 µs vor Sleep
- **OLED aus:** vor Sleep
- **Aktivitäts-Tracking:** Jede Gewichtsänderung >2 g, jeder Zustandswechsel, jeder Button-Press

---

## WiFi / Web-Konfiguration

- **Modus:** Access Point (kein Internet nötig)
- **Captive Portal:** DNS-Server leitet alle Anfragen auf AP-IP um
- **Start:** 5-Sekunden-Taster-Hold
- **Auto-Stop:** nach `wifiTimeout` Minuten ohne HTTP-Aktivität
- **Bekannter Bug:** AP kann nach erstem Client-Disconnect unsichtbar werden → wird in Phase 2 behoben

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
└── webconfig.h/.cpp   — Web-Server Stub (Phase 2: Neubau)
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
