# WDT

## Ursache des Verhaltens im gezeigten PIC-Code

Der Watchdog schaltet in der ersten Schleife nicht die Versorgung dauerhaft aus, obwohl `IN_START == 1` bleibt, weil der Controller in dieser Schleife nur **vom Watchdog zurückgesetzt** wird:

```c
while (IN_START == 1)
{
//  CLRWDT();
    __delay_ms(20);
}
```

### Warum das so aussieht, als ob nichts passiert

1. `WDTE_ON` in `__CONFIG(...)` aktiviert den Watchdog permanent.  
   Er kann damit nicht per Software abgeschaltet werden.
2. In der ersten `while`-Schleife wird der Watchdog nicht gelöscht, also läuft er nach der in `WDTCON` eingestellten Zeit ab.
3. Ein Watchdog-Ablauf schaltet aber **nicht automatisch `SUPPLY` aus**.  
   Stattdessen macht der PIC nur einen Reset und startet wieder bei `main()`.
4. Direkt nach dem Neustart setzt das Programm erneut:

```c
SUPPLY = 1;
```

Dadurch wirkt es so, als ob der Watchdog in der ersten Schleife nichts abschaltet.

## Entscheidend: Reset ist nicht gleich Abschalten

Wenn bei dauerhaftem `IN_START == 1` wirklich abgeschaltet werden soll, muss das Programm das explizit tun, zum Beispiel über einen eigenen Timeout-Zähler:

```c
unsigned int timeout = 0;

while (IN_START == 1)
{
    __delay_ms(20);
    timeout++;

    if (timeout >= 800)   // ca. 16 s bei 20 ms
    {
        IGNITION = 0;
        OUT_START = 0;
        SUPPLY = 0;
        while (1)
        {
        }
    }
}
```

## Kurzfassung

- `WDTE_ON` => Watchdog ist immer aktiv
- auskommentiertes `CLRWDT()` => Watchdog-Reset nach der in `WDTCON` eingestellten Zeit (hier laut Kommentar: 16 s)
- Watchdog-Reset => nur Neustart, **kein** automatisches `SUPPLY = 0`
- weil `SUPPLY` nach Reset wieder auf `1` gesetzt wird, bleibt die Schaltung scheinbar an
