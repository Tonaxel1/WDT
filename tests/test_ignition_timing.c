/*********************************************************************************
*        File: test_ignition_timing.c
*        Zweck: Host-Regressionstests (gcc) fuer die Zuendsteuerung aus
*        My_Spark_1_2026.c. Alle Zeiten in 4-us-Ticks des Timer1.
*
*        Geprueft werden Ausgangssignale (COIL an LATA4) und Messwerte,
*        nicht nur interne Flags. Der Ablauf beginnt immer beim echten
*        Reset (SystemStart) und der ersten Sensorflanke.
*
*        WICHTIG: Diese Tests validieren NICHT die Ziel-Firmware. Es fehlen
*        HI-TECH C / XC8, echtes PIC-Timing, ISR-Laufzeiten und jeder
*        Oszilloskop-Nachweis.
************************************************************************************/
#include "host_pic_shim.h"
#include <stdio.h>
#include <string.h>

#define COIL sim_LATA4
#define LED1 sim_LATA0

/* Firmware-Schnittstelle */
extern u8  LADEZEIT, WERT_1, WERT_2, WERT_3;
extern u8  START_WINKEL;
extern u16 DREHZ_W, VOR_W, DREHZ_MAX, ZEIT;
extern u8  OT_1, STARTFUNKE, KANTE_GUELTIG, FEHLTERMIN;
extern u32 TIMER_WERT, DREHZAHL, SOLLWINKEL, LETZTE_KANTE;
extern volatile u8  LADUNG_AKTIV, FUNKE_ARMIERT, LADUNG_ARMIERT, OT_NEU, OT_VERLOREN;
extern volatile u32 FUNKENZEITPUNKT, LADUNGSBEGINN, PLAN_FUNKE;
extern volatile u16 timer1_hoch;
extern u8  EE_PENDING, RAHMEN_AKTIV, QUITTUNG_AKTIV;

extern void Firmware_Init(void);
extern void Firmware_Rumpf(void);
extern void EESchreibe(void);
extern u32  ZeitJetzt(void);

static int fehler = 0;

#define CHECK(bed, ...) do { \
    if (!(bed)) { \
        fehler++; \
        printf("  FEHLER %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

/*--------------------------------------------------------------------------
  Pruefstand: OT-Impulse erzeugen, COIL-Flanken mitschreiben
--------------------------------------------------------------------------*/
#define MAX_EV 400

static uint32_t ev_zeit[MAX_EV];
static uint8_t  ev_typ[MAX_EV];        /* 1 = Ladung ein, 0 = Funke */
static int      ev_n;
static uint8_t  coil_alt;

static uint32_t kante_zeit[MAX_EV];
static int      kante_n;
static uint32_t kante_naechste;
static uint32_t kante_periode;
static int      kante_aktiv;

static uint32_t schleifen_takt = 1;    /* Ticks je Hauptschleifendurchlauf */
static uint32_t takt_zaehler;

/* Nur den Mitschnitt loeschen; der Impulsgeber laeuft weiter. */
static void ereignisse_loeschen(void)
{
    ev_n = 0;
    kante_n = 0;
    coil_alt = COIL;
}

static void pruefstand_reset(void)
{
    kante_aktiv = 0;
    schleifen_takt = 1;
    takt_zaehler = 0;
    ereignisse_loeschen();
}

/* Index der Kante (0-basiert), nach der der erste Startfunke liegt.
   Reicht START_WINKEL fuer die volle Ladezeit, wird der Funke derselben
   Umdrehung geplant (Kante 5), sonst die naechste vorhergesagt (Kante 6). */
static int start_kante_index(uint32_t ziel)
{
    return (ziel >= (uint32_t)ZEIT + 25u) ? 4 : 5;
}

static void aufzeichnen(void)
{
    if (COIL != coil_alt)
    {
        if (ev_n < MAX_EV)
        {
            ev_zeit[ev_n] = sim_zeitstempel();
            ev_typ[ev_n] = COIL ? 1 : 0;
            ev_n++;
        }
        coil_alt = COIL;
    }
}

static void fahre(uint32_t ticks)
{
    uint32_t i;
    for (i = 0; i < ticks; i++)
    {
        uint32_t t = sim_zeitstempel();
        if (kante_aktiv && t == kante_naechste)
        {
            sim_kante();
            if (kante_n < MAX_EV)
                kante_zeit[kante_n++] = t;
            kante_naechste += kante_periode;
        }
        if ((takt_zaehler % schleifen_takt) == 0)
            Firmware_Rumpf();
        takt_zaehler++;
        sim_schritt(1);
        aufzeichnen();
    }
}

/* Impulsgeber starten: erste Flanke nach "vorlauf" Ticks, danach alle
   "periode" Ticks. */
static void impulse_starten(uint32_t periode, uint32_t vorlauf)
{
    kante_periode = periode;
    kante_naechste = sim_zeitstempel() + vorlauf;
    kante_aktiv = 1;
}

static void impulse_stoppen(void)
{
    kante_aktiv = 0;
}

/* Anzahl Funken (COIL 1 -> 0) im Mitschnitt */
static int funken_zahl(void)
{
    int i, n = 0;
    for (i = 0; i < ev_n; i++)
        if (ev_typ[i] == 0)
            n++;
    return n;
}

static int ladungen_zahl(void)
{
    int i, n = 0;
    for (i = 0; i < ev_n; i++)
        if (ev_typ[i] == 1)
            n++;
    return n;
}

/* n-ter Funke (0-basiert), 0xFFFFFFFF wenn nicht vorhanden */
static uint32_t funke_zeit(int n)
{
    int i;
    for (i = 0; i < ev_n; i++)
        if (ev_typ[i] == 0 && n-- == 0)
            return ev_zeit[i];
    return 0xFFFFFFFFu;
}

static uint32_t ladung_zeit(int n)
{
    int i;
    for (i = 0; i < ev_n; i++)
        if (ev_typ[i] == 1 && n-- == 0)
            return ev_zeit[i];
    return 0xFFFFFFFFu;
}

/* Sollwerte aus der Spezifikation (bewusst getrennt von der Firmware
   noch einmal formuliert) */
static uint32_t winkel_ticks(uint32_t periode, uint32_t grad)
{
    return (periode * grad + 180u) / 360u;
}

static uint32_t soll_vorwinkel(uint32_t drehzahl)
{
    if (drehzahl < DREHZ_W)
        return (drehzahl * VOR_W + (DREHZ_W / 2u)) / DREHZ_W;
    return VOR_W;
}

/* Standard-Parameter: Funke 10 Grad nach OT im Start, 20 Grad Vorwinkel,
   2 ms Ladezeit, 4000 1/min Vollzuendung, 10000 1/min Begrenzer */
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

static void start_standard(void)
{
    parameter_standard();
    Firmware_Init();
    pruefstand_reset();
}

/*--------------------------------------------------------------------------
  Tests
--------------------------------------------------------------------------*/
static void test_init_und_zeitbasis(void)
{
    printf("== Init, Registerbild und Zeitbasis ==\n");
    start_standard();

    CHECK(ANSELA == 0 && ANSELB == 0, "ANSEL digital");
    CHECK(TRISA == 0, "TRISA Ausgaenge");
    CHECK(TRISB == 0x17, "TRISB = 0x17 (ist 0x%02X)", TRISB);
    CHECK(WPUB == 0x17, "WPUB = 0x17 (ist 0x%02X)", WPUB);
    CHECK(OPTION_REG == 0x3F, "OPTION_REG (ist 0x%02X)", OPTION_REG);
    CHECK(APFCON0 == 0 && APFCON1 == 0, "APFCON 0");
    CHECK(SPBRG == 207 && SPBRGH == 0, "9600 Baud");
    CHECK(BAUDCON == 0x08, "BAUDCON 16 Bit");
    CHECK(TXSTA == 0x24, "TXSTA (ist 0x%02X)", TXSTA);
    CHECK(RCSTA == 0x90, "RCSTA (ist 0x%02X)", RCSTA);
    CHECK(T1GCON == 0, "Timer1 Gate aus");

    /* Zeitbasis: FOSC/4 mit Prescaler 1:8 -> 4 us je Tick */
    CHECK(T1CON == 0x31, "T1CON = 0b00110001 (ist 0x%02X)", T1CON);
    CHECK(sim_takt_fehler == 0, "Timer1-Quelle FOSC/4");
    CHECK(sim_faktor == 1, "Prescaler 1:8 = 4 us je Tick (Faktor %lu)",
          (unsigned long)sim_faktor);
    CHECK(sim_zeitstempel() == 0, "Timer deterministisch bei 0 gestartet (%lu)",
          (unsigned long)sim_zeitstempel());
    CHECK(timer1_hoch == 0, "Ueberlaufzaehler 0");

    CHECK(CCP1CON == 0x0A && CCP2CON == 0x0A, "CCP im Compare-Modus");
    CHECK(PIE1bits.CCP1IE == 0 && PIE2bits.CCP2IE == 0,
          "Compare-Interrupts erst mit gueltigem Termin frei");
    CHECK(INTCONbits.PEIE == 1 && INTCONbits.GIE == 1, "Interrupts frei");
    CHECK(INTCONbits.IOCIE == 1 && IOCBNbits.IOCBN4 == 1, "IOC auf RB4 aktiv");
    CHECK(COIL == 0, "COIL nach Reset aus");
    CHECK(START_WINKEL == 10 && VOR_W == 20, "Parameter aus EEPROM");

    /* Gegenprobe: der alte Wert 0b00010001 (1:2) laesft die Zeitbasis
       viermal zu schnell laufen - das Modell wuerde das aufdecken. */
    T1CON = 0x11;
    host_register_sync();
    CHECK(sim_faktor == 4, "Modell erkennt Prescaler 1:2 (Faktor %lu)",
          (unsigned long)sim_faktor);
    T1CON = 0x31;
    host_register_sync();
}

static void test_erste_kanten_und_periode(void)
{
    uint32_t periode = 15000;      /* 60 ms = 1000 1/min */

    printf("== Erste Kante, Periodenmessung, Drehzahl ==\n");
    start_standard();

    impulse_starten(periode, 100);
    fahre(150);                     /* erste Kante */
    CHECK(kante_n == 1, "erste Kante erzeugt");
    CHECK(OT_1 == 1, "erste Kante gezaehlt (OT_1=%u)", OT_1);
    CHECK(KANTE_GUELTIG == 1, "erste Kante ist Referenz");
    CHECK(TIMER_WERT == 0 && DREHZAHL == 0, "noch keine Periode");
    CHECK(COIL == 0, "kein Funke aus der ersten Kante");

    fahre(periode);                 /* zweite Kante */
    CHECK(kante_n == 2, "zweite Kante erzeugt");
    CHECK(TIMER_WERT == periode, "60 ms = 15000 Ticks gemessen (%lu)",
          (unsigned long)TIMER_WERT);
    CHECK(DREHZAHL == 1000, "15000000/15000 = 1000 1/min (%lu)",
          (unsigned long)DREHZAHL);
    CHECK(funken_zahl() == 0, "in der Messphase kein Funke");
}

static void test_fuenf_messkanten_fuenf_startfunken(void)
{
    uint32_t periode = 15000;
    uint32_t ziel;
    int i;

    printf("== Fuenf Messkanten, genau fuenf Startfunken ==\n");
    start_standard();
    ziel = winkel_ticks(periode, START_WINKEL);   /* 417 Ticks = 10 Grad */

    impulse_starten(periode, 100);
    fahre(5 * periode);             /* Kanten 1..5 */
    CHECK(kante_n == 5, "5 Kanten (%d)", kante_n);
    CHECK(OT_1 == 5, "5 Messkanten gezaehlt (OT_1=%u)", OT_1);
    CHECK(funken_zahl() == 0, "waehrend der Messphase kein Funke (%d)", funken_zahl());
    CHECK(STARTFUNKE == 1, "nach der 5. Kante ist der 1. Startfunke geplant");

    /* Bis kurz hinter den fuenften Startfunken fahren, damit ausschliesslich
       Startfunken im Mitschnitt stehen. */
    fahre(4 * periode + ziel + 200);
    CHECK(STARTFUNKE == 5, "Startzaehler bei 5 (%u)", STARTFUNKE);
    CHECK(funken_zahl() == 5, "genau 5 Startfunken (%d)", funken_zahl());
    CHECK(ladungen_zahl() == 5, "genau 5 Ladungen (%d)", ladungen_zahl());

    /* Startfunke k liegt START_WINKEL nach der Kante k+6 (1-basiert:
       Kanten 6..10), die Ladung genau ZEIT davor - also vor dem OT. */
    for (i = 0; i < 5; i++)
    {
        uint32_t soll_funke = kante_zeit[start_kante_index(ziel) + i] + ziel;
        CHECK(funke_zeit(i) == soll_funke,
              "Startfunke %d bei OT+%lu (ist %lu, soll %lu)", i + 1,
              (unsigned long)ziel, (unsigned long)funke_zeit(i),
              (unsigned long)soll_funke);
        CHECK(ladung_zeit(i) == soll_funke - ZEIT,
              "Ladung %d beginnt ZEIT vor dem Funken (ist %lu, soll %lu)", i + 1,
              (unsigned long)ladung_zeit(i), (unsigned long)(soll_funke - ZEIT));
        CHECK(ladung_zeit(i) < kante_zeit[start_kante_index(ziel) + i],
              "Ladung %d beginnt VOR dem OT (praediktiv): %lu < %lu", i + 1,
              (unsigned long)ladung_zeit(i),
              (unsigned long)kante_zeit[start_kante_index(ziel) + i]);
    }

    /* Danach folgt der Normalbetrieb: Funke VOR dem OT. */
    fahre(2 * periode);
    CHECK(STARTFUNKE == 5, "keine weiteren Startfunken (%u)", STARTFUNKE);
    CHECK(funke_zeit(5) < kante_zeit[10],
          "sechster Funke liegt vor dem OT (%lu < %lu)",
          (unsigned long)funke_zeit(5), (unsigned long)kante_zeit[10]);
}

static void test_startwinkel_varianten(void)
{
    static const uint8_t winkel[3] = { 0, 10, 20 };
    uint32_t periode = 15000;
    int w, i;

    printf("== START_WINKEL 0 / 10 / 20 Grad ==\n");
    for (w = 0; w < 3; w++)
    {
        uint32_t ziel;
        parameter_standard();
        sim_eeprom[1] = winkel[w];
        Firmware_Init();
        pruefstand_reset();
        ziel = winkel_ticks(periode, winkel[w]);
        CHECK(START_WINKEL == winkel[w], "START_WINKEL %u uebernommen (%u)",
              winkel[w], START_WINKEL);

        impulse_starten(periode, 100);
        fahre(7 * periode);
        CHECK(funken_zahl() >= 2, "Startfunken vorhanden (%d)", funken_zahl());
        for (i = 0; i < 2; i++)
        {
            uint32_t soll = kante_zeit[start_kante_index(ziel) + i] + ziel;
            CHECK(funke_zeit(i) == soll,
                  "START_WINKEL %u: Funke %d bei OT+%lu (ist %lu, soll %lu)",
                  winkel[w], i + 1, (unsigned long)ziel,
                  (unsigned long)funke_zeit(i), (unsigned long)soll);
            /* volle Ladezeit auch bei 0 Grad, weil vor dem OT geladen wird */
            CHECK(soll - ladung_zeit(i) == ZEIT,
                  "START_WINKEL %u: volle Ladezeit %u (ist %lu)", winkel[w], ZEIT,
                  (unsigned long)(soll - ladung_zeit(i)));
        }
    }
}

static void test_ladezeit_endpunkte(void)
{
    static const unsigned int soll[5] = { 250, 375, 500, 625, 750 };
    uint32_t periode = 15000;
    int lz;

    printf("== Ladezeit-Endpunkte 1..5 (1,0 bis 3,0 ms) ==\n");
    for (lz = 1; lz <= 5; lz++)
    {
        parameter_standard();
        sim_eeprom[3] = (uint8_t)lz;
        Firmware_Init();
        pruefstand_reset();
        CHECK(ZEIT == soll[lz - 1], "LADEZEIT %d -> ZEIT %u (ist %u)",
              lz, soll[lz - 1], ZEIT);

        impulse_starten(periode, 100);
        fahre(7 * periode);
        CHECK(funke_zeit(0) - ladung_zeit(0) == soll[lz - 1],
              "LADEZEIT %d: gemessene Ladedauer %lu Ticks", lz,
              (unsigned long)(funke_zeit(0) - ladung_zeit(0)));
    }

    parameter_standard();
    sim_eeprom[3] = 0;             /* ungueltig -> 3 */
    Firmware_Init();
    CHECK(LADEZEIT == 3 && ZEIT == 500, "LADEZEIT 0 -> 3");
    parameter_standard();
    sim_eeprom[3] = 99;            /* ungueltig -> 3 */
    Firmware_Init();
    CHECK(LADEZEIT == 3 && ZEIT == 500, "LADEZEIT 99 -> 3");
}

static void test_normalbetrieb_1000(void)
{
    uint32_t periode = 15000;
    uint32_t ziel;
    int i, n;

    printf("== Normalbetrieb 1000 1/min: Vorzuendung vor OT ==\n");
    start_standard();

    impulse_starten(periode, 100);
    fahre(11 * periode);            /* Mess- und Startphase abarbeiten */
    ereignisse_loeschen();
    fahre(20 * periode);

    CHECK(DREHZAHL == 1000, "Drehzahl 1000 (%lu)", (unsigned long)DREHZAHL);
    CHECK(SOLLWINKEL == soll_vorwinkel(1000), "Vorwinkel linear: %lu Grad",
          (unsigned long)SOLLWINKEL);
    ziel = winkel_ticks(periode, SOLLWINKEL);

    n = funken_zahl();
    CHECK(n >= 19 && n <= 20, "je Umdrehung genau ein Funke (%d in 20)", n);
    CHECK(ladungen_zahl() == n, "je Funke genau eine Ladung (%d/%d)",
          ladungen_zahl(), n);

    for (i = 1; i < 15; i++)
    {
        uint32_t soll = kante_zeit[i] - ziel;      /* Funke VOR dem OT */
        CHECK(funke_zeit(i - 1) == soll,
              "Funke %d bei OT-%lu (ist %lu, soll %lu)", i, (unsigned long)ziel,
              (unsigned long)funke_zeit(i - 1), (unsigned long)soll);
        CHECK(soll - ladung_zeit(i - 1) == ZEIT,
              "volle Ladezeit vor dem Funken %d (%lu)", i,
              (unsigned long)(soll - ladung_zeit(i - 1)));
    }
}

static void test_6000_umin_3ms(void)
{
    uint32_t periode = 2500;        /* 10 ms = 6000 1/min */
    uint32_t ziel;
    int i;

    printf("== 6000 1/min, 3 ms Ladezeit (Basisfall, keine Vor-OT-Ladung noetig) ==\n");
    parameter_standard();
    sim_eeprom[3] = 5;              /* 3 ms */
    Firmware_Init();
    pruefstand_reset();

    impulse_starten(periode, 100);
    fahre(12 * periode);
    ereignisse_loeschen();
    fahre(10 * periode);

    CHECK(ZEIT == 750, "Ladezeit 3 ms = 750 Ticks (%u)", ZEIT);
    CHECK(DREHZAHL == 6000, "Drehzahl 6000 (%lu)", (unsigned long)DREHZAHL);
    CHECK(SOLLWINKEL == 20, "ueber DREHZ_W voller Vorwinkel 20 Grad (%lu)",
          (unsigned long)SOLLWINKEL);
    ziel = winkel_ticks(periode, 20);          /* 139 Ticks */
    for (i = 1; i < 8; i++)
    {
        uint32_t soll = kante_zeit[i] - ziel;
        CHECK(funke_zeit(i - 1) == soll, "Funke %d bei OT-%lu (ist %lu)", i,
              (unsigned long)ziel, (unsigned long)funke_zeit(i - 1));
        CHECK(ladung_zeit(i - 1) > kante_zeit[i - 1],
              "Ladung %d liegt hinter dem vorigen OT: %lu > %lu", i,
              (unsigned long)ladung_zeit(i - 1), (unsigned long)kante_zeit[i - 1]);
        CHECK(soll - ladung_zeit(i - 1) == 750, "volle 3 ms Ladezeit (%lu)",
              (unsigned long)(soll - ladung_zeit(i - 1)));
    }
}

static void test_lange_periode_mehrere_umlaeufe(void)
{
    uint32_t periode = 150000;      /* 600 ms = 100 1/min, > 2 Timer-Umlaeufe */
    uint32_t ziel;
    int i;

    printf("== 100 1/min: Periode ueber mehrere 16-Bit-Umlaeufe ==\n");
    start_standard();

    impulse_starten(periode, 100);
    fahre(6 * periode);
    CHECK(DREHZAHL == 100, "Drehzahl 100 (%lu)", (unsigned long)DREHZAHL);
    CHECK(TIMER_WERT == periode, "Periode 150000 Ticks (%lu)",
          (unsigned long)TIMER_WERT);
    CHECK(timer1_hoch >= 13, "Timer-Ueberlaeufe gezaehlt (%u)", timer1_hoch);

    ziel = winkel_ticks(periode, START_WINKEL);
    CHECK(funken_zahl() == 2, "zwei Startfunken bisher (%d)", funken_zahl());
    for (i = 0; i < funken_zahl(); i++)
        CHECK(funke_zeit(i) == kante_zeit[start_kante_index(ziel) + i] + ziel,
              "Funke %d trotz Umlauf punktgenau (ist %lu, soll %lu)", i + 1,
              (unsigned long)funke_zeit(i),
              (unsigned long)(kante_zeit[start_kante_index(ziel) + i] + ziel));
}

static void test_32bit_umlauf(void)
{
    uint32_t periode = 15000;
    int i, n;

    printf("== 32-Bit-Zeitstempel laeuft waehrend des Betriebs um ==\n");
    start_standard();

    /* Firmware- und Modelluhr gemeinsam kurz vor den 32-Bit-Umlauf setzen */
    timer1_hoch = 0xFFFF;
    sim_timer1_basis = sim_now_ticks - 0xFFFF0000u;
    CHECK(sim_zeitstempel() == 0xFFFF0000u, "Modelluhr gesetzt (%lu)",
          (unsigned long)sim_zeitstempel());
    CHECK(ZeitJetzt() == 0xFFFF0000u, "Firmwareuhr gleich (%lu)",
          (unsigned long)ZeitJetzt());

    impulse_starten(periode, 100);
    fahre(12 * periode);            /* laeuft ueber 0xFFFFFFFF hinweg */
    n = funken_zahl();
    CHECK(n >= 5, "Funken auch ueber den 32-Bit-Umlauf (%d)", n);
    for (i = 0; i < 5 && i < n; i++)
        CHECK(funke_zeit(i) == (uint32_t)(kante_zeit[start_kante_index(
                                              winkel_ticks(periode, START_WINKEL)) + i] +
                                          winkel_ticks(periode, START_WINKEL)),
              "Funke %d ueber den Umlauf punktgenau (%lu)", i + 1,
              (unsigned long)funke_zeit(i));
    CHECK(sim_zeitstempel() < 0x10000000u, "Umlauf hat stattgefunden (%lu)",
          (unsigned long)sim_zeitstempel());
}

static void test_kohaerentes_timerlesen(void)
{
    uint32_t t, gelesen;

    printf("== Kohaerentes 32-Bit-Lesen bei Umlauf zwischen den Zugriffen ==\n");
    start_standard();
    fahre(100);

    /* Zeit so legen, dass das Low-Byte zwischen zwei Lesezugriffen umlaeuft */
    while ((sim_zeitstempel() & 0xFFu) != 0xFEu)
        fahre(1);
    t = sim_zeitstempel();
    sim_tmr1_lese_versatz = 4;      /* 4 kuenstliche Ticks zwischen den Lesungen */
    gelesen = ZeitJetzt();
    CHECK(gelesen >= t && gelesen <= t + 4,
          "Zeitstempel bleibt kohaerent (ist %lu, erwartet %lu..%lu)",
          (unsigned long)gelesen, (unsigned long)t, (unsigned long)(t + 4));
    sim_tmr1_lese_versatz = 0;
}

static void test_langsame_hauptschleife(void)
{
    uint32_t periode = 15000;
    uint32_t ziel;
    int i;

    printf("== Traege Hauptschleife: Termine kommen aus der Hardware ==\n");
    start_standard();
    schleifen_takt = 250;           /* 1 ms je Hauptschleifendurchlauf */

    impulse_starten(periode, 100);
    fahre(12 * periode);
    ziel = winkel_ticks(periode, START_WINKEL);
    CHECK(funken_zahl() >= 5, "Startfunken trotz traeger Schleife (%d)",
          funken_zahl());
    for (i = 0; i < 5; i++)
        CHECK(funke_zeit(i) == kante_zeit[5 + i] + ziel,
              "Funke %d punktgenau trotz 1 ms Schleifenzeit (ist %lu, soll %lu)",
              i + 1, (unsigned long)funke_zeit(i),
              (unsigned long)(kante_zeit[5 + i] + ziel));
    for (i = 0; i < ev_n; i++)
        if (ev_typ[i] == 1 && i + 1 < ev_n)
            CHECK(ev_zeit[i + 1] - ev_zeit[i] <= 1000u,
                  "Ladedauer begrenzt (%lu Ticks)",
                  (unsigned long)(ev_zeit[i + 1] - ev_zeit[i]));
    schleifen_takt = 1;
}

static void test_beschleunigung(void)
{
    uint32_t periode = 15000;
    int i;

    printf("== Beschleunigung und Verzoegerung ==\n");
    start_standard();

    impulse_starten(periode, 100);
    fahre(12 * periode);
    ereignisse_loeschen();

    /* Periode schrittweise verkuerzen und wieder verlaengern */
    for (i = 0; i < 8; i++)
    {
        kante_periode = periode;
        fahre(periode);
        periode = periode - periode / 8u;         /* ca. +14 % Drehzahl */
    }
    for (i = 0; i < 8; i++)
    {
        kante_periode = periode;
        fahre(periode);
        periode = periode + periode / 8u;
    }
    /* 16 Umdrehungen mit +/- 14 % Periodenaenderung: hoechstens die erste
       beschleunigte Umdrehung darf ausfallen (dort fehlt noch der Trend). */
    CHECK(funken_zahl() >= 15, "auch bei Drehzahlaenderung Funken (%d)",
          funken_zahl());
    CHECK(FEHLTERMIN == 0, "keine verpassten Termine (%u)", FEHLTERMIN);
    CHECK(ladungen_zahl() == funken_zahl(),
          "keine Ladung ohne Funken (%d/%d)", ladungen_zahl(), funken_zahl());
    CHECK(LADUNG_AKTIV == 0 || FUNKE_ARMIERT == 1,
          "keine Ladung ohne armierten Abschalttermin");
    for (i = 0; i + 1 < ev_n; i++)
        if (ev_typ[i] == 1 && ev_typ[i + 1] == 0)
            CHECK(ev_zeit[i + 1] - ev_zeit[i] <= 1000u,
                  "Ladedauer bleibt unter 4 ms (%lu)",
                  (unsigned long)(ev_zeit[i + 1] - ev_zeit[i]));
}

static void test_stoerimpuls(void)
{
    uint32_t periode = 15000;
    uint32_t t_vor;

    printf("== Stoerimpuls: Referenz bleibt erhalten ==\n");
    start_standard();

    impulse_starten(periode, 100);
    fahre(12 * periode);
    t_vor = TIMER_WERT;

    /* zusaetzliche Flanke 100 Ticks nach der letzten: unplausibel kurz */
    impulse_stoppen();
    fahre(100);
    sim_kante();
    fahre(10);
    CHECK(TIMER_WERT == t_vor, "Stoerimpuls aendert die Periode nicht (%lu)",
          (unsigned long)TIMER_WERT);
    CHECK(KANTE_GUELTIG == 1, "Referenz bleibt gueltig");
}

static void test_begrenzer(void)
{
    uint32_t periode = 1250;        /* 5 ms = 12000 1/min */

    printf("== Drehzahlbegrenzer bei 12000 1/min ==\n");
    start_standard();

    impulse_starten(periode, 100);
    fahre(12 * periode);
    CHECK(DREHZAHL == 12000, "Drehzahl 12000 ungekappt gemessen (%lu)",
          (unsigned long)DREHZAHL);

    ereignisse_loeschen();
    fahre(10 * periode);
    CHECK(funken_zahl() == 0 && ladungen_zahl() == 0,
          "Begrenzer: keine Ladung, kein Funke (%d/%d)",
          ladungen_zahl(), funken_zahl());
    CHECK(LADUNG_ARMIERT == 0 && PIE2bits.CCP2IE == 0,
          "Ladetermin entwaffnet");
    CHECK(COIL == 0, "COIL aus");

    /* Rueckfall unter den Begrenzer */
    kante_periode = 5000;           /* 3000 1/min */
    ereignisse_loeschen();
    fahre(4 * 5000);
    CHECK(DREHZAHL == 3000, "Drehzahl 3000 nach Rueckfall (%lu)",
          (unsigned long)DREHZAHL);
    CHECK(funken_zahl() >= 2, "Zuendung nach Rueckfall wieder aktiv (%d)",
          funken_zahl());
    CHECK(ladungen_zahl() == funken_zahl(), "Ladung und Funke paarig");
}

static void test_stehengebliebener_compare(void)
{
    uint32_t periode = 15000;

    printf("== Kein Funke aus stehengebliebenem Compare ==\n");
    start_standard();

    impulse_starten(periode, 100);
    fahre(12 * periode);
    impulse_stoppen();

    /* Sensor weg: es darf kein weiterer Funke aus alten Compare-Werten
       entstehen, auch nicht nach einem vollen 16-Bit-Umlauf. */
    fahre(3000);                    /* laufenden Zyklus zu Ende bringen */
    pruefstand_reset();
    fahre(70000);                   /* mehr als ein 16-Bit-Umlauf */
    CHECK(funken_zahl() == 0, "kein Funke aus altem Compare (%d)", funken_zahl());
    CHECK(ladungen_zahl() == 0, "keine Ladung aus altem Compare (%d)",
          ladungen_zahl());
}

static void test_sensorverlust_und_neustart(void)
{
    uint32_t periode = 15000;
    uint32_t ein = 0xFFFFFFFFu, aus = 0xFFFFFFFFu;
    int i;

    printf("== Sensorverlust waehrend der Ladung, danach Neustart ==\n");
    start_standard();

    impulse_starten(periode, 100);
    fahre(6 * periode);             /* erste Ladung/Funke sicher aktiv */
    /* Impulse mitten in der Startphase abbrechen */
    impulse_stoppen();
    pruefstand_reset();

    fahre(600000);                  /* 2,4 s Stille */
    for (i = 0; i < ev_n; i++)
    {
        if (ev_typ[i] == 1 && ein == 0xFFFFFFFFu) ein = ev_zeit[i];
        if (ev_typ[i] == 0 && aus == 0xFFFFFFFFu && ein != 0xFFFFFFFFu)
            aus = ev_zeit[i];
    }
    CHECK(COIL == 0, "COIL nach Sensorausfall aus");
    if (ein != 0xFFFFFFFFu && aus != 0xFFFFFFFFu)
        CHECK(aus - ein <= 1250u,
              "Ladedauer bleibt unter 5 ms, nicht 2 s (%lu Ticks)",
              (unsigned long)(aus - ein));
    CHECK(DREHZAHL == 0 && OT_1 == 0 && STARTFUNKE == 0,
          "Betrieb zurueckgesetzt (DREHZAHL=%lu OT_1=%u STARTFUNKE=%u)",
          (unsigned long)DREHZAHL, OT_1, STARTFUNKE);
    CHECK(KANTE_GUELTIG == 0, "Referenz verworfen");
    CHECK(LADUNG_ARMIERT == 0, "kein Ladetermin mehr armiert");

    /* Neustart: wieder fuenf Messkanten und fuenf Startfunken */
    pruefstand_reset();
    impulse_starten(periode, 100);
    fahre(5 * periode + 200);
    CHECK(funken_zahl() == 0, "nach Neustart erst wieder messen (%d)",
          funken_zahl());
    fahre(4 * periode + winkel_ticks(periode, START_WINKEL) + 200);
    CHECK(funken_zahl() == 5, "nach Neustart genau 5 Startfunken (%d)",
          funken_zahl());
}

static void test_gleichzeitige_ereignisse(void)
{
    uint32_t periode = 15000;
    uint32_t ziel, t;
    int n_vor;

    printf("== Gleichzeitige Ereignisse: OT, CCP1, CCP2, Ueberlauf ==\n");
    start_standard();
    ziel = winkel_ticks(periode, START_WINKEL);

    impulse_starten(periode, 100);
    fahre(12 * periode);
    impulse_stoppen();

    /* Kante genau auf den Funkenzeitpunkt legen */
    n_vor = funken_zahl();
    t = FUNKENZEITPUNKT;
    if (LADUNG_ARMIERT)
        t = LADUNGSBEGINN;
    while (sim_zeitstempel() < t)
        fahre(1);
    sim_kante();                    /* OT exakt auf dem Compare-Termin */
    fahre(2 * periode);
    CHECK(OT_VERLOREN == 0, "kein OT-Ereignis verloren");
    CHECK(funken_zahl() > n_vor, "Zuendung laeuft weiter (%d nach %d)",
          funken_zahl(), n_vor);
    CHECK(TIMER_WERT > 0 && KANTE_GUELTIG, "Messung intakt");
    CHECK(ladungen_zahl() == funken_zahl() ||
          ladungen_zahl() == funken_zahl() + 1,
          "keine doppelten Ladungen (%d/%d)", ladungen_zahl(), funken_zahl());
    (void)ziel;
}

/*--------------------------------------------------------------------------
  UART / EEPROM
--------------------------------------------------------------------------*/
static void rahmen_senden(const uint8_t *b, uint32_t abstand)
{
    int i;
    for (i = 0; i < 11; i++)
    {
        sim_sende(b[i]);
        fahre(abstand);
    }
}

static void test_uart_empfang_bei_lauf(void)
{
    static const uint8_t rahmen[11] = {
        'S', 15, 30, 4, 5000 >> 8, 5000 & 0xFF, 8000 >> 8, 8000 & 0xFF, 5, 9, 13
    };
    uint32_t periode = 15000;
    int f_vor;

    printf("== UART-Rahmen bei laufender Zuendung ==\n");
    start_standard();
    impulse_starten(periode, 100);
    fahre(12 * periode);
    ereignisse_loeschen();

    rahmen_senden(rahmen, 260);     /* ca. 1,04 ms je Byte bei 9600 8N1 */
    fahre(20000);
    CHECK(START_WINKEL == 15, "START_WINKEL = 15 (%u)", START_WINKEL);
    CHECK(VOR_W == 30, "VOR_W = 30 (%u)", VOR_W);
    CHECK(LADEZEIT == 4 && ZEIT == 625, "LADEZEIT = 4 (%u/%u)", LADEZEIT, ZEIT);
    CHECK(DREHZ_W == 5000, "DREHZ_W = 5000 (%u)", DREHZ_W);
    CHECK(DREHZ_MAX == 8000, "DREHZ_MAX = 8000 (%u)", DREHZ_MAX);
    CHECK(WERT_1 == 5 && WERT_2 == 9 && WERT_3 == 13, "LED-Werte");

    f_vor = funken_zahl();
    CHECK(f_vor >= 1, "Zuendung waehrend des Empfangs aktiv (%d)", f_vor);

    /* EEPROM-Maschine im Hintergrund, Zuendung darf nicht stehen bleiben */
    ereignisse_loeschen();
    fahre(3 * periode);             /* deutlich laenger als 10 EEPROM-Zyklen */
    CHECK(!EE_PENDING, "EEPROM-Maschine fertig");
    CHECK(sim_eeprom[1] == 15 && sim_eeprom[2] == 30 && sim_eeprom[3] == 4,
          "EEPROM: Startwinkel/Vorwinkel/Ladezeit (%u/%u/%u)",
          sim_eeprom[1], sim_eeprom[2], sim_eeprom[3]);
    CHECK(sim_eeprom[4] == (5000 >> 8) && sim_eeprom[5] == (5000 & 0xFF),
          "EEPROM Big-Endian DREHZ_W");
    CHECK(sim_eeprom[6] == (8000 >> 8) && sim_eeprom[7] == (8000 & 0xFF),
          "EEPROM Big-Endian DREHZ_MAX");
    CHECK(sim_eeprom[8] == 5 && sim_eeprom[9] == 9 && sim_eeprom[10] == 13,
          "EEPROM LED-Werte");
    CHECK(funken_zahl() >= 3, "Zuendung laeuft waehrend der EEPROM-Schreibungen (%d)",
          funken_zahl());
}

static void test_quittung_nichtblockierend(void)
{
    static const uint8_t rahmen[11] = {
        'S', 12, 18, 3, 4000 >> 8, 4000 & 0xFF, 9000 >> 8, 9000 & 0xFF, 4, 8, 12
    };
    uint32_t periode = 15000;

    printf("== Quittung blockiert die Zuendung nicht ==\n");
    start_standard();
    impulse_starten(periode, 100);
    fahre(12 * periode);
    ereignisse_loeschen();

    rahmen_senden(rahmen, 260);
    CHECK(QUITTUNG_AKTIV == 1 && LED1 == 1, "Quittungs-LED laeuft");
    fahre(2 * periode);
    CHECK(funken_zahl() >= 2, "waehrend der Quittung weiter gezuendet (%d)",
          funken_zahl());
    CHECK(QUITTUNG_AKTIV == 1, "Quittung laeuft nach 120 ms noch");
    fahre(2 * periode);             /* zusammen ueber 200 ms */
    CHECK(QUITTUNG_AKTIV == 0 && LED1 == 0, "Quittung nach 200 ms beendet");
}

static void test_uart_zweiter_rahmen_waehrend_ee(void)
{
    static const uint8_t r1[11] = {
        'S', 11, 21, 2, 3000 >> 8, 3000 & 0xFF, 7000 >> 8, 7000 & 0xFF, 3, 7, 11
    };
    static const uint8_t r2[11] = {
        'S', 13, 23, 5, 5000 >> 8, 5000 & 0xFF, 9000 >> 8, 9000 & 0xFF, 6, 10, 14
    };

    printf("== Zweiter Rahmen waehrend laufender EEPROM-Schreibung ==\n");
    start_standard();
    rahmen_senden(r1, 260);
    fahre(2 * SIM_EE_DAUER_TICKS);  /* Schreiben laeuft noch */
    CHECK(EE_PENDING == 1, "EEPROM-Maschine laeuft");
    rahmen_senden(r2, 260);
    fahre(25 * SIM_EE_DAUER_TICKS);
    CHECK(!EE_PENDING, "EEPROM-Maschine fertig");

    /* Kein gemischter Satz: alle zehn Bytes stammen aus r2 */
    CHECK(sim_eeprom[1] == 13, "EE Startwinkel aus dem zweiten Rahmen (%u)",
          sim_eeprom[1]);
    CHECK(sim_eeprom[2] == 23, "EE Vorwinkel (%u)", sim_eeprom[2]);
    CHECK(sim_eeprom[3] == 5, "EE Ladezeit (%u)", sim_eeprom[3]);
    CHECK(sim_eeprom[4] == (5000 >> 8) && sim_eeprom[5] == (5000 & 0xFF),
          "EE DREHZ_W (%u/%u)", sim_eeprom[4], sim_eeprom[5]);
    CHECK(sim_eeprom[6] == (9000 >> 8) && sim_eeprom[7] == (9000 & 0xFF),
          "EE DREHZ_MAX (%u/%u)", sim_eeprom[6], sim_eeprom[7]);
    CHECK(sim_eeprom[8] == 6 && sim_eeprom[9] == 10 && sim_eeprom[10] == 14,
          "EE LED-Werte (%u/%u/%u)", sim_eeprom[8], sim_eeprom[9], sim_eeprom[10]);
    CHECK(START_WINKEL == 13 && VOR_W == 23 && LADEZEIT == 5,
          "RAM aus dem zweiten Rahmen");
}

static void test_uart_dauerlast(void)
{
    static const uint8_t r[11] = {
        'S', 10, 20, 3, 4000 >> 8, 4000 & 0xFF, 10000 >> 8, 10000 & 0xFF, 4, 8, 12
    };
    uint32_t periode = 15000;
    int k;

    printf("== Dauerhafter UART-Verkehr bei laufender Zuendung ==\n");
    start_standard();
    impulse_starten(periode, 100);
    fahre(12 * periode);
    ereignisse_loeschen();
    kante_n = 0;

    for (k = 0; k < 6; k++)
        rahmen_senden(r, 260);
    fahre(3 * periode);
    CHECK(funken_zahl() >= 3, "Zuendung laeuft unter Dauerlast (%d)",
          funken_zahl());
    CHECK(ladungen_zahl() == funken_zahl(), "Ladung/Funke paarig (%d/%d)",
          ladungen_zahl(), funken_zahl());
    CHECK(START_WINKEL == 10 && VOR_W == 20, "Parameter unveraendert gueltig");
}

static void test_uart_ferr_oerr_timeout(void)
{
    static const uint8_t r[11] = {
        'S', 15, 30, 4, 5000 >> 8, 5000 & 0xFF, 8000 >> 8, 8000 & 0xFF, 5, 9, 13
    };

    printf("== UART: Framing-Fehler, Ueberlauf, Rahmen-Timeout ==\n");
    start_standard();

    /* 1. FERR mitten im Rahmen: der Rahmen wird verworfen */
    sim_sende(r[0]);
    fahre(260);
    sim_sende_ferr(r[1]);
    fahre(260);
    CHECK(RAHMEN_AKTIV == 0, "Rahmen nach Framing-Fehler verworfen");
    CHECK(START_WINKEL == 10, "Parameter unveraendert (%u)", START_WINKEL);

    /* 2. Rahmen-Timeout nach 8 ms */
    sim_sende('S');
    fahre(260);
    sim_sende(12);
    fahre(260);
    CHECK(RAHMEN_AKTIV == 1, "Rahmen laeuft");
    fahre(2100);
    CHECK(RAHMEN_AKTIV == 0, "Rahmen nach 8 ms verworfen");
    CHECK(START_WINKEL == 10, "Parameter unveraendert (%u)", START_WINKEL);

    /* 3. OERR: Puffer laeuft ueber, Empfang wird neu gestartet */
    sim_sende('S');
    sim_sende(1);
    sim_sende(2);                   /* drittes Byte -> OERR */
    CHECK(RCSTAbits.OERR == 1, "OERR gesetzt");
    fahre(10);
    CHECK(RCSTAbits.OERR == 0, "OERR quittiert");
    CHECK(RCSTAbits.CREN == 1, "CREN wieder gesetzt");
    CHECK(RAHMEN_AKTIV == 0, "Rahmen nach OERR verworfen");

    /* 4. danach wieder ein vollstaendiger Rahmen */
    rahmen_senden(r, 260);
    fahre(20 * SIM_EE_DAUER_TICKS);
    CHECK(START_WINKEL == 15 && VOR_W == 30, "Rahmen nach Fehlern angenommen");
}

static void test_parameter_klemmen(void)
{
    static const uint8_t rahmen[11] = {
        'S', 99, 77, 9, 0xFF, 0xFF, 0x00, 0x01, 55, 66, 77
    };

    printf("== Ungueltige Parameter werden VOR dem Ablegen begrenzt ==\n");
    start_standard();
    rahmen_senden(rahmen, 260);
    fahre(20 * SIM_EE_DAUER_TICKS);

    CHECK(START_WINKEL == 10, "START_WINKEL 99 -> 10 (%u)", START_WINKEL);
    CHECK(VOR_W == 40, "VOR_W 77 -> 40 (%u)", VOR_W);
    CHECK(LADEZEIT == 3 && ZEIT == 500, "LADEZEIT 9 -> 3 (%u)", LADEZEIT);
    CHECK(DREHZ_W == 4000, "DREHZ_W 65535 -> 4000 (%u)", DREHZ_W);
    CHECK(DREHZ_MAX == 10000, "DREHZ_MAX 1 -> 10000 (%u)", DREHZ_MAX);
    CHECK(WERT_1 == 4 && WERT_2 == 8 && WERT_3 == 12, "LED-Werte begrenzt");

    /* entscheidend: im EEPROM stehen die GEPRUEFTEN Werte */
    CHECK(sim_eeprom[1] == 10, "EEPROM Startwinkel begrenzt (%u)", sim_eeprom[1]);
    CHECK(sim_eeprom[2] == 40, "EEPROM Vorwinkel begrenzt (%u)", sim_eeprom[2]);
    CHECK(sim_eeprom[3] == 3, "EEPROM Ladezeit begrenzt (%u)", sim_eeprom[3]);
    CHECK(sim_eeprom[4] == (4000 >> 8) && sim_eeprom[5] == (4000 & 0xFF),
          "EEPROM DREHZ_W begrenzt (%u/%u)", sim_eeprom[4], sim_eeprom[5]);
    CHECK(sim_eeprom[6] == (10000 >> 8) && sim_eeprom[7] == (10000 & 0xFF),
          "EEPROM DREHZ_MAX begrenzt (%u/%u)", sim_eeprom[6], sim_eeprom[7]);
}

static void test_ee_gie(void)
{
    printf("== EESchreibe sichert und stellt GIE wieder her ==\n");
    start_standard();

    INTCONbits.GIE = 0;
    EEDATA = 42;
    EEADRL = 20;
    EESchreibe();
    CHECK(INTCONbits.GIE == 0, "GIE bleibt 0");
    sim_schritt(SIM_EE_DAUER_TICKS + 10);
    CHECK(sim_eeprom[20] == 42, "Byte geschrieben (%u)", sim_eeprom[20]);

    INTCONbits.GIE = 1;
    EEDATA = 43;
    EEADRL = 21;
    EESchreibe();
    CHECK(INTCONbits.GIE == 1, "GIE bleibt 1");
    sim_schritt(SIM_EE_DAUER_TICKS + 10);
    CHECK(sim_eeprom[21] == 43, "Byte 2 geschrieben (%u)", sim_eeprom[21]);
}

int main(void)
{
    test_init_und_zeitbasis();
    test_erste_kanten_und_periode();
    test_fuenf_messkanten_fuenf_startfunken();
    test_startwinkel_varianten();
    test_ladezeit_endpunkte();
    test_normalbetrieb_1000();
    test_6000_umin_3ms();
    test_lange_periode_mehrere_umlaeufe();
    test_32bit_umlauf();
    test_kohaerentes_timerlesen();
    test_langsame_hauptschleife();
    test_beschleunigung();
    test_stoerimpuls();
    test_begrenzer();
    test_stehengebliebener_compare();
    test_sensorverlust_und_neustart();
    test_gleichzeitige_ereignisse();
    test_uart_empfang_bei_lauf();
    test_quittung_nichtblockierend();
    test_uart_zweiter_rahmen_waehrend_ee();
    test_uart_dauerlast();
    test_uart_ferr_oerr_timeout();
    test_parameter_klemmen();
    test_ee_gie();

    if (fehler == 0)
        printf("\nALLE TESTS BESTANDEN\n");
    else
        printf("\n%d FEHLER\n", fehler);
    return fehler ? 1 : 0;
}
