# WDT – Zündsteuerung `My_Spark_1_2026.c`

Einzylinder-Zündsteuerung für **PIC16F1827** (HI-TECH C / XC8), 8 MHz interner
Oszillator. Ein OT-Impuls je Umdrehung, eine Zündspule, Parametrierung über
eine 9600-8N1-Schnittstelle mit Ablage im EEPROM.

Dieses Dokument beschreibt den Stand nach der vollständigen Reparatur der
Zündplanung (Nachfolger von PR #6).

---

## 1. Hardware und Signale

| Signal | Pin | Bedeutung |
|---|---|---|
| `SENSOR` | RB4 | OT-Geber, **fallende** Flanke = OT, ein Impuls je Umdrehung (Interrupt-on-Change) |
| `COIL` | LATA4 | Zündendstufe. `1` = Spule lädt, **fallende Flanke erzeugt den Funken** |
| `LED_1` | LATA0 | Quittung eines gültigen Parameterrahmens |
| `LED_2` | LATA1 | leuchtet während der Ladung |
| `DREHZ` | LATB3 | Drehzahlausgang (folgt der Ladung) |
| `ROT`/`GRUEN`/`BLAU` | LATB5/6/7 | Statusanzeige aus `WERT_1..3` |
| `RX` | RB1 | serieller Empfang, 9600 8N1 |

Timer1 läuft frei mit `FOSC/4` und Prescaler **1:8**:
`T1CON = 0b00110001`. Bei 8 MHz ergibt das **4 µs je Tick**.

> Der bisherige Wert `0b00010001` wählte 1:2 (1 µs/Tick), während die gesamte
> Arithmetik 4 µs voraussetzte. Registerbelegung geprüft am Datenblatt
> PIC16(L)F1826/27 (DS41391): `TMR1CS<1:0>` = Bit 7..6 (`00` = FOSC/4),
> `T1CKPS<1:0>` = Bit 5..4 (`11` = 1:8), `TMR1ON` = Bit 0.
> Ebenfalls geprüft: **`INTCONbits.IOCIF` ist nur lesbar** und folgt den
> `IOCBF`-Bits. Die Firmware löscht deshalb ausschließlich `IOCBFbits.IOCBF4`.

CCP1 und CCP2 arbeiten im Compare-Modus mit Software-Interrupt
(`CCPxCON = 0b00001010`):

* **CCP2** = Ladebeginn (`COIL = 1`)
* **CCP1** = Funkenzeitpunkt (`COIL = 0`)

---

## 2. Zeitbasis und abgeleitete Konstanten

| Konstante | Wert | Herleitung |
|---|---:|---|
| Tick | 4 µs | 8 MHz / 4 = 2 MHz, Prescaler 1:8 → 250 kHz |
| 16-Bit-Umlauf | 262,144 ms | 65 536 · 4 µs |
| 32-Bit-Umlauf | ≈ 4,77 h | 2³² · 4 µs |
| `TICKS_PRO_MINUTE` | 15 000 000 | 60 s / 4 µs → `DREHZAHL = 15000000 / Periode` |
| `SENSOR_TIMEOUT_TICKS` | 500 000 | **2,000 s** ohne OT-Flanke |
| `RX_TIMEOUT_TICKS` | 2 000 | **8 ms** Rahmen-Timeout (ein Byte dauert 1,04 ms) |
| `PERIODE_MIN` | 375 | 1,5 ms ≙ 40 000 1/min, darunter Störimpuls |
| `PERIODE_MAX` | 500 000 | identisch mit dem Sensor-Timeout |
| `MIN_VORLAUF` | 8 | 32 µs, darunter wird kein Compare mehr armiert |
| `PLAN_LATENZ` | 25 | 100 µs Reserve für die Planung im Hauptprogramm |
| `LADUNG_MAX` | 1 000 | 4 ms harte Obergrenze der Ladezeit |
| `LADUNG_RESERVE` | 250 | 1 ms zusätzlich bis zur Notabschaltung |
| `KORREKTUR_MAX` | 125 | 500 µs Grenze der OT-Nachkorrektur |
| `QUITTUNG_TICKS` | 50 000 | 200 ms LED-Quittung, nichtblockierend |

**Ladezeittabelle** (`LADEZEIT` → `ZEIT` in Ticks):

| `LADEZEIT` | 1 | 2 | 3 | 4 | 5 |
|---|---:|---:|---:|---:|---:|
| Dauer | 1,0 ms | 1,5 ms | 2,0 ms | 2,5 ms | 3,0 ms |
| Ticks | 250 | 375 | 500 | 625 | 750 |

Referenzrechnung: OT-Impuls alle 60 ms → Periode = **15 000 Ticks** →
`15 000 000 / 15 000` = **1 000 1/min**.

> `TIMER_UEBERLAEUFE_MAX = 120` aus dem Altstand war keine Zeitmessung:
> Der Timer läuft frei, der erste Überlauf hängt von seiner Phase ab, und
> 120 Umläufe wären ≈ 31 s. Der Sensor-Timeout vergleicht jetzt echte
> 32-Bit-Zeitstempel (`jetzt - LETZTE_KANTE > 500 000`).

---

## 3. Zeitbasis-Architektur

### 3.1 Kohärenter 32-Bit-Zeitstempel

`Zeit32()` liest `TMR1H`/`TMR1L`/`TMR1H`, wiederholt bei Rollover des
Low-Bytes und addiert einen **noch nicht bearbeiteten** Überlauf (`TMR1IF`),
sofern der gelesene Stand bereits zur neuen Runde gehört. Die oberen 16 Bit
stehen in `timer1_hoch` und werden ausschließlich in der ISR erhöht.

`ZeitJetzt()` ist die Fassung für das Hauptprogramm: GIE sichern, sperren
(`do { GIE = 0; } while (GIE);` gegen die bekannte Race beim Löschen),
`Zeit32()` lesen, GIE nur wiederherstellen, wenn es vorher gesetzt war.

### 3.2 32-Bit-Termine auf 16-Bit-Compare

Die Compare-Register erhalten nur die **unteren 16 Bit** des Termins. Ein
Treffer kann deshalb einen oder mehrere Timer-Umläufe zu früh auftreten.
Jede Compare-ISR prüft daher mit `(s32)(jetzt - termin) >= 0`, ob der Termin
tatsächlich erreicht ist; ist er es nicht, bleibt der Compare armiert und es
passiert nichts. Damit sind Perioden über mehrere 16-Bit-Umläufe
(z. B. 100 1/min = 150 000 Ticks) und der 32-Bit-Umlauf abgedeckt.

`ArmiereFunke()`/`ArmiereLadung()` setzen Termin, Compare-Register,
Interrupt-Flag (`CCPxIF = 0`) und Interrupt-Freigabe (`CCPxIE = 1`) als
Einheit. `EntwaffneFunke()`/`EntwaffneLadung()` löschen Freigabe, Flag und
Zustandsbit; ein stehengebliebener Compare kann so keinen Funken auslösen.

---

## 4. Ereignisverwaltung

Der Altstand benutzte eine gemeinsame Variable `EREIGNIS` für „OT liegt an“
und für den Lade-/Comparezustand. Ein Compare-Interrupt konnte damit ein
OT-Ereignis löschen und umgekehrt. Beides ist jetzt getrennt:

| Zelle | Schreiber | Bedeutung |
|---|---|---|
| `OT_ZEIT`, `OT_NEU` | nur IOC-ISR | veröffentlichte Flanke (Zeitstempel + Flag) |
| `OT_VERLOREN` | IOC-ISR | Flanke kam vor der Verarbeitung der vorigen |
| `LADUNG_AKTIV` | ISR/Planer | Spule führt Strom |
| `FUNKE_ARMIERT`, `FUNKENZEITPUNKT` | ISR/Planer | armierter Abschalttermin (CCP1) |
| `LADUNG_ARMIERT`, `LADUNGSBEGINN` | ISR/Planer | armierter Ladetermin (CCP2) |
| `PLAN_FUNKE`, `PLAN_VORAUS` | Planer | vorgemerkter Abschalttermin für die nächste Ladung |

Alle von der ISR geschriebenen Zellen sind `volatile`. Mehrbytige Werte
werden im Hauptprogramm nur als **Schnappschuss** unter kurzer Sperre
gelesen bzw. geschrieben (`kante = OT_ZEIT; OT_NEU = 0;`). Ein bloßes
Abschalten von `IOCIE` genügt nicht, weil die ISR auch aus anderer Quelle
laufen und `IOCIF`/`IOCBF` auswerten kann.

**Feste Reihenfolge in der ISR** (deterministische Priorität bei
gleichzeitigen Ereignissen):

1. `TMR1IF` – Überlauf, `timer1_hoch++` (muss zuerst laufen, sonst ist jeder
   nachfolgende Zeitstempel um einen Umlauf falsch)
2. `IOCBF4` – OT-Flanke erfassen und veröffentlichen (nur Zeitstempel, keine
   Rechnung)
3. `CCP1IF` – Funke: `COIL = 0`
4. `CCP2IF` – Ladebeginn: **erst** CCP1 aus `PLAN_FUNKE` armieren, **dann**
   `COIL = 1`

In der ISR wird nicht dividiert und nicht geplant; Periodenmessung,
Division und Terminberechnung laufen im Hauptprogramm ohne gesperrte
Interrupts.

---

## 5. Zustandsübergänge des Zündablaufs

```
Reset
  |
  v
[Messphase]   erste Flanke = Referenz (KANTE_GUELTIG = 1, keine Periode)
  |           ab der zweiten Flanke: Periode, DREHZAHL, Vorhersage
  |           OT_1 zaehlt bis 5
  v
[Startphase]  genau 5 Startfunken (STARTFUNKE 0..4),
  |           START_WINKEL Grad NACH OT
  v
[Normalbetrieb] Funke VOR OT (Vorzuendung), Begrenzer bei DREHZ_MAX
  |
  |  keine Flanke > 2 s
  v
[Sensorverlust] KANTE_GUELTIG = 0, DREHZAHL = 0, OT_1 = 0, STARTFUNKE = 0,
                Ladetermin entwaffnet  ->  zurueck zur Messphase
```

* **Genau fünf Messkanten**, **genau fünf Startfunken**. Der Altstand plante
  mit `STARTFUNKE < 6` zusammen mit dem ersten Funken sechs Startzyklen.
* Ein **Störimpuls** (Periode außerhalb `PERIODE_MIN..PERIODE_MAX`) wird
  verworfen, **ohne** die Referenz zu zerstören; die nächste Flanke misst
  wieder gegen die zuletzt angenommene Kante.

---

## 6. Terminplanung

### 6.1 Vorhersage der nächsten Periode

```
trend      = periode - periode_vorher          (auf +/- 25 % begrenzt)
vorhersage = periode + trend
```

Ohne diese Fortschreibung liegen bei Beschleunigung **alle** Termine
systematisch hinter der tatsächlichen Kante; im Test mit −12,5 % Periode je
Umdrehung fiel dadurch jeder zweite Funke aus. `DREHZAHL` und der Vorwinkel
werden weiterhin aus der **echten** Messung gebildet, nicht aus der
Vorhersage.

### 6.2 Startphase

`START_WINKEL` ist der Winkel des **Funkens** nach OT (im Altstand war es der
Ladebeginn, also effektiv `START_WINKEL` + Ladezeit).

* `ziel >= ZEIT + PLAN_LATENZ`: Der Funke derselben Umdrehung ist noch mit
  voller Ladezeit erreichbar → Ladebeginn `kante + ziel − ZEIT`,
  Funke `kante + ziel`.
* Sonst (z. B. `START_WINKEL = 0`): Ein Funke mit voller Ladezeit ist am
  bereits vergangenen OT **nicht nachträglich erzeugbar**. Die Firmware
  erzeugt deshalb keinen willkürlich späten Funken, sondern sagt die nächste
  Umdrehung vorher (`PLAN_VORAUS = 1`), lädt **vor** deren OT und zündet
  punktgenau bei `OT + START_WINKEL`. Der erste Startfunke erscheint dadurch
  eine Umdrehung später (nach der 6. statt nach der 5. Kante); die Anzahl
  bleibt exakt fünf.

Beispiel `periode = 15 000`, `LADEZEIT = 3` (`ZEIT = 500`):

| `START_WINKEL` | `ziel` | Modus | Ladebeginn | Funke |
|---:|---:|---|---|---|
| 0 | 0 | vorhersagend | OT<sub>n+1</sub> − 500 | OT<sub>n+1</sub> |
| 10 | 417 | vorhersagend | OT<sub>n+1</sub> − 83 | OT<sub>n+1</sub> + 417 |
| 20 | 833 | selbe Umdrehung | OT<sub>n</sub> + 333 | OT<sub>n</sub> + 833 |

### 6.3 Normalbetrieb

```
DREHZAHL  = 15 000 000 / periode                     (echte Messung)
SOLLWINKEL = DREHZAHL < DREHZ_W ? DREHZAHL * VOR_W / DREHZ_W : VOR_W
ziel       = vorhersage * SOLLWINKEL / 360
Funke      = kante + vorhersage - ziel               (VOR dem naechsten OT)
Ladebeginn = Funke - ZEIT
```

Die Ladung beginnt damit echt **vor** dem OT (prädiktiv). Der Altstand lud
dagegen sofort und zündete bei `kante + ziel`, wodurch aus einer Vorzündung
eine Spätzündung wurde.

**Entfallene Klemmung:** Der Altstand erzwang „Funke nie näher am OT als die
Ladezeit“ (`ziel < ZEIT → ziel = ZEIT`). Bei 1 000 1/min und `ZEIT = 500`
verfälschte das jeden Vorwinkel unter 12°: aus 5° wurden effektiv 12°. Weil
jetzt vor dem OT geladen wird, ist die Klemmung überflüssig und entfällt –
der eingestellte Vorwinkel gilt unverfälscht.

### 6.4 Korrektur, Auslassen, Grenzen

* **OT-Nachkorrektur:** Trifft die echte Flanke ein, während die Spule
  bereits lädt, wird der Abschalttermin um höchstens `KORREKTUR_MAX`
  (500 µs) verschoben, nie über `LADUNG_MAX` hinaus verlängert und nie in
  die Vergangenheit gelegt.
* **Vorausgeplanter Termin:** Ein noch nicht begonnener Ladevorgang mit
  `PLAN_VORAUS = 1` gehört zur jetzt beginnenden Umdrehung und wird von der
  neuen Kante **nicht** überschrieben. Ein Termin der beendeten Umdrehung
  ist dagegen veraltet und wird ersetzt.
* **Sofortladung:** Liegt der Ladebeginn bereits zurück, wird nur geladen,
  wenn mindestens **75 %** der Ladezeit übrig bleiben und `LADUNG_MAX` nicht
  überschritten wird. Sonst wird der Zyklus bewusst ausgelassen
  (`FEHLTERMIN++`) – definierte Fehlerreaktion statt Schwachfunke.
* **Notabschaltung:** Überschreitet eine laufende Ladung
  `LADUNG_MAX + LADUNG_RESERVE` (5 ms), schaltet das Hauptprogramm die Spule
  ab. Zusätzlich ist CCP1 **immer** armiert, **bevor** `COIL = 1` gesetzt
  wird; ein blockiertes Hauptprogramm kann die Spule daher nicht dauerhaft
  bestromen.
* **Begrenzer (`DREHZ_MAX`):** keine neue Ladung mehr; der Ladetermin wird
  entwaffnet.
* **Sensorverlust:** keine neue Ladung; Betrieb zurückgesetzt.

> **Physik:** Das Abschalten einer geladenen Spule erzeugt **immer** einen
> Funken. Eine bereits laufende Ladung wird deshalb bei Begrenzer oder
> Sensorausfall nicht „funkenfrei“ abgebrochen, sondern behält ihren
> begrenzten Abschalttermin. Ein funkenfreies Abschalten wird nirgends
> behauptet.

---

## 7. Serielle Parametrierung und EEPROM

Protokoll unverändert: **9600 8N1**, Rahmen `'S'` + 10 Parameterbytes.

| Byte | Parameter | Bereich | Ersatzwert |
|---|---|---|---|
| 1 | `START_WINKEL` | 0..20 | 10 |
| 2 | `VOR_W` | 0..40 | 40 (Klemmung) |
| 3 | `LADEZEIT` | 1..5 | 3 |
| 4/5 | `DREHZ_W` (big endian) | 2000..6000 | 4000 |
| 6/7 | `DREHZ_MAX` (big endian) | 4000..12000 | 10000 |
| 8/9/10 | `WERT_1..3` | 0..30 | 4 / 8 / 12 |

EEPROM-Layout unverändert: Adresse 1..10 in derselben Reihenfolge.

Korrigiert wurden:

* **Zeitstempel:** `EmpfangsDienst()` benutzte nur `TMR1L`; der Rahmen-Timeout
  war dadurch auf 262 ms-Fenster bezogen und nicht umlaufsicher. Jetzt
  32-Bit-Zeitstempel und Vergleich über `(u32)(jetzt - RAHMEN_ZEIT)`.
* **`FERR`:** wird **vor** dem Lesen von `RCREG` ausgewertet, weil das Lesen
  das nächste Byte samt neuem Fehlerbit nachlädt.
* **`OERR`:** wird erkannt und der Empfang über `CREN = 0; CREN = 1;`
  neu gestartet; der angefangene Rahmen wird verworfen.
* **Parameterprüfung vor der Ablage:** Der Altstand rief `PruefeParameter()`
  **vor** dem Übernehmen der empfangenen Bytes auf und prüfte damit die alten
  RAM-Werte; ungeprüfte Bytes gelangten ins EEPROM. Jetzt: Bytes übernehmen →
  prüfen/klemmen → **geprüfte** Werte ablegen.
* **Zweiter Rahmen während einer laufenden Schreibung:** Der jüngste Rahmen
  wird vollständig in `EE_WARTE` zwischengelegt (Latest-Frame-Politik) und
  erst nach Abschluss des laufenden 10-Byte-Auftrags geschrieben. Ein
  gemischter Parametersatz kann nicht entstehen.
* **Quittung:** Das blockierende `Blink()` (mehrere hundert ms `__delay_ms`)
  entfällt im Laufbetrieb. Die LED-Quittung ist nichtblockierend
  (`QUITTUNG_TICKS`, 200 ms); die Zündung läuft währenddessen weiter.
* **EEPROM-Schreiben:** korrekte Entsperrsequenz (`0x55`/`0xAA`), GIE wird
  gesichert und wiederhergestellt; je Hauptschleifendurchlauf wird höchstens
  ein Byte verarbeitet (begrenzte Arbeit).
* Neue Parameter verändern eine **bereits laufende Ladung nicht**; sie wirken
  ab der nächsten Planung.

---

## 8. Tests

### 8.1 Bauen und ausführen

```bash
gcc -std=c99 -Wall -Wextra -DHOST_TEST -Itests \
    -o /tmp/test_ignition \
    My_Spark_1_2026.c tests/host_pic_shim.c tests/sim_main.c \
    tests/test_ignition_timing.c
/tmp/test_ignition
```

Ergebnis des letzten Laufs: **`ALLE TESTS BESTANDEN`** (24 Testgruppen,
Übersetzung ohne Warnungen mit `-Wall -Wextra`).

### 8.2 Was der Host-Shim modelliert

`tests/host_pic_shim.[ch]` bildet nach:

* **Zieltypbreiten** (`u8`/`u16`/`u32`/`s32` mit fester Breite), damit die
  Tests dieselbe Arithmetik und dieselben Überläufe sehen wie HI-TECH C – der
  Host hat sonst 64-Bit-`long` und 32-Bit-`int`.
* **Timer-Takt und Prescaler** aus `T1CON` (falscher Prescaler ⇒ falsche
  Zeiten ⇒ Testfehler).
* **Compare-Hardware:** Treffer nur bei tatsächlicher 16-Bit-Übereinstimmung
  im durchlaufenen Intervall; `CCPxIF` wird unabhängig von `CCPxIE` gesetzt
  (stehengebliebene Flags werden dadurch sichtbar).
* **`TMR1IF`** beim Überlauf, `IOCIF` **abgeleitet** aus `IOCBF` (nur lesbar).
* **Kohärentes Lesen:** einstellbarer Versatz zwischen `TMR1H`- und
  `TMR1L`-Zugriff, um Rollover zwischen den Lesezugriffen zu erzwingen.
* **UART:** zweistufiger FIFO mit `FERR` je Byte, `OERR`, Nachladen bei
  `RCREG`-Lesen.
* **EEPROM:** Schreibdauer, Erkennung über `EECON1bits.WR`.

### 8.3 Abgedeckte Fälle

Reset/Initialisierung und Registerbild · erste Sensorflanke und Referenz ·
Periodenmessung 60 ms → 15 000 Ticks → 1 000 1/min · genau 5 Messkanten und
genau 5 Startfunken · `START_WINKEL` 0/10/20 · Ladezeit-Endpunkte 1..5 und
ungültige Werte · Normalbetrieb 1 000 1/min mit Vorzündung vor OT ·
6 000 1/min mit 3 ms (Basisfall, hier ist **keine** Vor-OT-Ladung nötig) ·
100 1/min über mehrere 16-Bit-Umläufe · 32-Bit-Umlauf im Betrieb ·
kohärentes Timerlesen mit Überlauf zwischen den Zugriffen · träge
Hauptschleife · Beschleunigung/Verzögerung ±14 % je Umdrehung · Störimpuls ·
Begrenzer und Rückfall · stehengebliebener Compare · Sensorverlust während
der Ladung und Neustart · gleichzeitige IOC/CCP1/CCP2/Überlauf-Ereignisse ·
UART-Rahmen im Lauf · nichtblockierende Quittung · zweiter Rahmen während
laufender EEPROM-Schreibung · UART-Dauerlast · `FERR`/`OERR`/Rahmen-Timeout ·
Parameterklemmung und Big-Endian-Ablage · `EESchreibe()` mit GIE gesetzt und
gelöscht.

---

## 9. Offene Punkte und Grenzen

* **Keine Zielübersetzung geprüft.** In der Testumgebung stand weder HI-TECH C
  noch XC8 zur Verfügung. Der Zielbuild ist damit **unverifiziert**;
  Codegröße, RAM-Bedarf und die Wirkung der 8-/16-Bit-Arithmetik des
  Zielcompilers wurden nicht gemessen.
* **Keine Hardwaremessung.** Sämtliche Ergebnisse stammen aus der
  Host-Simulation. Es wurde **nichts** an einem PIC, an einem Signalgenerator
  oder an einer Zündspule gemessen.
* **ISR-Laufzeiten unbekannt.** Die Simulation kennt keine Befehlszyklen. Ob
  die ISR bei hoher Drehzahl (z. B. 12 000 1/min = 1 250 Ticks je Umdrehung)
  rechtzeitig fertig wird, muss am Ziel gemessen werden. Die ISR enthält
  bewusst keine Division und keine Planung.
* **Trendvorhersage** ist eine lineare Fortschreibung mit ±25 %-Begrenzung.
  Bei sehr sprunghaften Drehzahländerungen kann ein Zyklus ausgelassen werden
  (`FEHLTERMIN` zählt mit); im Test mit ±14 % je Umdrehung fällt genau die
  erste beschleunigte Umdrehung aus, weil dort noch kein Trend vorliegt.
* **Semantikänderung** von `START_WINKEL` (Funken- statt Ladebeginnwinkel) und
  Wegfall der Ladezeit-Klemmung ändern das Verhalten gegenüber dem Altstand
  bewusst; siehe Abschnitt 6.
* Ein **funkenfreies** Abschalten einer geladenen Spule ist physikalisch nicht
  möglich und wird nirgends behauptet.

---

## 10. Prüfliste für den Bench-Test (ohne Zündendstufe)

> **Diese Reihenfolge einhalten. Die Zündendstufe und die Zündspule bleiben
> zunächst abgeklemmt.** Die Host-Simulation ist kein Nachweis der
> Betriebssicherheit.

1. **Endstufe und Zündspule abklemmen.** Nur der Logikausgang LATA4 wird
   gemessen. Kein Kraftstoff, kein Motorlauf.
2. Firmware mit HI-TECH C / XC8 übersetzen. Warnungen und Speicherbedarf
   protokollieren.
3. Versorgung anlegen, Oszilloskop auf LATA4 (Logikpegel) und RB4.
4. **Ohne Impuls:** LATA4 muss dauerhaft `0` sein.
5. Funktionsgenerator an RB4, saubere fallende Flanken, **60 ms** Abstand
   (1 000 1/min), Logikpegel, ggf. entprellt.
6. Erwartung: Nach der 5. Flanke beginnen Startfunken, insgesamt genau
   **fünf**, dann Normalbetrieb.
7. Nachmessen mit `START_WINKEL = 10`, `LADEZEIT = 3`:
   * Ladedauer (LATA4 high) = **2,00 ms ± Messtoleranz**
   * Startfunke: **1,67 ms nach** der OT-Flanke (417 Ticks)
   * Normalbetrieb: Funke **0,83 ms vor** der OT-Flanke (208 Ticks, 5°)
8. Periode auf 10 ms (6 000 1/min) ändern: Vorwinkel muss auf `VOR_W`
   stehen bleiben, Ladedauer unverändert.
9. Impuls abschalten: LATA4 muss spätestens nach der laufenden Ladung `0`
   sein, die Stillstandserkennung nach **≈ 2 s** greifen; danach beginnt der
   Ablauf wieder mit der Messphase.
10. Periode unter den Begrenzer legen (`DREHZ_MAX`) und darüber: oberhalb
    darf **keine neue Ladung** beginnen.
11. Parameterrahmen über die serielle Schnittstelle senden, während Impulse
    laufen: Die Funkenfolge darf **nicht** aussetzen, die Quittungs-LED darf
    nicht blockieren, das EEPROM muss die geklemmten Werte enthalten.
12. Erst wenn alle Punkte am Oszilloskop bestätigt sind, darf über einen
    Anschluss der Zündendstufe nachgedacht werden.

**Zielübersetzung und Oszilloskop-Nachweis sind Voraussetzung für jeden
Motorbetrieb.**
