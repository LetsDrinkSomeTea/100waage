# 100-Waage – Technische Spezifikation

Bedienung aus Nutzersicht: [README.md](README.md). Dieses Dokument beschreibt
das Verhalten der Firmware so, wie es im Code umgesetzt ist. Alle Zeiten sind
benannte Konstanten in ms; die Namen stehen in Klammern.

## Hardware

| Komponente | Details                                                                                                                                                       |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MCU        | ESP32-C3 Super Mini (FQBN `esp32:esp32:nologo_esp32c3_super_mini`)                                                                                            |
| Wägezelle  | HX711, 10 Samples/s (RATE-Pin auf GND). Ein auf 80 SPS umgelötetes Modul wird automatisch erkannt (gemessene Sample-Rate), alle Fenster sind in ms definiert. |
| Display    | SSD1306 128×32, I2C-Adresse `0x3C`, kein Reset-Pin (`OLED_RESET = -1`)                                                                                        |
| Taster     | GPIO 5, HIGH = gedrückt, Wake-up-Quelle für den Deep-Sleep                                                                                                    |
| Akku       | Li-Ion mit Schutzschaltung (Tiefentladeschutz in Hardware), Spannungsteiler an GPIO 0                                                                         |

| Funktion        | GPIO    |
| --------------- | ------- |
| OLED SDA / SCL  | 8 / 9   |
| HX711 DAT / CLK | 21 / 20 |
| Taster          | 5       |
| Akku-ADC        | 0       |

### Compile-Schalter

| Schalter               | Datei     | Wert    | Wirkung                                        |
| ---------------------- | --------- | ------- | ---------------------------------------------- |
| `RESET_CONFIG_ENABLED` | `app.cpp` | `false` | Werksreset: Taster beim Einschalten 3 s halten |

## Modulstruktur

Muster: reine, Arduino-freie Kerne (`*_core.*`, auf dem PC getestet) und dünne
Adapter für die Hardware. Kein ISR, kein eigener Task; alles läuft aus `loop()`.
Einzige Ausnahme ist der ESP-NOW-Empfang (WiFi-Task), der Pakete nur in eine
Queue legt.

| Kern (rein)    | Inhalt                                                                                                                                                      |
| -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `config_core`  | Config-Modell, Defaults, `sanitize` (Laden), `validate` (Web), Parser, UTF-8-Kürzung, AP-Name, Änderungsmaske, Import des alten EEPROM-Abbilds, Zufallsziel |
| `scale_core`   | Sample-Puffer, adaptiver Filter, Stabilität, nicht blockierende Tara, Nullung aus dem Fenster, Kalibrier-Assistent                                          |
| `game_core`    | Solo- und Duell-Ablauf, Bewertung, Ergebnisregeln, Auto-Zero/NegZero-Auslöser                                                                               |
| `duell_core`   | Duell-Protokoll v3: Nachrichtenformat, Rundentabelle, Merge, Ranking                                                                                        |
| `button_core`  | Entprellung, Haltezonen, Overlay                                                                                                                            |
| `battery_core` | Spannung → Prozent, Glättung, Hysterese, Warnung, Teiler-Abgleich                                                                                           |
| `power_core`   | wrap-sichere Zeiten, Sleep-Policy, AP-Auto-Aus                                                                                                              |
| `text_core`    | UTF-8 → CP437, Zeilenlayout, Zahlformate, Trinksprüche                                                                                                      |
| `stats_core`   | Statistik: Stufen, Bestwert, schnellste Zeit, Erfolg der Runde, Duell-Siege, Verlauf                                                                        |
| `ui_model`     | baut pro Loop ein vergleichbares Bild (Ebenen, Symbole, Texte)                                                                                              |
| `web_core`     | JSON-Writer, Cookie-Parser, konstantzeitiger Vergleich, Token, Login-Bremse                                                                                 |

| Adapter               | Inhalt                                                                       |
| --------------------- | ---------------------------------------------------------------------------- |
| `waage.ino`           | nur `app_setup()` / `app_loop()`                                             |
| `app`                 | verbindet alle Module, Taster-Aktionen, Kalibrierung, OTA-Status, Deep-Sleep |
| `config`              | NVS-Speicher (Preferences), Import beim ersten Start                         |
| `scale`               | HX711-Ansteuerung, Sensorfehler, Power-Down                                  |
| `battery`             | ADC-Messung                                                                  |
| `stats`               | Statistik im NVS (Namespace `stats`), JSON für das Web                       |
| `display` / `ui`      | Zeichenprimitive / rendert nur bei Änderung                                  |
| `duell`               | ESP-NOW, Empfangs-Queue, Ausstieg mit Flush, `DuelPort` für `game_core`      |
| `radio`               | Funk- und AP-Lebenszyklus, verzögerter AP-Neustart und Reboot                |
| `web` / `web_pages.h` | Webserver, JSON-API, statische Seiten                                        |
| `version.h`           | `FW_VERSION` aus `version_gen.h` (erzeugt) oder `dev-<Datum>`                |

## Konfiguration

### Felder

| Feld                | Default            | Bereich                                                  | Web                 |
| ------------------- | ------------------ | -------------------------------------------------------- | ------------------- |
| `apSSID`            | `100-Waage-Config` | 1–32 Byte, keine Steuerzeichen                           | Admin               |
| `adminPassword`     | `admin`            | neu gesetzt: 4–31 Byte                                   | Admin               |
| `scaleFactor`       | 708                | endlich, \|f\| ≥ 1                                       | Kalibrier-Assistent |
| `goal`              | 100,0 g            | 1–5000 g, 0,1-g-Raster, ≥ `tolerance` + 1                | Start               |
| `tolerance`         | 10 g               | 0,5–100 g                                                | Admin               |
| `displayRotation`   | 0                  | 0 oder 2 (180°)                                          | Start               |
| `wifiTimeout`       | 10 min             | 0–255, 0 = nie                                           | Admin               |
| `sleepTimeout`      | 5 min              | 0–255, 0 = nie                                           | Admin               |
| `battDividerRatio`  | 2,0                | 1–6                                                      | Akku-Abgleich       |
| `batteryPresent`    | an                 | aus = Netzbetrieb (keine Messung, kein Symbol)           | Admin               |
| `scaleMode`         | Game               | Game / Duel / Standard (gespeichert 0 / 2 / 1)           | Start               |
| `autoResetRange`    | 10 %               | 0–100                                                    | Admin               |
| `autoZeroEnabled`   | an                 |                                                          | Admin               |
| `autoZeroThreshold` | 2 g                | 0,1–20 g, ≤ `tolerance`                                  | Admin               |
| `autoZeroDelay`     | 5 s                | 1–60                                                     | Admin               |
| `randomModeEnabled` | aus                |                                                          | Start               |
| `randomMin`         | 20 g               | wird auf [min(`tolerance` + 1, `goal`), `goal`] geklemmt | Start               |
| `statsRotation`     | an                 |                                                          | Start               |
| `statsAfterS`       | 20 s               | 1–255                                                    | Admin               |
| `statsStepS`        | 3 s                | 1–60                                                     | Admin               |
| `statsGoalS`        | 6 s                | 1–60                                                     | Admin               |

Beim **Laden** klemmt `sanitize` jeden Wert in seinen Bereich und scheitert nie
(ein gültiger `scaleFactor` bleibt bit-genau). Aus dem **Web** prüft `validate`
strikt und lehnt mit deutscher Meldung und Feldnamen ab (HTTP 400); nur
`randomMin` wird geklemmt. Zahlen dürfen `,` als Dezimaltrenner haben, der ganze
String muss eine endliche Zahl sein.

Der wirksame AP-Name ist bei Default-SSID `100-Waage-XXXX` (letzte zwei
MAC-Bytes), damit mehrere Waagen unterscheidbar sind; sonst die SSID selbst.

### Speicher (NVS)

Namespace `waage`, ein Schlüssel pro Feld. `schema` (aktuell 1) wird zuletzt
geschrieben; fehlt er, gilt der Speicher als leer.

| Schlüssel | Feld              | Schlüssel | Feld             |
| --------- | ----------------- | --------- | ---------------- |
| `ssid`    | apSSID            | `pw`      | adminPassword    |
| `scale`   | scaleFactor       | `goal`    | goal             |
| `tol`     | tolerance         | `arRange` | autoResetRange   |
| `rot`     | displayRotation   | `wifiTo`  | wifiTimeout      |
| `sleepTo` | sleepTimeout      | `battDiv` | battDividerRatio |
| `mode`    | scaleMode         | `azOn`    | autoZeroEnabled  |
| `azThr`   | autoZeroThreshold | `azDelay` | autoZeroDelay    |
| `rndOn`   | randomModeEnabled | `rndMin`  | randomMin        |

`config_set` schreibt nur die geänderten Schlüssel (Änderungsmaske `cfg::diff`).

### Migration vom alten Format

Beim ersten Start ohne `schema` wird das alte Abbild der Arduino-EEPROM-Emulation
(NVS-Namespace und -Key `eeprom`) **nur lesend** geöffnet und mit festen
Little-Endian-Offsets dekodiert (Magic `0xCD` = 136 Byte mit Zufallsfeldern,
`0xCC` = 132 Byte ohne). Danach `sanitize` und Schreiben ins neue Format. Das
alte Abbild bleibt unverändert, ein Downgrade ist also möglich. Ohne gültiges
Abbild gelten die Defaults. Ein Werksreset schreibt Defaults inklusive `schema`
(kein erneuter Import).

### Live-Übernahme

Änderungen gelten ohne Neustart:

| Änderung     | Wirkung                                                                                                                                   |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| Ziel, Zufall | im Game-Idle sofort (ohne Tara), sonst ab dem nächsten Reset                                                                              |
| Rotation     | sofort                                                                                                                                    |
| Modus        | wie der Taster: Reset mit Tara, Duell verlassen und Ausstieg senden. Aus dem Web während Spiel, offener Runde, Kalibrierung oder OTA: 409 |
| Toleranz     | neue Stabilitätsschwelle                                                                                                                  |
| Akku-Teiler  | Akku wird neu bewertet                                                                                                                    |
| SSID         | AP startet 1 s nach der Antwort neu, falls sich der wirksame Name ändert                                                                  |
| Passwort     | alle Sitzungen ungültig, neu anmelden                                                                                                     |

## Waage

### Messung

`scale.cpp` fragt den HX711 per `is_ready()` ab (nie blockierend) und gibt jedes
Rohsample mit Zeitstempel an `scale::Core`. Gramm = (raw − offset) / factor,
wie in der HX711-Bibliothek.

- **Anzeige-Filter:** gleitendes Mittel über höchstens `DISPLAY_MS` = 1000 ms.
  Ein Sprung > `STEP_G` = 2 g startet die Glättung neu, damit Aufstellen und
  Abheben sofort sichtbar sind.
- **Stabilität:** die Samples der letzten `STABLE_MS` = 500 ms decken das
  Fenster ab und ihre Spanne (über ~100-ms-Mittel) ist ≤ `stableSpread`
  = max(1 g, `tolerance` / 5).
- **Sensorfehler:** kommt `NO_SAMPLE_MS` = 1000 ms lang kein Sample, gilt der
  Sensor als ausgefallen (Anzeige „Sensorfehler“, frühestens 2 s nach dem Start).
  Loop-Pausen über 500 ms (OTA, AP-Start) zählen nicht. Kommt der Sensor zurück,
  wird das Spiel zurückgesetzt und tariert. Ohne HX711 startet die Waage trotzdem.
- **Deep-Sleep:** HX711 `power_down()`, CLK-Pin per GPIO-Hold auf HIGH (der HX711
  bleibt im Schlaf aus); beim Start wird der Hold wieder gelöst.

### Tara und Nullung

Jede Tara ist nicht blockierend: `TARE_DISCARD_MS` = 100 ms verwerfen, dann
Offset = Mittel aus `TARE_MIN_SAMPLES` = 5 stabilen Samples (ca. 0,6 s bei
10 SPS), spätestens nach `TARE_MAX_MS` = 2000 ms mit dem Mittel aller
gesammelten Samples. Währenddessen pausiert das Spiel; das Display zeigt ohne
eigenen Hinweis den Idle-Bildschirm (Game/Duell: Ziel, Standard: 0.0g).

`game_core` fordert die Waage über `ScaleReq` an:

| Anforderung | Wann                                                                                              | Ausführung                                                                              |
| ----------- | ------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `Tare`      | jeder Reset: Kurzdruck, Moduswechsel, Start, Sensor zurück, Ende der Kalibrierung                 | frische Tara                                                                            |
| `TareEmpty` | schlechtes Ergebnis, Glas abgehoben                                                               | Nullung aus dem Fenster (\|Mittel\| ≤ `tolerance`, Spanne ≤ `stableSpread`), sonst Tara |
| `AutoZero`  | Idle, \|w\| < `autoZeroThreshold`, stabil für `autoZeroDelay` s; danach Pause 3 × `autoZeroDelay` | Nullung aus dem Fenster (Mittel und Spanne ≤ Schwelle)                                  |
| `NegZero`   | Game-Idle, w < −`tolerance` und stabil für `NEGZERO_MS` = 1000 ms (mit Glas tariert, Glas weg)    | Nullung aus dem Fenster ohne Betragsgrenze                                              |

Die Nullung aus dem Fenster verschiebt den Offset sofort um das Mittel der
bereits geprüften Samples; sie blockiert nicht und nullt kein gerade
aufgestelltes Glas mit, weil sie ein stabiles Fenster verlangt.

Im Standard-Modus zeigt die Waage 0,0 g, solange Auto-Zero aktiv ist und
\|w\| < Schwelle.

### Kalibrier-Assistent

Gestartet aus dem Admin-Bereich (nicht während Spiel, Runde oder Sensorfehler):

1. **Prepare** – „Waage leeren“ für `CAL_PREPARE_MS` = 2 s.
2. **Taring** – Tara wie oben.
3. **WaitWeight** – bekanntes Gewicht auflegen, im Web eintragen, „Messen“
   (0,5–5000 g).
4. **Measuring** – bis stabil, spätestens `CAL_MEASURE_MAX_MS` = 5 s, dann Mittel.
   Rohdelta < 1000 Zählschritte → Fehler „kein Gewicht“.
5. **Done** – neuer Faktor gesetzt und gespeichert.
6. **RemoveWeight** – bis \|w\| < `tolerance` für 1 s, dann Reset mit Tara.

Timeout 120 s für Taring, WaitWeight und Measuring. Fehler und Abbruch stellen
Offset und Faktor wieder her. Ein Fehler steht 10 s auf dem Display
(`CAL_ERROR_SHOW_MS`), danach geht es mit Reset und Tara weiter, damit die Waage
nicht wach bleibt, wenn niemand quittiert. Jeder Reset (Kurzdruck, Moduswechsel,
Sensor zurück) bricht eine laufende Kalibrierung ab und tariert (liegt das
Gewicht noch, einfach erneut tarieren).

## Spielablauf (Game- und Duell-Modus)

### Zustände

Phasen `Idle → Ready → Drinking → Result`, im Duell zusätzlich
`Offline | WaitReady | WaitStart | Live`. Solange keine gültige Messung vorliegt
(Tara, Sensorfehler, Kalibrierung, OTA), ändert sich nichts.

| Übergang          | Bedingung (Konstante)                                                                                                                           |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| Idle → Ready      | w ≥ Ziel und stabil 500 ms (`PLACE_STABLE_MS`); `fullWeight` = w                                                                                |
| Ready → Drinking  | w ≤ `fullWeight` − `tolerance` für 300 ms (`LIFT_MS`); Startzeit = erstes Unterschreiten, `emptyWeight` = w                                     |
| während Drinking  | `emptyWeight` wird nachgeführt, solange das Glas weg und stabil ist                                                                             |
| Drinking → Result | w ≥ `emptyWeight` + `tolerance` (Endzeit = erstes Überschreiten), dann stabil 500 ms (`RETURN_STABLE_MS`), spätestens 1500 ms (`RETURN_MAX_MS`) |
| Result → Idle     | siehe Ergebnisregeln                                                                                                                            |

Es wird immer ein volles Glas aufgestellt (kein Einschenken auf der Waage).

### Bewertung

Getrunken = `fullWeight` − Endgewicht, gerundet auf 0,01 g (Centigramm,
`toCg`). Die erste Nachkommastelle ist exakt, die zweite Glück. d = getrunken −
Ziel (beides in cg):

| d              | Bewertung  |
| -------------- | ---------- |
| 0              | Perfekt!   |
| \|d\| ≤ 0,10 g | Not Bad!   |
| \|d\| ≤ 1,00 g | Ganz ok!   |
| d < 0          | Schüchtern |
| d > 0          | Zu gierig! |

**Gut** heißt \|d\| ≤ `autoResetRange` % vom Ziel.

### Ergebnisregeln

- **Gut** bleibt zum Prahlen stehen, bis der Taster gedrückt wird.
- **Schlecht** verschwindet, wenn das Glas abgehoben ist (w < `emptyWeight` +
  `tolerance` für 500 ms, `REMOVED_MS`); Reset mit `TareEmpty`.
- **Duell:** dieselben Regeln, aber ein schlechtes Ergebnis erst, wenn der Rang
  final ist und mindestens 3 s (`FINAL_MIN_SHOW_MS`) angezeigt wurde. Aufgegeben
  („Zu spät!“) zählt als schlecht.
- Wurde mit Glas tariert und das Glas danach weggenommen, greift NegZero.

### Zufallsziel

Bei jedem Reset wird neu gewürfelt: ganze Gramm in
[⌈max(`randomMin`, `tolerance` + 1)⌉, ⌊`goal`⌋].

## Duell (Protokoll v3)

Mehrere Waagen spielen per **ESP-NOW-Broadcast** auf Kanal 1 (kein Pairing).
Voraussetzung: Duell-Modus (Funk läuft dort immer). Jede Waage kennt die
komplette Rundentabelle und berechnet das Ranking selbst; es gibt keinen
Master. Die Logik liegt in `duell_core` und wird auf dem PC mit mehreren simulierten Waagen
und 30 % Paketverlust getestet.

> Bei Protokolländerungen wird `duell::MAGIC` (aktuell `0xD3`) erhöht. Alte und
> neue Firmware ignorieren sich dann; **alle Waagen zusammen flashen**.

### Nachricht

Eine einzige Nachricht, gesendet alle 1 s (`HEARTBEAT_IDLE_MS`), in einer Runde
alle 250 ms (`HEARTBEAT_ACTIVE_MS`) und sofort bei jeder Änderung:

```
magic u8 | phase u8 (Idle/Ready/InRound) | goal f32 | roundId u16 (0 = keine)
| elapsedDs u16 (Zehntelsek. seit Rundenstart) | target f32 | n u8
| n × { mac[6] | status u8 (Pending/Forfeit/Done) | result f32 | durationCs u16 }
```

Little Endian, Kopf 15 Byte, Eintrag 13 Byte, max. 11 Teilnehmer → 158 Byte.

### Ablauf

1. Glas aufgestellt (Ready) → Waage meldet `Ready`, Anzeige „Warte...“ /
   „2/3 bereit“.
2. **Start:** Die Waage mit der höchsten MAC unter allen aktiven Waagen startet,
   wenn **alle** 1,5 s lang (`READY_GRACE_MS`) `Ready` sind; die Karenz beginnt
   neu, wenn eine Waage dazukommt oder wegfällt. Erst 5 s nach Funkstart
   (`STARTUP_GUARD_MS`) darf eine Waage starten. Teilnehmer nach MAC sortiert,
   Ziel aus ihren Zielen gewürfelt.
3. **Beitritt:** jede Nachricht eines Teilnehmers reicht, solange die Runde
   < 10 s (`JOIN_WINDOW_MS`) alt und der eigene Eintrag `Pending` ist.
4. **Ergebnis:** die Waage setzt ihren Eintrag auf `Done` (Gramm, Zeit).
   Tabellen werden monoton gemerged (`Pending < Forfeit < Done`); den eigenen
   Eintrag ändert nur die Waage selbst.
5. **Ranking** lokal: Abstand zum Ziel in 0,01 g, bei Gleichstand die kürzere
   Zeit, sonst geteilter Platz (1, 1, 3). Forfeit = kein Platz. **Final**,
   sobald niemand mehr `Pending` ist.

### Anzeige

| Situation                             | Anzeige                                                   |
| ------------------------------------- | --------------------------------------------------------- |
| Warten auf Start                      | „Warte...“ / „2/3 bereit“                                 |
| Rundenstart                           | Duell-Ziel                                                |
| eigenes Ergebnis, andere trinken noch | Gramm (0,01 g) / „~2. Platz“, im Wechsel mit „2/3 fertig“ |
| final                                 | Gramm / „2. Platz!“, im Wechsel mit der Zeit              |
| aufgegeben                            | „Zu spät!“                                                |
| stiller Wechsel auf Solo              | Hinweis „Solo!“                                           |

Wird der Funk nach dem Final ausgeschaltet (Moduswechsel), bleibt der letzte
finale Rang stehen.

### Ausstieg

Kurzdruck, Moduswechsel und Deep-Sleep verlassen eine Runde. Vor dem
Ausschalten wird die Abmeldung dreimal im Abstand von 25 ms gesendet
(`duell_flush_burst`), damit die anderen Waagen sofort Bescheid wissen statt
erst nach 30 s.

### Robustheit

| Mechanismus                                                          | Wert                                                  |
| -------------------------------------------------------------------- | ----------------------------------------------------- |
| Peer gilt als aktiv                                                  | letzte Nachricht < 5 s                                |
| Peer wird vergessen                                                  | keine Nachricht > 10 s                                |
| Forfeit: Teilnehmer verlässt die Runde (Taster, Moduswechsel, Sleep) | sofort                                                |
| Forfeit: Teilnehmer nachweislich in keiner/anderer Runde             | sofort                                                |
| Forfeit: Beitritt verpasst (weiter `Ready`)                          | nach 15 s                                             |
| Forfeit: Teilnehmer nicht mehr gehört                                | 30 s                                                  |
| Harter Rundentimeout (alle `Pending` → Forfeit)                      | 180 s                                                 |
| Nachlauf: verlassene Runde wird weiter gesendet                      | 20 s                                                  |
| WaitReady: Gegner weg oder 60 s ohne Start                           | Solo                                                  |
| WaitReady: Glas 300 ms abgehoben                                     | Solo, direkt Trinken (kürzeres Anheben ändert nichts) |
| Runde vor dem Final verloren                                         | Solo gegen das Duell-Ziel                             |

Eine Waage, die nicht bereit ist (z. B. weil sie noch ein gutes Ergebnis zeigt),
hält den Start auf, bis jemand drückt oder die 60 s ablaufen. Das ist gewollt.

Der Empfang legt Pakete nur in eine Queue (32 Pakete); verarbeitet wird
ausschließlich im Loop. Läuft der Funk außerhalb des Duell-Modus (nur für den
AP), wird die Queue jede Loop geleert und die Waage sendet nichts; so erzeugen
nach dem Wechsel in den Duell-Modus keine alten Pakete Geister-Gegner.

## Taster

Polling im Loop, kein Interrupt. Entprellung 30 ms, Drücke unter 50 ms zählen
nicht. Jede Flanke zählt als Aktivität für den Deep-Sleep. Der Druck, der die
Waage aufweckt, löst nichts aus.

| Haltezeit beim Loslassen | Aktion                                          | Text im Balken                              |
| ------------------------ | ----------------------------------------------- | ------------------------------------------- |
| < 0,75 s                 | Reset + Tara (bricht auch eine Kalibrierung ab) | „Tara“                                      |
| 0,75–2 s                 | nächster Modus: Game → Duell → Standard → Game  | Zielmodus, z. B. „Duell-Modus“, mit Punkten |
| 2–3,25 s                 | AP an/aus                                       | „AP an“ / „AP aus“                          |
| ≥ 3,25 s                 | nichts                                          | „Abbrechen“                                 |

Ab 300 ms zeigt das Display einen Balken über 3,25 s mit Marken bei 0,75 s und
2 s (Pixel 29 und 78); der Text zeigt die Wirkung beim Loslassen. In der
Modus-Zone steht der Zielmodus in kleiner Schrift, darunter drei Punkte
(Game, Duell, Standard) mit dem Zielmodus gefüllt. Der Hinweis nach dem
Wechsel zeigt Name und Punkte noch einmal. So wird die Reihenfolge sichtbar,
ohne sie erklären zu müssen.

### Modi und Funk

| Modus    | Spiel | Funk (ESP-NOW)      | Statusleiste |
| -------- | ----- | ------------------- | ------------ |
| Game     | solo  | aus (an nur mit AP) | Akku         |
| Duell    | Duell | immer an            | `Vs n`, Akku |
| Standard | Waage | aus (an nur mit AP) | Akku         |

Der Funk läuft genau dann, wenn der Duell-Modus aktiv ist oder der AP läuft
(`syncRadio` in `app.cpp`). AP aus schaltet im Duell-Modus nur den AP ab.
Außerhalb des Duell-Modus nimmt die Waage auch bei laufendem Funk nicht am
Duell teil und ist für andere unsichtbar.

## Anzeige

- OLED im CP437-Modus; `text_core` wandelt UTF-8 für ä ö ü Ä Ö Ü ß. Andere
  Zeichen ohne CP437-Entsprechung (z. B. „…“) sind nicht erlaubt, Texte nutzen
  „...“.
- `ui_model` baut pro Loop ein Bild; Ebenen in dieser Reihenfolge:
  Haltebalken > Systembildschirm (Update, Kalibrierung, Sensorfehler) > Hinweis (Toast,
  1,5 s) > Spiel.
- `ui` zeichnet nur, wenn sich das Bild ändert, höchstens alle 50 ms.
- „Bereit?“ erscheint 400 ms, dann ein Trinkspruch (gespeichert pro Runde).
  Ergebnisse wechseln alle 3 s zwischen Wert und Zeit. Ladeanimation mit 300 ms
  pro Schritt.

| Symbol              | Bedingung                                      |
| ------------------- | ---------------------------------------------- |
| Zufall (oben links) | Zufallsziel aktiv                              |
| WLAN-Bogen          | Config-AP läuft                                |
| `Vs n`              | Duell-Modus, n andere Waagen sichtbar (auch 0) |
| Akku mit Prozent    | Akku angeschlossen (ganz rechts)               |
| Akku mit `!`        | Akku-Warnung (statt der Füllung)               |

Rechts oben von rechts nach links: Akku, `Vs n`, WLAN-Bogen.

## Statistik

`stats_core` zählt pro Waage (nicht pro Person). Summen und Bestwerte liegen im
NVS (Namespace `stats`, mit Versionsbyte; bei anderer Version wird neu
begonnen), der Verlauf der letzten 10 Runden nur im RAM.

| Wert            | Regel                                                                       |
| --------------- | --------------------------------------------------------------------------- |
| Runde           | jedes fertige Ergebnis (solo oder Duell); abgebrochene Runden zählen nicht  |
| Stufen          | getrennt: \|d\| = 0 Perfekt, ≤ 0,10 g Not Bad, ≤ 1,00 g Ganz ok             |
| Bester Treffer  | kleinste \|d\| über alle Ziele, mit Ziel und Zeit (nur strikt besser zählt) |
| Schnellste Zeit | nur Runden mit \|d\| ≤ 10 % des Ziels (nur strikt schneller zählt)          |
| Duell           | finaler Stand mit ≥ 2 Teilnehmern; Sieg = Rang 1 ohne Aufgabe               |

d = getrunken − Ziel (im Duell gegen das Duell-Ziel).

**Erfolg im Ergebnis:** Neuer bester Treffer („Neuer Rekord!“ / „0.03g
daneben“) oder neue schnellste Zeit („Schnellste Zeit!“ / „3.87s“), höchstens
einer pro Runde, Rekord zuerst. Läuft als dritter Zustand im 3-s-Wechsel mit
(Wert → Zeit → Erfolg), im Duell erst, wenn der Rang final ist (nicht bei
Aufgabe). Auch die erste Runde ist ein Rekord.

**Info-Rotation** (`statsRotation`, Standard an): Im Game-Idle (Game- und
Duell-Modus) ohne Glas zeigt die Anzeige nach `statsAfterS` (20 s) die
Statistik-Bildschirme je `statsStepS` (3 s), danach das Ziel für `statsGoalS`
(6 s), dann wieder von vorn, auch vor der ersten Runde:

| Bildschirm     | Inhalt                                                      |
| -------------- | ----------------------------------------------------------- |
| Bester Treffer | „0.03g daneben“, „Ziel 100.0g, 4.21s“ (sonst „noch keiner“) |
| Schnellste     | „3.87s“, „1.00g daneben“ (sonst „noch keine“)               |
| Runden         | „42 Runden“, ggf. „5 Siege, 12 Duelle“                      |
| Stufen         | „Perfekt 3“, „Not Bad 7“, „Ganz ok 15“                      |
| Letzte Runden  | bis zu 4 Abweichungen, neueste zuerst (nur mit Verlauf)     |
| Ziel           | wie immer, mit Statusleiste                                 |

Die Statistik-Bildschirme zeigen keine Statusleiste. Glas, Taster, Tara oder
Reset bringen sofort das Ziel zurück, die Wartezeit beginnt neu. Den Deep-Sleep
hält die Rotation nicht auf.

## Energie

### Deep-Sleep

`power::SleepPolicy`: nach `sleepTimeout` Minuten ohne Aktivität, **in jedem
Zustand** (auch mit stehendem Ergebnis). Aktivität ist jede Tasterflanke und
jede Gewichtsänderung ≥ 2 g (geprüft alle 2 s). Blocker, die den Timer laufend
zurücksetzen (die Weboberfläche läuft nur mit AP und hält die Waage damit wach):

- Config-AP läuft,
- eigene Duell-Runde ist offen (noch nicht final),
- Taster gedrückt,
- Kalibrierung läuft,
- AP-Neustart, Reboot oder OTA stehen an.

Reihenfolge beim Einschlafen: Funk aus (Runde verlassen, Abmeldung senden) →
Display aus → HX711 aus + CLK-Hold → GPIO-Wake-up auf den Taster →
`esp_deep_sleep_start()`.

Nach dem Aufwachen folgt der Funk dem Modus: im Duell-Modus sofort wieder an,
sonst aus. Der AP bleibt aus.

### Config-AP

Geht nach `wifiTimeout` Minuten ohne Web-Anfrage aus (Hinweis „AP aus“). Im
Duell-Modus läuft der Funk weiter, sonst geht er mit aus. Ein Zeitstempel,
der nach dem Loop-Zeitpunkt liegt, gilt als frische Aktivität.

## Akku

- Messung mit `analogReadMilliVolts` (Werkskalibrierung des ADC, 11 dB),
  16 Samples, getrimmtes Mittel, alle 5 s. Nach dem Ein- oder Ausschalten des
  Funks wird 1 s nicht gemessen (Spannungseinbruch).
- Akkuspannung = Pin-Spannung × `battDividerRatio`.
- Prozent aus einer Li-Ion-Ruhespannungskurve (3,35 V = 0 %, 4,20 V = 100 %),
  geglättet (EMA 0,2). Die Anzeige fällt erst, wenn zwei Messungen in Folge
  mindestens 2 Punkte tiefer liegen, und steigt erst ab 5 Punkten (Laden).
  0 % und 100 % werden direkt übernommen.
- **Warnung** unter 10 %, aus ab 13 %. Keine Abschaltung in Software (die
  Schutzschaltung übernimmt das).
- **Abgleich** im Admin-Bereich: Spannung mit dem Multimeter an den Akkupolen
  messen und eintragen; der Teiler wird daraus berechnet (Bereich 1–6) und
  gespeichert. „Zurücksetzen“ stellt 2,0 wieder her.
- **Ohne Akku** (`batteryPresent` aus, Admin „Akku vorhanden“): kein ADC-Zugriff,
  kein Akkusymbol und keine Warnung, `battery` im Status ist `null`, Abgleich
  liefert 503. Wirkt sofort ohne Neustart. Deep-Sleep bleibt davon unberührt
  (für Netzbetrieb ggf. „Deep-Sleep 0 = nie“ setzen).

## Funk und AP

| Aktion   | Ablauf                                                                                                |
| -------- | ----------------------------------------------------------------------------------------------------- |
| Funk an  | STA-Modus ohne Verbindung, Modem-Sleep aus, 8,5 dBm, Kanal 1, ESP-NOW                                 |
| AP an    | Funk an + Soft-AP (offen, Kanal 1, max. 4 Clients), DNS-Captive-Portal, mDNS `waage.local`, Webserver |
| AP aus   | Webserver und AP aus; Funk bleibt nur im Duell-Modus                                                  |
| Funk aus | Runde verlassen und Abmeldung senden, dann WiFi aus                                                   |

AP-Neustart (SSID geändert) und Reboot (nach OTA) werden nur vorgemerkt und im
Loop nach der HTTP-Antwort ausgeführt. Nach einem OTA-Reboot startet der AP
einmalig automatisch (Merker im RTC-Speicher), damit die Seite neu laden kann.

## Weboberfläche

### Seiten

| Seite    | Inhalt                                                                                          |
| -------- | ----------------------------------------------------------------------------------------------- |
| `/`      | Live-Status (fragt `/api/status` jede Sekunde ab), Modus, Ziel, Zufall, Rotation                |
| `/login` | Passwort                                                                                        |
| `/admin` | Einstellungen, Akku-Abgleich, Kalibrier-Assistent, Firmware-Update mit Fortschritt, Duell-Debug |

Seiten sind statisch (PROGMEM), Daten kommen per JSON. Formulare senden nur
geänderte Felder.

### Sicherheit

- Anmeldung erzeugt ein 128-bit-Zufallstoken (`esp_random`) als Cookie
  `waage_session` (`HttpOnly; SameSite=Strict`), Vergleich in konstanter Zeit.
  Abmelden oder Passwortwechsel machen es ungültig.
- Login-Bremse: nach 5 Fehlversuchen 30 s gesperrt.
- Der AP ist offen; das Admin-Passwort schützt nur die Einstellungen.

### API

Vollständige Beschreibung mit allen Feldern: Kommentar in `waage/web.h`.
Anfragen `application/x-www-form-urlencoded`, Antworten JSON mit
`Cache-Control: no-store`, Fehler `{"ok":false,"error":"…","field":"…"}`.

| Route                                                              | Zweck                                                                                         |
| ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------- |
| `GET /api/status`                                                  | Gewicht, Modus, Phase, busy, Ziel, Funk/AP, Akku (Prozent, Spannung, Pin-mV, Teiler, Warnung) |
| `GET/POST /api/config`                                             | Ziel, Zufall, Rotation, Modus, Info-Rotation (Moduswechsel während Spiel → 409)               |
| `GET /api/stats`                                                   | Statistik: Zähler, bester Treffer, schnellste Zeit, Duelle, letzte Runden                     |
| `POST /api/admin/stats/reset`                                      | Statistik zurücksetzen                                                                        |
| `GET/POST /api/admin/config`                                       | SSID, Toleranz, Auto-Reset, Timeouts, Auto-Zero, Passwort                                     |
| `POST /api/admin/battcal`                                          | `measuredV=<V>` oder `reset=1`                                                                |
| `POST /api/admin/cal/{start,measure,cancel}`, `GET /api/admin/cal` | Kalibrier-Assistent                                                                           |
| `GET /api/admin/update/allowed`, `POST /api/admin/update`          | Firmware-Update                                                                               |
| `GET /api/admin/duell`                                             | Duell-Debug (Gegner, Rundentabelle)                                                           |

### Firmware-Update

Nur außerhalb von Spiel, Runde und Kalibrierung (sonst 409). Ein abgebrochener
Upload ruft `Update.abort()` auf, der nächste Versuch funktioniert ohne
Neustart. Reißt die Verbindung erst nach dem erfolgreichen Abschluss ab, gilt
das Update als gelungen (das neue Image ist bereits aktiv). Nach Erfolg: Antwort senden, Runde verlassen, nach 1 s Neustart, AP
danach wieder an. Während des Uploads blockiert der Loop (10–30 s).

## Build und Version

- Profil `c3` in `waage/sketch.yaml` mit festen Versionen: ESP32-Core 3.3.12,
  Adafruit SSD1306 2.5.17, Adafruit GFX 1.12.6, Adafruit BusIO 1.17.4,
  HX711 (Rob Tillaart) 0.6.5.
- `tools/gen_version.sh` schreibt `waage/version_gen.h` aus `git describe`
  (nicht eingecheckt); ohne die Datei heißt die Version `dev-<Datum>`. Die
  Version erscheint beim Start auf dem Display, im Seriellen Log, im Web und im
  Duell-Debug.
- `compile.sh` baut im Docker-Image (arduino-cli 1.3.1, per sha256 geprüft);
  das Image-Tag hängt vom Inhalt von `Dockerfile` und `sketch.yaml` ab.
- Flash-Belegung derzeit ca. 92 % der OTA-Partition (Standard-Partitionstabelle).

## Tests

`./test/run.sh` baut jeden Test mit `g++ -std=c++17 -Wall -Wextra -Werror` und
führt ihn aus; `SANITIZE=1` zusätzlich mit AddressSanitizer und UBSan.

| Test                                   | Prüft                                                                           |
| -------------------------------------- | ------------------------------------------------------------------------------- |
| `duell_core_test`, `duell_sim_test`    | Protokoll, Merge, Ranking; 2–4 Waagen mit Paketverlust, Ausstieg                |
| `config_core_test`                     | Sanitize, Validierung, Parser, altes EEPROM-Abbild bit-genau                    |
| `scale_core_test`                      | Filter, Stabilität, Tara, Nullung, Kalibrierung inkl. Fehlerpfade, Zeitüberlauf |
| `button_core_test`                     | Prellen, Zonengrenzen, Weck-Druck                                               |
| `battery_core_test`, `power_core_test` | Kurve, Hysterese, Abgleich; Sleep-Policy, AP-Timeout, Zeitüberlauf              |
| `text_core_test`                       | CP437, Layout, alle Texte ohne Ersatzzeichen                                    |
| `web_core_test`                        | JSON-Escaping, Cookies, Token, Login-Bremse                                     |
| `game_core_test`, `game_duel_sim_test` | Solo- und Duell-Regeln mit Gewichtsskripten, mehrere Waagen                     |
| `stats_core_test`                      | Stufen, Bestwert, schnellste Zeit, Erfolge, Duell, Verlauf                      |
| `ui_model_test`                        | Ebenen, Symbole, Texte, Erfolg im Ergebnis, Info-Rotation                       |

## Abnahme auf der Hardware

Vor dem Merge mit mindestens zwei Waagen:

- [ ] Erster Start nach dem Flashen: Einstellungen und Kalibrierfaktor übernommen
      (bekanntes Gewicht vorher und nachher wiegen).
- [ ] Kurzdruck mit Glas tariert; Glas weg → nach 1 s Nullung (NegZero).
- [ ] Solo: gutes Ergebnis bleibt beim Abheben, schlechtes verschwindet.
- [ ] Haltebalken: Texte bei 0,3 / 0,75 / 2 / 3,25 s, Loslassen in jeder Zone;
      Modus-Punkte im Balken und im Hinweis, Zyklus Game → Duell → Standard.
- [ ] Umlaute auf dem Display („Schüchtern“, Trinksprüche).
- [ ] Deep-Sleep aus stehendem Ergebnis; Schlafstrom (HX711 aus); Aufwachen im
      Duell-Modus mit Funk an, im Game-Modus ohne.
- [ ] HX711 abgezogen → „Sensorfehler“, wieder angesteckt → Waage läuft.
- [ ] Akku: Multimeter gegen `/api/status` vor und nach dem Abgleich; Warnsymbol
      bei < 10 %.
- [ ] OTA: Erfolg, Abbruch mit erneutem Versuch, 409 während einer Runde.
- [ ] SSID im Admin ändern → AP startet neu, Seite unter neuem Namen erreichbar.
- [ ] Duell mit 2–3 Waagen: Start, vorläufiger und finaler Rang auf allen gleich;
      gut bleibt / schlecht geht nach Final + 3 s.
- [ ] Duell: Moduswechsel mitten in der Runde → andere Waage sieht sofort
      „aufgegeben“.
- [ ] Game-Modus mit AP an: Waage taucht bei anderen nicht als Gegner auf.
- [ ] Duell-Modus: AP aus → `Vs n` bleibt, Duell läuft weiter.
- [ ] Statistik: „Neuer Rekord!“ im Ergebnis-Wechsel; nach 20 s ohne Glas
      Statistik je 3 s, Ziel 6 s, Zeiten im Admin änderbar; Glas/Taster
      bringt sofort das Ziel; Werte nach
      Deep-Sleep noch da, Verlauf leer; Zurücksetzen im Admin.
