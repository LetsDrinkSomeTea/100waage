# 100-Waage

Trinkspiel-Waage auf Basis eines ESP32-C3: Ziel ist, möglichst genau eine
vorgegebene Menge aus dem Glas zu trinken. Mehrere Waagen können per Funk
gegeneinander antreten (Duell). Technische Details stehen in [Specs.md](Specs.md).

## Hardware

| Komponente | Details                                                          |
| ---------- | ---------------------------------------------------------------- |
| MCU        | ESP32-C3 Super Mini                                              |
| Wägezelle  | HX711-Verstärker (10 Messungen/s, 80/s bei umgelötetem RATE-Pin) |
| Display    | SSD1306 OLED 128×32, I2C                                         |
| Taster     | GPIO 5 (HIGH = gedrückt, weckt aus dem Deep-Sleep)               |
| Akku       | Li-Ion mit Schutzschaltung, Spannungsteiler an GPIO 2            |

| Funktion        | GPIO    |
| --------------- | ------- |
| OLED SDA / SCL  | 8 / 9   |
| HX711 DAT / CLK | 21 / 20 |
| Taster          | 5       |
| Akku-ADC        | 2       |

## Bedienung

### Taster

| Aktion       | Wirkung                                                                                               |
| ------------ | ----------------------------------------------------------------------------------------------------- |
| Kurz drücken | Zurücksetzen **und tarieren** (immer, auch mit Glas auf der Waage)                                    |
| Halten       | Ab 0,3 s erscheint ein Balken mit Marken bei 3 s und 5 s; der Text zeigt, was beim Loslassen passiert |
| Halten 3–5 s | Modus wechseln (Game ↔ Standard)                                                                      |
| Halten 5–8 s | Funk: läuft der Config-AP → alles aus, sonst Funk + AP an                                             |
| Halten ≥ 8 s | Abbrechen, nichts passiert                                                                            |

### Spiel (Game-Modus)

1. Das Display zeigt das Ziel, z. B. `100.0g?` (🔀 = Zufallsziel).
2. Volles Glas aufstellen. Sobald es mindestens das Ziel wiegt und ruhig steht
   (0,5 s), erscheint `Bereit?` und ein Trinkspruch.
3. Glas abheben und trinken (Ladeanimation), dann zurückstellen.
4. Ergebnis: getrunkene Menge auf 0,01 g mit Bewertung, im Wechsel mit der Zeit.
   Die erste Nachkommastelle ist exakt, die zweite Glück.

| Abweichung vom Ziel | Bewertung  |
| ------------------- | ---------- |
| 0,00 g              | Perfekt!   |
| ≤ 0,10 g            | Not Bad!   |
| ≤ 1,00 g            | Ganz ok!   |
| zu wenig            | Schüchtern |
| zu viel             | Zu gierig! |

**Ergebnis stehen lassen:** Ein gutes Ergebnis (innerhalb des Auto-Reset-Bereichs,
Standard ±10 %) bleibt zum Prahlen stehen, bis jemand den Taster drückt. Ein
schlechtes verschwindet, wenn das Glas abgehoben wird; die Waage tariert dabei
die leere Waage.

Wurde mit Glas tariert und das Glas danach weggenommen, nullt sich die leere
Waage nach 1 s selbst.

### Duell

Voraussetzung: Funk an (5 s halten) und Game-Modus. Das Symbol `Vs n` zeigt,
wie viele andere Waagen sichtbar sind.

- Glas aufstellen → `Warte... 2/3 bereit`. Die Runde startet, sobald **alle**
  sichtbaren Waagen bereit sind. Das Ziel wird aus den Zielen der Teilnehmer
  gewürfelt und angezeigt.
- Nach dem eigenen Trinken erscheint sofort ein vorläufiger Platz (`~2. Platz`,
  im Wechsel mit `2/3 fertig`), final dann `2. Platz!`.
- Es gelten dieselben Regeln wie solo: gutes Ergebnis bleibt bis zum Taster,
  schlechtes verschwindet beim Abheben (frühestens 3 s nach dem Endergebnis).
- Eine Waage, die nicht bereit ist (z. B. noch ihr Ergebnis zeigt), hält den Start
  auf; nach 60 s Warten spielt man solo. Ein Wechsel auf Solo wird mit `Solo!`
  angezeigt.
- Alle Waagen müssen dieselbe Firmware haben (Protokoll v3, `0xD3`).

### Standard-Modus

Einfache Waage mit 0,1 g Anzeige. Nimmt nicht an Duellen teil.

### Symbole

| Symbol                  | Bedeutung                              |
| ----------------------- | -------------------------------------- |
| 🔀 oben links           | Zufallsziel aktiv                      |
| WLAN-Bogen              | Funk an                                |
| `AP`                    | Config-Access-Point läuft              |
| `Vs n`                  | n andere Duell-Waagen sichtbar         |
| Akku                    | Ladezustand (bei ausgeschaltetem Funk) |
| blinkender Akku mit `!` | Akku unter 10 %                        |

### Energie

- Nach `Deep-Sleep`-Minuten ohne Gewichtsänderung oder Tastendruck schläft die
  Waage, in jedem Zustand. Ausnahmen: der Config-AP läuft oder die eigene
  Duell-Runde ist noch nicht entschieden.
- Aufwecken mit dem Taster. War der Funk an, erscheint kurz `Funk aus`.
- Der Config-AP geht nach `AP-Auto-Aus` Minuten ohne Web-Zugriff aus, der
  Duell-Funk läuft weiter.

## Weboberfläche

1. 5 s halten, das Display zeigt den WLAN-Namen (`100-Waage-XXXX`, eindeutig pro Waage).
2. Mit dem Handy verbinden (offenes WLAN), die Seite öffnet sich als Captive
   Portal, sonst `http://192.168.4.1` bzw. `http://waage.local` aufrufen.

**Startseite:** Live-Status (Gewicht, Modus, Ziel, Akku), Einstellungen für
Modus, Zielgewicht, Zufallsziel und Display-Rotation. Änderungen gelten sofort
(Ziel und Zufall während einer laufenden Runde ab der nächsten).

**Admin** (Passwort, Standard `admin`): WLAN-Name, Toleranz, Auto-Reset-Bereich,
Timeouts, Auto-Zero, Passwort; alles ohne Neustart. Außerdem:

- **Akku-Abgleich:** Akkuspannung mit dem Multimeter an den Akkupolen messen
  (Funk an), eintragen, „Abgleichen“.
- **Waage kalibrieren:** Waage leeren → Start (tariert) → bekanntes Gewicht
  auflegen → Gewicht eintragen → „Messen“ → Gewicht entfernen.
- **Firmware-Update** (.bin), gesperrt während Spiel/Duell-Runde.
- **Duell-Debug:** sichtbare Waagen und Rundentabelle.

Beim ersten Start der neuen Firmware werden Einstellungen und Kalibrierung aus
dem alten Speicherformat übernommen.

## Build

Feste Versionen (ESP32-Core 3.3.12, Bibliotheken) stehen im Profil
`c3` in `waage/sketch.yaml`.

```bash
./tools/gen_version.sh                       # Firmware-Version aus git describe
arduino-cli compile --profile c3 waage
arduino-cli upload  --profile c3 -p /dev/ttyACM0 waage
```

Alternativ per Docker (ohne lokale Installation): `./compile.sh` →
`build/waage.ino.bin`.

## Tests

Die Logik (`waage/*_core.*`) ist ohne Arduino auf dem PC testbar, inklusive
Simulation mehrerer Waagen mit Paketverlust:

```bash
./test/run.sh                 # alle Host-Tests
SANITIZE=1 ./test/run.sh      # zusätzlich mit AddressSanitizer/UBSan
```

## Formatierung

C++ wird mit clang-format (`.clang-format`, LLVM-Stil, 80 Spalten) formatiert,
Markdown und YAML mit Prettier (`.prettierrc.yaml`). `tools/format.sh`
formatiert alles. Ein Pre-commit-Hook formatiert die gestagten Dateien:

```sh
git config core.hooksPath .githooks
```

## Lizenz

MIT
