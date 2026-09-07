# WDT – Zündsteuerung PIC16F1827

`My_Spark_1_2026.c` ist HI-TECH-C-Firmware für einen PIC16F1827 mit internem
8-MHz-Oszillator. Timer1 läuft mit Fosc/4 und 1:8-Vorteiler; ein Tick beträgt
damit 4 µs. Der EUSART verwendet die Standardbelegung (APFCON=0: RX RB1,
TX RB2) mit etwa 9600 Baud. RB4 bleibt der Sensor-Eingang, RA4 (`COIL`) der
Spulen-Ausgang und RB7 (`BLAU`) ist Ausgang.

## Annahmen und Verhalten

Die bestehende Konvention wird beibehalten: Die fallende Flanke des
RB4-Sensors liegt am OT und der Funke entsteht beim Abschalten (`COIL=0`).
Diese Sensorposition und die angeschlossene Treiberstufe sind **nicht**
hardwareseitig bestätigt. Die ersten fünf synchronisierten Impulse verwenden
den Startwinkel als Ladebeginn nach OT. Ab Impuls 5 wird – nach dem
Abschalten des noch ausstehenden Startfunkens – der Ladebeginn für den Funken
vor Impuls 6 aus der zu diesem Zeitpunkt gemessenen Periode geplant. Ein
Sensorimpuls überschreibt niemals eine laufende Ladung oder deren
Abschalttermin; bei Kollision, zu kurzer Periode, überholtem Compare-Termin
oder aktivem Begrenzer wird der betreffende Funken konservativ verworfen.

Die erste Periode nach einem Stillstand wird nur zur Synchronisation
verwendet. Nach einem Timer1-Überlauf (ca. 262 ms) verwirft die Firmware
ausstehende Ereignisse, setzt Drehzahl/Startzustand zurück und schaltet die
Spule aus. ISR und Hauptprogramm enthalten keine Warte-Schleifen für die
Zündung: Compare startet und beendet die Ladung als Zustandsmaschine.

## Untersuchter Aussetzer (Bezug zu PR #2)

Ausgangspunkt war eine vom Nutzer am Oszilloskop beobachtete Lücke in einem
periodischen Signal (CH2/türkis), während das andere Signal (CH1/gelb)
weiterlief; der Nutzer hat bestätigt, dass zu diesem Zeitpunkt der Stand aus
PR #2 (a5816aff3cbf1918c1dc0c664b16825252c03890) geflasht war. Die exakte
Kanalzuordnung (CH1=Sensor, CH2=Zündausgang) und die Zuordnung der im Bild
gezählten sechs Impulse zu einer bestimmten Funken-/Impulsnummer wurden vom
Nutzer **nicht** ausdrücklich bestätigt; aus der Bildzählung allein wird daher
keine Ursache behauptet.

**Befund im Code von PR #2:** `SensorEreignis()` setzte Timer1 bei **jedem**
Sensorimpuls zurück, bevor geprüft wurde, ob noch eine Ladung/Zündung
aussteht (`zuend_zustand != ZUEND_AUS`). Der zu diesem Zeitpunkt bereits in
`CCPR1H:CCPR1L` programmierte Compare-Termin ist ein **absoluter**
Timer1-Zählerstand. Der Kommentar im alten Code („Ein Abschalttermin wird
niemals durch ein Sensorereignis überschrieben“) war zutreffend – das
CCPR1-Register wurde tatsächlich nicht verändert – aber irreführend: Der
Timer1-**Reset** verändert die Zeitbasis, auf die sich dieser unveränderte
Termin bezieht. Trifft ein Sensorimpuls während einer laufenden Ladung
(`NORMAL_LADEN`/`NORMAL_ABSCHALTEN`, ebenso `START_LADEN`/`START_ABSCHALTEN`)
ein, wird Timer1 auf 0 zurückgesetzt, während CCPR1 stehen bleibt. Damit der
Compare noch auslöst, muss Timer1 erneut bis zu diesem (jetzt „falschen“)
Wert zählen – im ungünstigsten Fall bis zu einem vollen Timer1-Überlauf
(≈262 ms bei diesem Vorteiler). Praktisch bedeutet das: Die Spule bleibt weit
über die vorgesehene Ladezeit hinaus eingeschaltet („hängender Funke“) oder
der Abschalt-/Ladetermin wird so weit verzögert, dass ein Timer1-Überlauf
zuvor `Stillstand()` auslöst und die Zündung verwirft. Beides passt
qualitativ zu einem fehlenden Impuls mit anschließend verdoppeltem Abstand,
**ist aber keine bewiesene Erklärung für das konkrete Bild** – dafür fehlt
eine tatsächliche Hardwaremessung mit bekannter Kanalzuordnung und
Firmwareversion je Puls.

**Korrektur:** In `SensorEreignis()` wird die Prüfung auf eine laufende
Ladung/Zündung jetzt **vor** dem Timer1-Reset durchgeführt. Läuft noch ein
Vorgang, wird der Sensorimpuls weiterhin konservativ verworfen (wie schon in
PR #2 beabsichtigt), aber Timer1 und der laufende Compare-Termin bleiben
dabei unangetastet; zusätzlich wird ein zeitgleich gesetztes `TMR1IF` in
diesem Fall gelöscht, damit ein simultaner Sensorimpuls nicht fälschlich ein
Stillstand-Timeout auslöst. Die Start-/Normalübergabe (Startfunke 5 →
Vorbereitung des Funkens vor Impuls 6 über `NormalenFunkenPlanen()`, sowohl
direkt in der ISR nach Abschalten von Startfunke 5 als auch – konservativ
verworfen bei Kollision – beim folgenden Sensorimpuls) ist unverändert; sie
erzeugt weiterhin keinen zusätzlichen Funken und keine Doppelzündung.

## Tests

Automatisierte, hostseitige Tests befinden sich in `tests/` und binden den
**tatsächlichen Produktionscode** aus `My_Spark_1_2026.c` unverändert per
`#include` ein (Makro `HOST_TEST` schaltet nur die PIC-SFR-Header und den
`main()`-Rahmen gegen ein einfaches Register-/Zeitmodell aus, siehe
`tests/host_pic_shim.h` und `tests/sim_harness.h`); getestet wird also nicht
ein unabhängiges Verhaltensmodell, sondern dieselbe Timing-/Zustandslogik,
die auf dem Controller läuft.

```sh
gcc -std=c99 -Wall -Wextra -o /tmp/test_ignition tests/test_ignition_timing.c tests/host_pic_shim.c
/tmp/test_ignition

gcc -std=c99 -Wall -Wextra -o /tmp/test_regression tests/test_regression_pr2_bug.c tests/host_pic_shim.c
/tmp/test_regression
```

- `tests/test_ignition_timing.c` prüft mit dem **korrigierten** Code:
  Synchronisationsimpuls und Impulse 1–8 bei konstanter Periode (genau vier
  Startfunken sowie ein Normalfunke direkt nach Startfunke 5 und je einer vor
  jedem weiteren Impuls, keine Doppel-/Ausfallzündung, Ladezeiten exakt
  `ZEIT` Ticks – Pulsbreite, nicht nur Anzahl), Sensorflanken während
  wartendem Ladebeginn, während aktiver Ladung und gleichzeitig mit
  Compare-Abschalten, Timer1-Überlauf/Signalverlust, `CompareSetzen()`- und
  `TICKS_GUARD`-Grenzfälle, Drehzahlbegrenzer, Startwinkel 0/klein/20, ein zu
  kurzes Ladefenster sowie (neu) den Übergang vom letzten Startfunken zum
  Normalfunken im Detail: erfolgreiche Rettung durch den Rückfall in
  `NormalenFunkenPlanen()` bei spätem Idealtermin (Periode 1700) und der
  physisch unvermeidbare Einzelausfall, wenn selbst der Rückfall keinen
  Platz mehr hat (Periode 1500, siehe unten). Alle Fälle bestehen mit dem
  korrigierten Code.
- `tests/test_regression_pr2_bug.c` bindet zusätzlich eine wörtliche Kopie
  der **alten** (PR-#2-)Fassung von `SensorEreignis()`/`isr()` ein und
  reproduziert damit gezielt eine Sensorflanke während aktiver Ladung: Mit
  dem alten Codepfad bestätigt der Test alle drei erwarteten Symptome
  (Timer1-Reset trotz laufender Ladung, dadurch ungültig gewordener
  Abschalttermin, Spule schaltet nicht rechtzeitig ab). Dieselbe Prüfung mit
  dem korrigierten Code ist Teil von `test_ignition_timing.c` und besteht
  dort.

**Diese Hosttests ersetzen keine Zielübersetzung** mit HI-TECH C/XC8 für den
PIC16F1827 (im Repository ist kein Target-Compiler vorhanden) **und keine
Hardwaremessung**. Ob der im Oszilloskopbild konkret beobachtete Aussetzer
durch genau diesen Fehlerpfad verursacht wurde, ist damit **nicht
nachgewiesen** – nachgewiesen ist, dass dieser Fehlerpfad im Code von PR #2
existierte, zu den beobachteten Symptomen passen würde und durch die
Korrektur beseitigt ist.

## Nachgemeldeter Aussetzer: "Es fehlt noch der 6. Puls"

Nach der PR-#2-Korrektur wurde weiterhin ein fehlender Puls beim Übergang
vom Start- zum Normalbetrieb gemeldet. Mit dem hostseitigen Modell ließ sich
dafür folgender **reproduzierbarer, von der PR-#2-Korrektur unabhängiger**
Fehlerpfad in `NormalenFunkenPlanen()` bestätigen (Nutzerzählung: Sync =
Impuls 1, Startfunken = Impulse 2–5, der hier betroffene Übergangsfunke wäre
Impuls 6):

Der Funke unmittelbar nach Startfunke 5 wird direkt in der ISR beim
Abschalten dieses Startfunkens geplant (`isr()`, Zweig
`START_ABSCHALTEN`, ruft `NormalenFunkenPlanen()`). Diese Funktion berechnet
den winkelbasierten Ladebeginn relativ zum Referenzimpuls, bei dem Timer1
zuletzt auf 0 gesetzt wurde – **aber** zwischen diesem Referenzimpuls und dem
Aufruf ist bereits die volle Ladezeit des gerade beendeten Startfunkens
vergangen. Bei langer Ladezeit (`LADEZEIT` nahe 5, `ZEIT`≈750 Ticks) und
großem Vorwinkel (`VOR_W` nahe 40) kann der berechnete Ladebeginn dadurch
bereits in der Vergangenheit liegen; `CompareSetzen()` verwirft ihn dann
korrekt, aber der Rückgabewert wurde bislang ignoriert – der Funke fiel
ersatzlos aus, alle späteren Impulse liefen danach wieder korrekt (kein
dauerhafter Phasenfehler, aber ein permanent fehlender Einzelimpuls an genau
dieser Übergangsstelle).

**Konkrete, im Repository nachvollziehbare Reproduktion** (Periode 1500
Ticks ≙ 10000 U/min unterhalb `DREHZ_MAX=11000`, `START_WINKEL=20`,
`VOR_W=40`, `LADEZEIT=5`): Startfunke 5 schaltet bei Tick 833 ab, der ideale
Ladebeginn des Übergangsfunkens läge aber schon bei Tick 583 – innerhalb der
noch laufenden Ladung des Startfunkens. Dies ist eine gültige, aber
elektrisch extreme Einstellungskombination (siehe `EinstellungenGueltig()`);
ob sie dem konkreten, vom Nutzer beobachteten Bild entspricht, ist **nicht
bewiesen**.

**Korrektur:** `NormalenFunkenPlanen()` versucht jetzt, wenn der ideale
winkelbasierte Termin bereits abgelaufen ist, einen sicheren Rückfall: sofort
(mit `TICKS_GUARD` Abstand zum aktuellen Timer1-Stand) mit **voller**
Ladezeit `ZEIT` laden – aber nur, wenn diese volle Ladezeit dabei mit
`TICKS_GUARD`-Sicherheitsabstand vor dem für die aktuelle Periode
angenommenen nächsten Referenzimpuls endet. Das bedeutet:

- Die Ladezeit wird nie verkürzt und nie verlängert.
- Der Rückfall-Funke überlappt nie mit dem gerade beendeten Startfunken
  (Ladebeginn immer erst nach dessen Abschalten + `TICKS_GUARD`).
- Es wird nie nach dem angenommenen Referenzimpuls gezündet (nur ein
  *späterer*, nie ein früherer Zündwinkel als eingestellt).
- Passt selbst der sofortige Rückfall nicht mehr sicher vor den nächsten
  Referenzimpuls (wie im obigen Beispiel: Startfunke schaltet bereits bei
  833 Ticks ab, `TICKS_GUARD` + `ZEIT` + `TICKS_GUARD` = 1582 Ticks reichen
  bei 1500 Ticks Periode nicht mehr), bleibt dieser eine Übergangsfunke
  weiterhin bewusst aus – ein Zünden würde sonst entweder die Ladezeit
  verkürzen, mit dem Startfunken überlappen oder nach dem Referenzimpuls
  liegen, was alles explizit vermieden werden soll. Synchronisation, OT_1
  und alle nachfolgenden Impulse bleiben davon unberührt.

Andere, in der Aufgabenstellung genannte Kandidaten (Startwinkel 0, exakt
gleichzeitiger Sensor-/Compare-Impuls beim Übergang) wurden mit dem
Hostmodell zusätzlich untersucht, führten dort aber – anders als der
konkrete Ladezeit-/Vorwinkel-Fall oben – nicht zu einem Impulsausfall; ISR-
Rechenzeit/-Latenz kann das Hostmodell grundsätzlich nicht abbilden (siehe
unten).

`tests/test_ignition_timing.c` enthält für diesen Übergang zwei neue,
gezielte Testfälle: einen, bei dem der Rückfall den sonst ausfallenden
Impuls erfolgreich rettet (Periode 1700), und einen, bei dem selbst der
Rückfall keinen Platz mehr hat und der Impuls – nachweislich einmalig und
ohne Folgeschäden für spätere Impulse – ausfällt (Periode 1500, siehe oben).
Beide Fälle wurden zunächst gegen den unveränderten Code als fehlschlagend
verifiziert.

**Auch dies ersetzt keine Hardwaremessung.** Ob die tatsächliche Ursache des
vom Nutzer beobachteten Aussetzers exakt diesem Pfad entspricht, bleibt ohne
Messung mit bekannter Kanalzuordnung und Firmwareversion je Puls offen.

## 11-Byte-UART-Paket

Ein Paket beginnt mit `S` und wird vollständig gepuffert, validiert und erst
dann gemeinsam übernommen sowie im Stillstand nichtblockierend ins EEPROM
geschrieben:

| Byte | Inhalt |
| --- | --- |
| 1 | `'S'` |
| 2 | `START_WINKEL` (Grad, 0…20; 0 wird auf einen sicheren Mindesttermin gelegt) |
| 3 | `VOR_W` (Grad, 1…40) |
| 4 | `LADEZEIT` (ms, 1…5) |
| 5–6 | `DREHZ_W`, High- dann Low-Byte (U/min, 2000…6000) |
| 7–8 | `DREHZ_MAX`, High- dann Low-Byte (U/min, 4000…11000, mindestens `DREHZ_W`) |
| 9–11 | `WERT_1`, `WERT_2`, `WERT_3` (geordnete LED-Schwellen, je 500 U/min, 1…30) |

Ungültige, unvollständige, FERR- oder OERR-behaftete Pakete werden verworfen.
Der Paket-Timeout beträgt zwei Timer0-Überläufe, also etwa 65,5 ms. Die
EEPROM-Defaults sind konservativ: 10°, 40°, 3 ms, 4000 U/min, 11000 U/min
und LED-Schwellen 4/8/12.

## Prüfung

Im Repository gibt es weder ein HI-TECH-C-/XC8-Projekt noch einen installierten
Target-Compiler; daher wurde keine erfolgreiche PIC-Kompilierung oder
Hardwaremessung behauptet. Vor einem Motorlauf sind mindestens folgende
Prüfungen mit simuliertem Sensorsignal, Logikanalysator/Oszilloskop und **ohne
angeschlossene Zündspule bzw. ohne laufenden Motor** erforderlich:

1. Impulse 1…7, insbesondere Startfunken 5 und der vor Impuls 6 geplante
   Funken: begrenzte Ladezeit, keine Doppelzündung.
2. Begrenzung sowie `DREHZ_W=4000` als `0x0F, 0xA0`; Werte direkt nach UART
   und nach Neustart vergleichen.
3. Unvollständige/ungültige Pakete, Ladezeit 0/1/5/>5, Startwinkel
   0/klein/20 und Vorwinkel 0/klein/40.
4. Überholte Compare-Termine, Timer-Wrap, Signalverlust während Ladung und
   Wiederanlauf sowie gleichzeitige Sensor-, Compare-, Overflow-, UART- und
   EEPROM-Aktivität.
5. **Gezielt für diese Korrektur:** Sensorimpuls künstlich (z. B. per
   Funktionsgenerator) so einspeisen, dass er in das Ladefenster oder die
   aktive Ladung eines Zündereignisses fällt (kurzzeitig verkürzte Periode
   bzw. zusätzlicher Störimpuls), und mit Logikanalysator/Oszilloskop
   verifizieren, dass die Ladezeit dabei konstant bleibt und keine Lücke
   bzw. kein doppelt so großer Pulsabstand am Zündausgang entsteht. Dies ist
   der hostseitig nachgebildete, aber noch nicht hardwareseitig verifizierte
   Kernfall dieser Änderung.

Eine Einsatzfreigabe für ein Fahrzeug erfolgt damit nicht.
