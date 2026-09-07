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

Eine Einsatzfreigabe für ein Fahrzeug erfolgt damit nicht.
