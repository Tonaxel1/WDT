/*********************************************************************************
*        File: test_ignition_timing.c
*        Zweck: Host-Tests (gcc) fuer das Zuend-Steuermodell aus
*        My_Spark_1_2026.c. Alle Zeiten in 4-us-Ticks.
*        WICHTIG: Diese Tests validieren NICHT die Ziel-Firmware
*        (kein HI-TECH C, kein echtes PIC-Timing, kein Oszi-Nachweis).
************************************************************************************/
#include "host_pic_shim.h"
#include <stdio.h>
#include <string.h>

extern uint32_t sim_debug_step;

#define COIL    sim_LATA4
#define TIMER_UEBERLAEUFE_MAX 120
#define RX_TIMEOUT_TICKS 2000

/* Firmware-Schnittstelle */
extern unsigned char OT_1, LADEZEIT, WERT_1, WERT_2, WERT_3, TEMP, TXWERT;
extern unsigned char STARTFUNKE, START_WINKEL;
extern unsigned int  DREHZ_W, VOR_W, DREHZ_MAX, ZEIT;
extern unsigned long TIMER_WERT, DREHZAHL, SOLLWINKEL;
extern unsigned long NEUE_PERIODE, LADUNGSBEGINN, FUNKENZEITPUNKT, NEUES_OT;
extern unsigned char LADUNG_AKTIV, EREIGNIS, VORLAUF, OT_KORRIGIEREN;
extern unsigned char EMPFANGSZAEHLER, RAHMEN_AKTIV;
extern unsigned char EE_PENDING, EE_INDEX;
extern unsigned char ueberlaeufe;
extern unsigned int  timer1_hoch;
extern unsigned char BYTE1, BYTE2, BYTE3, BYTE4, BYTE5, BYTE6, BYTE7, BYTE8, BYTE9, BYTE10, BYTE11;

extern void Firmware_Init(void);
extern void EESchreibe(void);

static int fehler = 0;

#define CHECK(bed, ...) do { \
    if (!(bed)) { \
        fehler++; \
        printf("FEHLER %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

#define TICKS_US(us)   (((us) + 3u) / 4u)
#define US(ticks)      ((ticks) * 4u)

/* Standard-Parameter: 10 Grad nach OT, 20 Grad Vorwinkel, 2 ms Ladezeit,
   4000 1/min Vollzuendung, 10000 1/min Begrenzer */
static void parameter_standard(void)
{
    memset(sim_eeprom, 0xFF, sizeof(sim_eeprom));
    sim_eeprom[1]  = 10;          // START_WINKEL
    sim_eeprom[2]  = 20;          // VOR_W
    sim_eeprom[3]  = 3;           // LADEZEIT (2 ms)
    sim_eeprom[4]  = 4000 >> 8;   // DREHZ_W High
    sim_eeprom[5]  = 4000 & 0xFF;
    sim_eeprom[6]  = 10000 >> 8;  // DREHZ_MAX High
    sim_eeprom[7]  = 10000 & 0xFF;
    sim_eeprom[8]  = 4;           // WERT_1
    sim_eeprom[9]  = 8;           // WERT_2
    sim_eeprom[10] = 12;          // WERT_3
}

/* eine OT-Flanke erzeugen und die Firmware einen Schritt rechnen lassen */
static uint32_t ot_impuls(void)
{
    uint32_t t = sim_zeitstempel();
    sim_kante();
    sim_hauptschleife(2);
    return t;
}

/* Zeitspanne ablaufen lassen, Hauptprogramm laeuft weiter */
static void warten(uint32_t ticks)
{
    while (ticks)
    {
        uint32_t schritt = ticks > 40 ? 40 : ticks;
        sim_hauptschleife(schritt);
        ticks -= schritt;
    }
}

/* n Funken (COIL 1 -> 0) innerhalb der Zeitspanne zaehlen */
static uint32_t funken_waehrend(uint32_t ticks, uint32_t *erster, uint32_t *ladungen)
{
    uint32_t n = 0, lad = 0;
    uint8_t alt = COIL;
    if (erster) *erster = 0xFFFFFFFFu;
    while (ticks--)
    {
        sim_hauptschleife(1);
        if (alt == 1 && COIL == 0)
        {
            if (erster && *erster == 0xFFFFFFFFu)
                *erster = sim_zeitstempel();
            n++;
        }
        if (alt == 0 && COIL == 1)
            lad++;
        alt = COIL;
    }
    if (ladungen) *ladungen = lad;
    return n;
}

static void rev_laufen(uint32_t periode, int anzahl, uint32_t *funken, uint32_t *ladungen)
{
    uint32_t f = 0, l = 0;
    uint32_t t0, t1, i;
    int k;
    t0 = ot_impuls();
    for (k = 1; k < anzahl; k++)
    {
        f += funken_waehrend(periode, NULL, &l);
        t1 = ot_impuls();
        CHECK(t1 - t0 == periode, "Periode eingehalten: %lu statt %lu",
              (unsigned long)(t1 - t0), (unsigned long)periode);
        t0 = t1;
    }
    f += funken_waehrend(periode, NULL, &l);
    if (funken) *funken = f;
    if (ladungen) *ladungen = l;
    (void)i;
}

static void test_startfunken(void)
{
    uint32_t periode = 15000;      // 60 ms ~ 1000 1/min
    uint32_t f, l, t0, t1, spark = 0;
    int k;

    printf("== Test: Startfunken (START_WINKEL = 10) ==\n"); fflush(stdout);
    parameter_standard();
    sim_debug_step = 1;
    Firmware_Init();
    printf("init ok\n"); fflush(stdout);
    sim_debug_step = 2;

    CHECK(OT_1 == 0 && STARTFUNKE == 0, "Startzustand OT_1/STARTFUNKE");
    sim_debug_step = 3;
    f = funken_waehrend(4 * periode, NULL, &l);
    sim_debug_step = 4;
    printf("messphase ok f=%lu l=%lu\n", (unsigned long)f, (unsigned long)l); fflush(stdout);
    CHECK(f == 0, "kein Funke in den ersten 4 Messumdrehungen (%lu)", (unsigned long)f);

    for (k = 0; k < 4; k++)
        ot_impuls();
    CHECK(OT_1 == 4 && STARTFUNKE == 0, "4 Kanten gemessen, noch kein Startfunke");

    t0 = ot_impuls();
    CHECK(OT_1 == 5 && STARTFUNKE == 1, "5. Kante armiert den Startfunken");
    f = funken_waehrend(periode, &spark, &l);
    CHECK(f == 1 && l == 1, "genau 1 Startfunke + 1 Ladung (f=%lu l=%lu)",
          (unsigned long)f, (unsigned long)l);
    CHECK(spark == t0 + 417 + ZEIT, "Startfunke bei OT+10Grad+Ladezeit (%lu statt %lu)",
          (unsigned long)spark, (unsigned long)(t0 + 417 + ZEIT));

    for (k = 0; k < 4; k++)
    {
        t1 = ot_impuls();
        CHECK(t1 - t0 == periode, "Periode im Startlauf");
        f = funken_waehrend(periode, &spark, &l);
        CHECK(f == 1, "Startfunke %d genau einmal (f=%lu)", k + 2, (unsigned long)f);
        CHECK(spark == t1 + 417 + ZEIT, "Startfunke %d bei OT+917 (%lu)",
              k + 2, (unsigned long)spark);
        t0 = t1;
    }
    CHECK(STARTFUNKE == 6, "nach 5 Startfunken Uebergang (STARTFUNKE=%u)", STARTFUNKE);

    /* erste Normalumdrehung: Funke 20 Grad vor OT (2000 1/min < 4000) */
    t0 = ot_impuls();
    f = funken_waehrend(periode, &spark, &l);
    CHECK(f == 1 && l == 1, "Normalbetrieb: 1 Funke, 1 Ladung");
    CHECK(spark == t0 + periode - 417, "Funke 5 Grad vor OT bei 1000 1/min (%lu statt %lu)",
          (unsigned long)spark, (unsigned long)(t0 + periode - 417));

    /* Schwingen zwischen Zustaenden verboten: keine Doppelfunken */
    rev_laufen(periode, 20, &f, &l);
    CHECK(f == 20 && l == 20, "20 Umdrehungen: 20 Funken, 20 Ladungen (f=%lu l=%lu)",
          (unsigned long)f, (unsigned long)l);
}

static void test_startwinkel_null(void)
{
    uint32_t periode = 15000;
    uint32_t f, t0, spark = 0;
    int k;

    printf("== Test: START_WINKEL = 0 ==\n");
    parameter_standard();
    sim_eeprom[1] = 0;
    Firmware_Init();

    for (k = 0; k < 5; k++)
        ot_impuls();
    t0 = ot_impuls() - periode; /* Zeitpunkt der 5. Kante */
    f = funken_waehrend(periode, &spark, NULL);
    CHECK(f == 1, "START_WINKEL=0: genau 1 Funke (%lu)", (unsigned long)f);
    CHECK(spark == t0 + ZEIT, "Funke bei OT+Ladezeit (%lu statt %lu)",
          (unsigned long)spark, (unsigned long)(t0 + ZEIT));
}

static void test_vorladung_1000rpm(void)
{
    /* 1000 1/min: periode = 60 ms; VOR_W = 20 Grad -> Funken OT+58333.
       Ladezeit 2 ms -> Ladebeginn OT+56333, kein Vorlauf noetig. */
    uint32_t periode = 15000;
    uint32_t f, l, t0, spark = 0;
    int k;

    printf("== Test: 1000 1/min, Ladezeit 2 ms (kein Vorlauf noetig) ==\n");
    parameter_standard();
    Firmware_Init();
    for (k = 0; k < 5; k++)
        ot_impuls();
    funken_waehrend(periode, NULL, NULL);
    for (k = 0; k < 5; k++)
    {
        funken_waehrend(periode, NULL, NULL);
        ot_impuls();
    }
    /* ab hier Normalbetrieb */
    t0 = ot_impuls();
    f = funken_waehrend(periode, &spark, &l);
    CHECK(f == 1, "1 Funke im Normalbetrieb (%lu)", (unsigned long)f);
    CHECK(spark == t0 + periode - 417, "Funke 5 Grad vor OT (%lu statt %lu)",
          (unsigned long)spark, (unsigned long)(t0 + periode - 417));
}

static void test_vorladung_ueber_ot(void)
{
    /* 6000 1/min: periode = 10 ms = 2500 Ticks; VOR_W = 20 Grad ->
       Funken bei OT + 2500 - 139 = OT + 2361; Ladebeginn OT + 1861:
       liegt NACH dem aktuellen OT, aber vor dem Funken -> normal.
       Erst ab Ladezeit > 11 Grad wird der Vorlauf aktiv:
       3000 us = 750 Ticks > 139 Ticks (5 Grad) -> Vorlauf. */
    uint32_t periode = 2500;
    uint32_t f, l, t0, spark = 0, ladung = 0xFFFFFFFFu;
    int k;
    uint8_t alt;

    printf("== Test: 6000 1/min, Ladezeit 3 ms (Vorlauf ueber OT) ==\n");
    parameter_standard();
    sim_eeprom[3] = 5;             // 3 ms Ladezeit
    Firmware_Init();
    CHECK(ZEIT == 750, "Ladezeit 5 = 750 Ticks (ZEIT=%u)", ZEIT);

    for (k = 0; k < 11; k++)
    {
        funken_waehrend(periode, NULL, NULL);
        ot_impuls();
    }

    /* Normalbetrieb: 6000 1/min >= DREHZ_W 4000 -> volle 20 Grad.
       Funken: OT + 2500 - 139 = OT + 2361.
       Ladebeginn Soll: OT + 1611 -> vor OT+0? 1611 < 2500, also Ladebeginn
       innerhalb der Umdrehung, aber der Vorlauf-Test: LADUNGSBEGINN <= kante?
       kante = aktuelles OT, LADUNGSBEGINN = kante + 1611 > kante, also
       wird der Vorlauf erst ausgeloest, wenn ziel < ZEIT.
       Hier ziel = 139 < 750 -> geklemmt auf 750: Funken OT+1750, Ladung OT+1000. */
    t0 = ot_impuls();
    alt = COIL;
    f = 0; l = 0;
    {
        uint32_t i;
        for (i = 0; i < periode; i++)
        {
            sim_hauptschleife(1);
            if (alt == 1 && COIL == 0 && spark == 0)
                spark = sim_zeitstempel();
            if (alt == 0 && COIL == 1 && ladung == 0xFFFFFFFFu)
                ladung = sim_zeitstempel();
            alt = COIL;
        }
    }
    (void)f; (void)l;
    CHECK(ladung == t0 + 1000, "Ladung bei OT+1000 (%lu statt %lu)",
          (unsigned long)ladung, (unsigned long)(t0 + 1000));
    CHECK(spark == t0 + 1750, "Funke bei OT+1750 = Ladezeit-Grenze (%lu statt %lu)",
          (unsigned long)spark, (unsigned long)(t0 + 1750));

    /* stabiler Weiterlauf ohne Doppelfunken */
    rev_laufen(periode, 10, &f, &l);
    CHECK(f == 10 && l == 10, "Vorlauf stabil: f=%lu l=%lu",
          (unsigned long)f, (unsigned long)l);
}

static void test_ladezeit_endpunkte(void)
{
    static const unsigned int soll[5] = { 250, 375, 500, 625, 750 };
    int lz;
    printf("== Test: Ladezeit-Endpunkte 1..5 ==\n");
    for (lz = 1; lz <= 5; lz++)
    {
        parameter_standard();
        sim_eeprom[3] = (uint8_t)lz;
        Firmware_Init();
        CHECK(ZEIT == soll[lz - 1], "LADEZEIT %d -> ZEIT %u (ist %u)",
              lz, soll[lz - 1], ZEIT);
    }
    parameter_standard();
    sim_eeprom[3] = 0;             // ungueltig -> 3
    Firmware_Init();
    CHECK(LADEZEIT == 3 && ZEIT == 500, "LADEZEIT 0 -> 3");
    parameter_standard();
    sim_eeprom[3] = 99;            // ungueltig -> 3
    Firmware_Init();
    CHECK(LADEZEIT == 3 && ZEIT == 500, "LADEZEIT 99 -> 3");
}

static void test_lange_periode_wrap(void)
{
    /* 100 1/min: 600 ms = 150000 Ticks > 65535 (16-Bit Ueberlauf) */
    uint32_t periode = 150000;
    uint32_t f, l;
    int k;

    printf("== Test: 100 1/min ueber dem 16-Bit-Ueberlauf ==\n");
    parameter_standard();
    Firmware_Init();
    for (k = 0; k < 5; k++)
        ot_impuls();
    CHECK(DREHZAHL >= 95 && DREHZAHL <= 105, "Drehzahl 100 gemessen (%lu)",
          (unsigned long)DREHZAHL);
    /* Startfunke: 10 Grad = 16667 Ticks nach OT, danach 2 ms laden */
    f = funken_waehrend(periode, NULL, &l);
    CHECK(f == 1 && l == 1, "Funke und Ladung bei 100 1/min (f=%lu l=%lu)",
          (unsigned long)f, (unsigned long)l);
    CHECK(timer1_hoch >= 2, "Ueberlaeufe gezaehlt (hoch=%u)", timer1_hoch);
}

static void test_begrenzer(void)
{
    /* 12000 1/min: 5 ms = 1250 Ticks; Begrenzer 10000 */
    uint32_t periode = 1250;
    uint32_t f, l;
    int k;

    printf("== Test: Drehzahlbegrenzer bei 12000 1/min ==\n");
    parameter_standard();
    Firmware_Init();
    for (k = 0; k < 12; k++)
    {
        funken_waehrend(periode, NULL, NULL);
        ot_impuls();
    }
    CHECK(DREHZAHL == 12000, "Drehzahl 12000 gemessen, nicht gekappt (%lu)",
          (unsigned long)DREHZAHL);
    f = funken_waehrend(periode, NULL, &l);
    CHECK(f == 0 && l == 0, "Begrenzer: kein Funke, keine Ladung (f=%lu l=%lu)",
          (unsigned long)f, (unsigned long)l);

    /* Rueckfall unter den Begrenzer: Zuendung wieder da */
    funken_waehrend(5000, NULL, NULL);
    ot_impuls();                    // 3000 1/min
    CHECK(DREHZAHL == 3000, "Drehzahl 3000 nach Rueckfall (%lu)", (unsigned long)DREHZAHL);
    f = funken_waehrend(5000, NULL, &l);
    CHECK(f == 1 && l == 1, "Zuendung nach Rueckfall aktiv (f=%lu l=%lu)",
          (unsigned long)f, (unsigned long)l);
}

static void test_sensorverlust_waehrend_ladung(void)
{
    int k;
    uint32_t ladung_start, coil_aus = 0xFFFFFFFFu, i;
    uint8_t alt;

    printf("== Test: Sensorverlust waehrend der Ladung ==\n");
    parameter_standard();
    Firmware_Init();
    for (k = 0; k < 5; k++)
        ot_impuls();
    /* Startfunke planen: Ladung beginnt OT+417, dauert 500 Ticks */
    funken_waehrend(400, NULL, NULL);
    CHECK(COIL == 0, "noch keine Ladung");
    sim_hauptschleife(17);          // bis OT+417
    CHECK(COIL == 1, "Ladung laeuft (COIL=1)");
    ladung_start = sim_zeitstempel();

    /* Sensor bleibt stumm: nach ca. 2 s (120 Ueberlaeufe) muss COIL aus sein */
    alt = COIL;
    for (i = 0; i < (uint32_t)(TIMER_UEBERLAEUFE_MAX + 2) * 65536u; i += 400)
    {
        sim_hauptschleife(400);
        if (alt == 1 && COIL == 0 && coil_aus == 0xFFFFFFFFu)
            coil_aus = sim_zeitstempel();
        alt = COIL;
    }
    CHECK(COIL == 0, "COIL nach Sensor-Timeout aus");
    CHECK(coil_aus != 0xFFFFFFFFu, "Abschaltzeitpunkt erfasst");
    if (coil_aus != 0xFFFFFFFFu)
        CHECK(US(coil_aus - ladung_start) <= 2100000u,
              "COIL-Ein-Zeit begrenzt (< 2,1 s): %lu us",
              (unsigned long)US(coil_aus - ladung_start));
    CHECK(DREHZAHL == 0 && OT_1 == 0, "Betrieb zurueckgesetzt");
}

static void test_erste_kanten_ohne_periode(void)
{
    printf("== Test: erste Kante ohne gueltige Periode ==\n");
    parameter_standard();
    Firmware_Init();
    ot_impuls();
    CHECK(OT_1 == 1, "erste Kante gezaehlt");
    CHECK(EREIGNIS == 0, "kein Ereignis ohne Periode (EREIGNIS=%u)", EREIGNIS);
    CHECK(COIL == 0, "COIL bleibt aus");
    ot_impuls();
    CHECK(TIMER_WERT > 0, "Periode nach zweiter Kante vorhanden");
}

static void test_uart_empfang(void)
{
    static const uint8_t rahmen[11] = {
        'S', 15, 30, 4, 5000 >> 8, 5000 & 0xFF, 8000 >> 8, 8000 & 0xFF, 5, 9, 13
    };
    uint32_t periode = 15000;
    int k, i;
    uint32_t f, l;

    printf("== Test: UART-Rahmen bei laufender Zuendung ==\n");
    parameter_standard();
    Firmware_Init();
    for (k = 0; k < 12; k++)
    {
        funken_waehrend(periode, NULL, NULL);
        ot_impuls();
    }

    for (i = 0; i < 11; i++)
    {
        sim_sende(rahmen[i]);
        sim_hauptschleife(50);      // ~200 us je Byte, kein Blockieren
    }
    /* EEPROM-Schreibmaschine abarbeiten: 10 Bytes x ca. 5 ms */
    warten(10 * SIM_EE_DAUER_TICKS + 20000);

    CHECK(START_WINKEL == 15, "START_WINKEL = 15 (%u)", START_WINKEL);
    CHECK(VOR_W == 30, "VOR_W = 30 (%u)", VOR_W);
    CHECK(LADEZEIT == 4 && ZEIT == 625, "LADEZEIT = 4 (%u/%u)", LADEZEIT, ZEIT);
    CHECK(DREHZ_W == 5000, "DREHZ_W = 5000 (%u)", DREHZ_W);
    CHECK(DREHZ_MAX == 8000, "DREHZ_MAX = 8000 (%u)", DREHZ_MAX);
    CHECK(WERT_1 == 5 && WERT_2 == 9 && WERT_3 == 13, "LED-Werte");
    CHECK(sim_eeprom[1] == 15 && sim_eeprom[3] == 4, "EEPROM abgelegt");
    CHECK(sim_eeprom[4] == (5000 >> 8) && sim_eeprom[5] == (5000 & 0xFF),
          "EEPROM Big-Endian DREHZ_W");
    CHECK(!EE_PENDING, "EEPROM-Maschine fertig");

    /* Zuendung laeuft weiter */
    f = funken_waehrend(periode, NULL, &l);
    CHECK(f == 1 && l == 1, "Zuendung nach Empfang aktiv (f=%lu l=%lu)",
          (unsigned long)f, (unsigned long)l);
}

static void test_uart_timeout_und_oerr(void)
{
    uint32_t periode = 15000;
    int k;

    printf("== Test: UART-Rahmen-Timeout und OERR ==\n");
    parameter_standard();
    Firmware_Init();
    for (k = 0; k < 6; k++)
    {
        funken_waehrend(periode, NULL, NULL);
        ot_impuls();
    }

    sim_sende('S');
    sim_hauptschleife(10);
    sim_sende(12);
    sim_hauptschleife(10);
    /* Rest kommt nie: nach RX_TIMEOUT_TICKS muss der Rahmen verworfen sein */
    warten(RX_TIMEOUT_TICKS + 500);
    CHECK(!RAHMEN_AKTIV, "Rahmen nach Timeout verworfen");
    CHECK(START_WINKEL == 10, "START_WINKEL unveraendert (%u)", START_WINKEL);

    /* OERR: Empfang wird neu gestartet */
    RCSTAbits.OERR = 1;
    sim_hauptschleife(5);
    CHECK(RCSTAbits.CREN == 1, "CREN wieder gesetzt");
}

static void test_ungueltige_parameter_empfang(void)
{
    static const uint8_t rahmen[11] = {
        'S', 99, 77, 9, 0xFF, 0xFF, 0x00, 0x01, 55, 66, 77
    };
    int i;

    printf("== Test: ungueltige Parameter werden begrenzt ==\n");
    parameter_standard();
    Firmware_Init();
    for (i = 0; i < 11; i++)
    {
        sim_sende(rahmen[i]);
        sim_hauptschleife(50);
    }
    warten(10 * SIM_EE_DAUER_TICKS + 20000);
    CHECK(START_WINKEL == 10, "START_WINKEL 99 -> 10 (%u)", START_WINKEL);
    CHECK(VOR_W == 40, "VOR_W 77 -> 40 (%u)", VOR_W);
    CHECK(LADEZEIT == 3, "LADEZEIT 9 -> 3 (%u)", LADEZEIT);
    CHECK(DREHZ_W == 4000, "DREHZ_W 65535 -> 4000 (%u)", DREHZ_W);
    CHECK(DREHZ_MAX == 10000, "DREHZ_MAX 256 -> 10000 (%u)", DREHZ_MAX);
    CHECK(WERT_1 == 4 && WERT_2 == 8 && WERT_3 == 12, "LED-Werte begrenzt");
}

static void test_eesave_gie(void)
{
    printf("== Test: EESchreibe sichert GIE ==\n");
    parameter_standard();
    Firmware_Init();
    INTCONbits.GIE = 0;
    EEDATA = 42;
    EEADRL = 20;
    EESchreibe();
    CHECK(INTCONbits.GIE == 0, "GIE bleibt 0");
    sim_schritt(SIM_EE_DAUER_TICKS + 10);
    CHECK(sim_eeprom[20] == 42, "Byte geschrieben");
    INTCONbits.GIE = 1;
    EEDATA = 43;
    EEADRL = 21;
    EESchreibe();
    CHECK(INTCONbits.GIE == 1, "GIE bleibt 1");
    sim_schritt(SIM_EE_DAUER_TICKS + 10);
    CHECK(sim_eeprom[21] == 43, "Byte 2 geschrieben");
}

static void test_gleichzeitige_flags(void)
{
    /* OT-Kante und Compare im selben Tick: beide muessen bedient werden */
    uint32_t periode = 15000;
    int k;

    printf("== Test: gleichzeitige Flags ==\n");
    parameter_standard();
    Firmware_Init();
    for (k = 0; k < 11; k++)
    {
        funken_waehrend(periode, NULL, NULL);
        ot_impuls();
    }
    /* Normalbetrieb: Ladebeginn OT+14083, Funke OT+14583.
       Kante genau beim Ladebeginn: sim_kante setzt die Flags, danach
       wird der Compare im selben Schritt geprueft. */
    funken_waehrend(periode - 417, NULL, NULL);   // bis kurz vor Funke
    ot_impuls();
    CHECK(OT_1 > 5, "Betrieb laeuft (OT_1=%u)", OT_1);
}

static void test_init_werte(void)
{
    printf("== Test: Init-Sequenz und Register ==\n"); fflush(stdout);
    sim_debug_step = 100;
    parameter_standard();
    Firmware_Init();
    sim_debug_step = 101;
    CHECK(ANSELA == 0 && ANSELB == 0, "ANSEL digital");
    CHECK(TRISA == 0, "TRISA Ausgaenge");
    CHECK(TRISB == 0b00010111, "TRISB = 0x17 (ist 0x%02X)", TRISB);
    CHECK(WPUB == 0b00010111, "WPUB = 0x17 (ist 0x%02X)", WPUB);
    CHECK(OPTION_REG == 0b00111111, "OPTION_REG (ist 0x%02X)", OPTION_REG);
    CHECK(APFCON0 == 0 && APFCON1 == 0, "APFCON 0");
    CHECK(SPBRG == 207 && SPBRGH == 0, "9600 Baud");
    CHECK(BAUDCON == 0b00001000, "BAUDCON 16 Bit");
    CHECK(TXSTA == 0b00100100, "TXSTA (ist 0x%02X)", TXSTA);
    CHECK(RCSTA == 0b10010000, "RCSTA (ist 0x%02X)", RCSTA);
    CHECK(T1GCON == 0, "Timer1 Gate aus");
    CHECK(INTCONbits.PEIE == 1 && INTCONbits.GIE == 1, "Interrupts frei");
    CHECK(START_WINKEL == 10 && VOR_W == 20, "Parameter aus EEPROM");
}

int main(void)
{
    test_init_werte();
    test_startfunken();
    test_startwinkel_null();
    test_vorladung_1000rpm();
    test_vorladung_ueber_ot();
    test_ladezeit_endpunkte();
    test_lange_periode_wrap();
    test_begrenzer();
    test_sensorverlust_waehrend_ladung();
    test_erste_kanten_ohne_periode();
    test_uart_empfang();
    test_uart_timeout_und_oerr();
    test_ungueltige_parameter_empfang();
    test_eesave_gie();
    test_gleichzeitige_flags();

    if (fehler == 0)
        printf("\nALLE TESTS BESTANDEN\n");
    else
        printf("\n%d FEHLER\n", fehler);
    return fehler ? 1 : 0;
}
