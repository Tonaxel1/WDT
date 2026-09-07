/*********************************************************************************
 * Hostseitige Tests der Zündtiming-Logik aus My_Spark_1_2026.c (PR #2,
 * a5816aff3cbf1918c1dc0c664b16825252c03890, mit der in dieser Änderung
 * eingeführten Korrektur in SensorEreignis()).
 *
 * Diese Datei bindet den Produktionscode unverändert per #include ein
 * (HOST_TEST schaltet nur die SFR-Header/den main()-Rahmen um, siehe
 * My_Spark_1_2026.c und tests/host_pic_shim.h) und treibt ihn über ein
 * Register-/Zeitmodell (tests/sim_harness.h). Getestet wird damit der
 * tatsächlich produktiv verwendete Code, kein unabhängiges Ersatzmodell.
 *
 * WICHTIG: Diese Hosttests ersetzen weder eine Zielübersetzung für den
 * PIC16F1827 mit HI-TECH C/XC8 noch eine Hardwaremessung. Siehe README.md.
 *********************************************************************************/
#define HOST_TEST
#include "../My_Spark_1_2026.c"
#include "sim_harness.h"
#include "test_common.h"

#define MAX_EVENTS 32

typedef struct {
    unsigned long start_zeit;
    unsigned long ende_zeit;
    unsigned char abgeschlossen;
} funken_ereignis_t;

static funken_ereignis_t ereignisse[MAX_EVENTS];
static int ereignis_zahl;
static unsigned char coil_vorher;

static void funken_logger(unsigned long zeit)
{
    if (LATA4 && !coil_vorher) {
        if (ereignis_zahl < MAX_EVENTS) {
            ereignisse[ereignis_zahl].start_zeit = zeit;
            ereignisse[ereignis_zahl].abgeschlossen = 0;
        }
    } else if (!LATA4 && coil_vorher) {
        if (ereignis_zahl < MAX_EVENTS) {
            ereignisse[ereignis_zahl].ende_zeit = zeit;
            ereignisse[ereignis_zahl].abgeschlossen = 1;
            ereignis_zahl++;
        }
    }
    coil_vorher = LATA4;
}

static void logger_start(void)
{
    ereignis_zahl = 0;
    coil_vorher = 0;
    sim_tick_hook = funken_logger;
}

/*
 * Testfall 1: Synchronisationsimpuls und Impulse 1-8 bei konstanter Periode.
 * Erwartet werden genau vier Startfunken (Impulse 1-4, OT_1 2..5), danach ein
 * Funke direkt im Anschluss an Startfunke 5 (ISR, Übergabe an
 * NormalenFunkenPlanen) sowie je ein Normalfunke vor jedem weiteren Impuls
 * (5-8) -- ohne Ausfall und ohne Doppelfunke. Alle Ladezeiten müssen exakt
 * ZEIT-Ticks betragen (Pulsbreite), nicht nur die Anzahl.
 */
static void test_start_und_normalbetrieb(void)
{
    const unsigned int periode = 2000u; /* konstante Periode, DREHZAHL=7500 < DREHZ_MAX */
    int i;

    sim_reset(10u, 40u, 3u, 4000u, 11000u);
    logger_start();

    sim_sensor_edge(isr);           /* Synchronisationsimpuls */
    CHECK(synchronisiert == 1u, "Synchronisation nach erstem Impuls gesetzt");
    CHECK(OT_1 == 1u, "OT_1 nach Synchronisation auf 1");

    for (i = 1; i <= 8; i++) {
        sim_advance(isr, periode);
        sim_sensor_edge(isr);
    }
    sim_advance(isr, periode);       /* letzten geplanten Funken abschließen lassen */

    CHECK(ereignis_zahl == 9, "9 Zündereignisse: 4 Startfunken + 5 Normalfunken");
    for (i = 0; i < ereignis_zahl; i++) {
        CHECK(ereignisse[i].abgeschlossen, "jedes Zündereignis wurde korrekt beendet");
        CHECK((ereignisse[i].ende_zeit - ereignisse[i].start_zeit) == ZEIT,
              "Ladezeit entspricht exakt ZEIT (Pulsbreite, kein hängender/verkürzter Funke)");
    }
    /* Kein Doppelfunke: monoton steigende, nicht überlappende Zeitfenster. */
    for (i = 1; i < ereignis_zahl; i++)
        CHECK(ereignisse[i].start_zeit > ereignisse[i - 1].ende_zeit,
              "keine überlappenden/doppelten Zündereignisse");
}

/*
 * Testfall 2: Sensorflanke während wartendem Ladebeginn (zuend_zustand ==
 * NORMAL_LADEN, Compare für Ladebeginn ist bereits programmiert, aber noch
 * nicht ausgelöst). Nach der Korrektur darf Timer1 dabei NICHT verändert
 * werden, da sonst der bereits programmierte CCPR1-Termin seine Zeitbasis
 * verliert (siehe Kommentar in SensorEreignis()).
 */
static void test_kollision_waehrend_ladebeginn_wartet(void)
{
    unsigned int termin_vorher;

    sim_reset(10u, 40u, 3u, 4000u, 11000u);
    synchronisiert = 1;
    OT_1 = 5u;
    timer_wert = 2000u;
    CHECK(NormalenFunkenPlanen(), "Normalfunken erfolgreich geplant");
    CHECK(zuend_zustand == NORMAL_LADEN, "Zustand ist NORMAL_LADEN (wartet auf Ladebeginn)");
    termin_vorher = sim_ccpr1_get();

    sim_advance(isr, 300u);          /* deutlich vor dem programmierten Ladebeginn */
    CHECK(sim_tmr1_get() == 300u, "Timer1 läuft normal bis zur Kollision");

    sim_sensor_edge(isr);            /* kollidierender Sensorimpuls */

    CHECK(zuend_zustand == NORMAL_LADEN, "Zustand bleibt unangetastet bei Kollision");
    CHECK(sim_tmr1_get() == 300u,
          "Timer1 wird durch kollidierenden Sensorimpuls NICHT zurückgesetzt");
    CHECK(sim_ccpr1_get() == termin_vorher,
          "programmierter Ladebeginn-Termin bleibt unverändert");

    logger_start();
    sim_advance(isr, (unsigned int)(normal_termin - 300u + ZEIT + 10u));
    CHECK(ereignis_zahl == 1, "Ladung/Zündung findet trotz Kollision genau einmal statt");
    CHECK(ereignisse[0].start_zeit == normal_termin,
          "Ladebeginn erfolgt exakt zum ursprünglich geplanten Termin (keine Verschiebung)");
    CHECK((ereignisse[0].ende_zeit - ereignisse[0].start_zeit) == ZEIT,
          "Ladezeit bleibt exakt auf ZEIT begrenzt");
}

/*
 * Testfall 3: Sensorflanke während aktiver Ladung (zuend_zustand ==
 * NORMAL_ABSCHALTEN, Spule ist bereits an, Abschalttermin programmiert).
 * Dies ist der im Auftrag beschriebene Haupt-Verdachtsfall: Ein Reset von
 * Timer1 hier würde den Abschalttermin um bis zu eine volle Timer1-Periode
 * verzögern ("hängende Spule").
 */
static void test_kollision_waehrend_aktiver_ladung(void)
{
    unsigned int abschalt_termin_vorher;

    sim_reset(10u, 40u, 3u, 4000u, 11000u);
    synchronisiert = 1;
    OT_1 = 5u;
    timer_wert = 2000u;
    CHECK(NormalenFunkenPlanen(), "Normalfunken geplant");
    sim_advance(isr, normal_termin); /* Ladebeginn ausgelöst: Spule ist jetzt an */
    CHECK(zuend_zustand == NORMAL_ABSCHALTEN, "Zustand ist NORMAL_ABSCHALTEN (Spule lädt aktiv)");
    CHECK(LATA4 == 1u, "Spule (COIL) ist während der Ladung eingeschaltet");
    abschalt_termin_vorher = sim_ccpr1_get();

    sim_advance(isr, 200u);          /* mitten in der aktiven Ladung */
    sim_sensor_edge(isr);            /* kollidierender Sensorimpuls */

    CHECK(zuend_zustand == NORMAL_ABSCHALTEN, "Ladung läuft unterbrechungsfrei weiter");
    CHECK(LATA4 == 1u, "Spule bleibt während der Kollision an (kein Aussetzer)");
    CHECK(sim_ccpr1_get() == abschalt_termin_vorher,
          "Abschalttermin bleibt durch die Kollision unverändert");
    CHECK(sim_tmr1_get() == (unsigned int)(normal_termin + 200u),
          "Timer1 zählt trotz Kollision unverändert weiter (keine Zeitbasis-Verschiebung)");

    logger_start();
    sim_advance(isr, ZEIT + 10u);
    CHECK(ereignis_zahl == 1, "Spule schaltet trotz Kollision genau einmal ab");
    CHECK(ereignisse[0].ende_zeit == (unsigned long)normal_termin + ZEIT,
          "Abschalten erfolgt exakt ZEIT Ticks nach Ladebeginn, nicht verzögert");
}

/*
 * Testfall 4: Sensorimpuls (IOC) und Compare-Abschalten (CCP1) im selben
 * simulierten Takt. Laut ISR-Reihenfolge wird IOC vor CCP behandelt; die
 * Korrektur muss dabei verhindern, dass SensorEreignis() den Timer
 * zurücksetzt, bevor der laufende Abschaltvorgang im selben ISR-Aufruf
 * behandelt wurde.
 */
static void test_gleichzeitig_ioc_und_compare_abschalten(void)
{
    sim_reset(10u, 40u, 3u, 4000u, 11000u);
    synchronisiert = 1;
    OT_1 = 5u;
    timer_wert = 2000u;
    CHECK(NormalenFunkenPlanen(), "Normalfunken geplant");
    sim_advance(isr, normal_termin);           /* Ladebeginn */
    CHECK(zuend_zustand == NORMAL_ABSCHALTEN, "Ladung aktiv");

    sim_advance(isr, (unsigned int)(ZEIT - 1u)); /* bis knapp vor den Abschalttermin */

    /* Einen Tick vor dem Abschalttermin: IOC-Flag von Hand setzen, dann den
     * letzten Tick simulieren, der CCP1IF auslöst - beide Flags sind dann im
     * selben ISR-Aufruf aktiv. */
    IOCBFbits.IOCBF4 = 1;
    INTCONbits.IOCIF = 1;
    sim_advance(isr, 1u);

    CHECK(zuend_zustand == ZUEND_AUS, "Abschalten wird trotz gleichzeitigem IOC korrekt ausgeführt");
    CHECK(LATA4 == 0u, "Spule ist nach dem gleichzeitigen Ereignis sicher aus");
}

/*
 * Testfall 5: Timer1-Überlauf (Signalverlust) muss die Zündung sicher
 * abschalten und Synchronisation/Drehzahl zurücksetzen (Stillstand()),
 * unabhängig vom Kollisionsschutz aus SensorEreignis().
 */
static void test_timer_overflow_signalverlust(void)
{
    sim_reset(10u, 40u, 3u, 4000u, 11000u);
    synchronisiert = 1;
    OT_1 = 5u;
    timer_wert = 2000u;
    CHECK(NormalenFunkenPlanen(), "Normalfunken geplant");
    sim_advance(isr, normal_termin);
    CHECK(LATA4 == 1u, "Spule lädt aktiv, dann bleibt Signal aus");

    sim_advance(isr, 65536u - (unsigned int)normal_termin); /* voller Timer1-Überlauf ohne Impuls */

    CHECK(synchronisiert == 0u, "Stillstand() setzt Synchronisation zurück");
    CHECK(zuend_zustand == ZUEND_AUS, "Stillstand() erzwingt ZUEND_AUS");
    CHECK(LATA4 == 0u, "Stillstand() schaltet die Spule sicher aus (kein hängender Funke)");
    CHECK(DREHZAHL == 0u, "Stillstand() setzt die Drehzahl zurück");
}

/*
 * Testfall 6: Bereits abgelaufene bzw. zu knappe Compare-Termine
 * (TICKS_GUARD) müssen konservativ verworfen werden statt einen falschen
 * (zu späten oder sofortigen) Interrupt zu programmieren.
 */
static void test_ticks_guard_grenzfaelle(void)
{
    unsigned int jetzt = 1000u;

    sim_reset(10u, 40u, 3u, 4000u, 11000u);
    CHECK(CompareSetzen((unsigned int)(jetzt - 1u), jetzt) == 0,
          "bereits abgelaufener Termin wird verworfen");
    CHECK(CompareSetzen(jetzt, jetzt) == 0, "Termin == jetzt wird verworfen");
    CHECK(CompareSetzen((unsigned int)(jetzt + TICKS_GUARD - 1u), jetzt) == 0,
          "Termin knapp unterhalb TICKS_GUARD wird verworfen");
    CHECK(CompareSetzen((unsigned int)(jetzt + TICKS_GUARD), jetzt) == 1,
          "Termin bei genau TICKS_GUARD wird akzeptiert");
}

/*
 * Testfall 7: Drehzahlbegrenzer verhindert neue Zündplanung oberhalb
 * DREHZ_MAX, sowohl in der Startphase als auch im Normalbetrieb.
 */
static void test_drehzahlbegrenzer(void)
{
    sim_reset(10u, 40u, 3u, 4000u, 5000u); /* DREHZ_MAX niedrig gewählt */

    synchronisiert = 1;
    OT_1 = 5u;
    timer_wert = 2000u;              /* DREHZAHL = 7500 > DREHZ_MAX(5000) */
    DREHZAHL = 7500u;
    CHECK(NormalenFunkenPlanen() == 0, "Normalfunken wird oberhalb DREHZ_MAX verworfen");
    CHECK(zuend_zustand == ZUEND_AUS, "kein Zustandswechsel oberhalb Drehzahlbegrenzer");

    /* Startphase: gleiche Periode (DREHZAHL=7500 > DREHZ_MAX) über den echten
     * SensorEreignis()-Pfad, nicht nur über die manuell gesetzte Variable. */
    sim_reset(10u, 40u, 3u, 4000u, 5000u);
    synchronisiert = 1;
    OT_1 = 1u;
    logger_start();
    sim_advance(isr, 2000u);
    sim_sensor_edge(isr);
    CHECK(OT_1 == 2u, "Impulszähler läuft auch oberhalb des Limiters weiter");
    CHECK(ereignis_zahl == 0, "kein Startfunke oberhalb Drehzahlbegrenzer");
}

/*
 * Testfall 8: Startwinkel 0, klein und 20 (laut EinstellungenGueltig maximal
 * gültiger Wert) ergeben einen gültigen, durch TICKS_GUARD nach unten
 * begrenzten Ladebeginn.
 */
static void test_startwinkel_grenzwerte(void)
{
    unsigned char winkel[3];
    int i;

    winkel[0] = 0u;
    winkel[1] = 1u;
    winkel[2] = 20u;

    for (i = 0; i < 3; i++) {
        unsigned int start_termin;

        sim_reset(winkel[i], 40u, 3u, 4000u, 11000u);
        synchronisiert = 1;
        OT_1 = 1u;
        start_termin = (unsigned int)(((unsigned long)2000u * winkel[i]) / 360UL);
        if (start_termin < TICKS_GUARD)
            start_termin = TICKS_GUARD;

        logger_start();
        sim_advance(isr, 2000u);      /* Periode, aus der der Startwinkel berechnet wird */
        sim_sensor_edge(isr);
        sim_advance(isr, start_termin + ZEIT + 10u);
        CHECK(ereignis_zahl == 1, "genau ein Startfunke je Startwinkel-Grenzwert");
        if (ereignis_zahl > 0)
            CHECK(ereignisse[0].start_zeit == (unsigned long)2000u + start_termin,
                  "Ladebeginn entspricht Startwinkel bzw. TICKS_GUARD-Untergrenze");
    }
}

/*
 * Testfall 9: kurzes Ladefenster / Ladezeitgrenzen (LADEZEIT 1..5 -> ZEIT
 * 250..750 Ticks) und Periodenänderung: Ein Normalfunke, dessen Ladebeginn
 * ohne die Vorlauf-Prüfung vor den Referenzimpuls fallen würde, wird
 * verworfen statt falsch getimt ausgeführt.
 */
static void test_kurzes_ladefenster_wird_verworfen(void)
{
    sim_reset(10u, 40u, 5u, 4000u, 11000u); /* LADEZEIT=5 -> ZEIT=750 (langes Ladefenster) */
    synchronisiert = 1;
    OT_1 = 5u;
    timer_wert = 100u; /* sehr kurze Periode: Vorlauf+ZEIT >= Periode */
    CHECK(NormalenFunkenPlanen() == 0,
          "zu kurzes Ladefenster wird verworfen statt falsch getimt zu zünden");
    CHECK(zuend_zustand == ZUEND_AUS, "kein Zustandswechsel bei verworfenem Ladefenster");
}

/*
 * Testfall 10: Übergang vom letzten Startfunken (Impuls 4, OT_1 5) zum
 * Normalfunken vor Impuls 5 ("6. Puls" in Nutzerzählung: Sync=1,
 * Start 1-4=2..5, dieser Funke=6). Bei langer Ladezeit (LADEZEIT=5,
 * ZEIT=750) und großem Vorwinkel (VOR_W=40) kann der winkelbasierte
 * Ladebeginn für diesen Funken bereits vergangen sein, wenn der ISR-Aufruf
 * für das Abschalten von Startfunke 4 (und damit NormalenFunkenPlanen())
 * erfolgt - hier bei Periode 1700 der Fall (idealer Ladebeginn 761 Ticks,
 * aber Startfunke 4 schaltet erst bei 844 Ticks ab). Der Rückfall in
 * NormalenFunkenPlanen() muss diesen Funken dann sofort (mit
 * TICKS_GUARD-Abstand nach dem Abschalten von Startfunke 4) mit voller
 * Ladezeit ZEIT laden, OHNE mit dem Startfunken zu überlappen und OHNE
 * nach dem angenommenen nächsten Referenzimpuls zu zünden (später als
 * ideal, nie früher). Referenzimpulse 4 und 5 (Sensorflanken-Zählung)
 * entsprechen dabei genau den Zündereignis-Indizes 3 (letzter Startfunke)
 * und 4 (Rückfall-Normalfunke) sowie 5 (erster regulär vor Impuls 6
 * geplanter Normalfunke) unten.
 */
static void test_uebergang_rueckfall_bei_spaetem_idealtermin(void)
{
    const unsigned int periode = 1700u;
    const unsigned char start_winkel = 20u, vor_w = 40u, ladezeit = 5u;
    const unsigned int dreh_w = 4000u, dreh_max = 11000u;
    unsigned long letzter_start_termin, letzter_start_ende;
    unsigned long ideal_vor_ticks, ideal_start_termin;
    unsigned long ref_impuls4, ref_impuls5;
    int i;

    sim_reset(start_winkel, vor_w, ladezeit, dreh_w, dreh_max);
    logger_start();

    sim_sensor_edge(isr);            /* Synchronisationsimpuls */
    for (i = 1; i <= 5; i++) {
        sim_advance(isr, periode);
        sim_sensor_edge(isr);
    }
    sim_advance(isr, periode);        /* letzten geplanten Funken abschließen lassen */

    CHECK(synchronisiert == 1u, "Synchronisation bleibt über den Übergang hinweg erhalten");
    CHECK(OT_1 == 5u, "OT_1 verbleibt nach Erreichen von 5 auf 5 (kein Stillstand)");
    CHECK(ereignis_zahl == 6,
          "6 Zündereignisse: 4 Startfunken + Rückfall-Normalfunke + 1 regulärer Normalfunke "
          "(kein Ausfall des Übergangsfunkens trotz spätem Idealtermin)");

    for (i = 0; i < ereignis_zahl; i++) {
        CHECK(ereignisse[i].abgeschlossen, "jedes Zündereignis wurde korrekt beendet");
        CHECK((ereignisse[i].ende_zeit - ereignisse[i].start_zeit) == ZEIT,
              "volle Ladezeit ZEIT auch beim Rückfall-Funken, keine Verkürzung");
    }
    for (i = 1; i < ereignis_zahl; i++)
        CHECK(ereignisse[i].start_zeit > ereignisse[i - 1].ende_zeit,
              "kein Überlappen des Rückfall-Funkens mit dem vorherigen Startfunken");

    /* Referenzimpuls 4 (Sensorflanken-Zählung, Sync=Impuls 1): letzter
     * Startfunke (Zündereignis-Index 3, "Impuls 5" in OT_1-Zählung). */
    ref_impuls4 = 4UL * periode;
    letzter_start_termin = (unsigned long)((periode * start_winkel) / 360UL);
    if (letzter_start_termin < TICKS_GUARD)
        letzter_start_termin = TICKS_GUARD;
    letzter_start_ende = ref_impuls4 + letzter_start_termin + ZEIT;
    CHECK(ereignisse[3].start_zeit == ref_impuls4 + letzter_start_termin,
          "Startfunke 4 beginnt exakt beim Startwinkel-Termin vor Referenzimpuls 4");

    /* Idealer (winkelbasierter) Ladebeginn des Übergangsfunkens - liegt
     * hier bereits vor dem Abschalten von Startfunke 4. */
    ideal_vor_ticks = ((unsigned long)periode * vor_w + 180UL) / 360UL;
    ideal_start_termin = ref_impuls4 + periode - ideal_vor_ticks - ZEIT;
    CHECK(ideal_start_termin < letzter_start_ende,
          "Testvoraussetzung: idealer Ladebeginn liegt vor Abschalten von Startfunke 4 "
          "(genau der Fall, den der Rückfall behandeln muss)");

    /* Rückfall-Normalfunke (Zündereignis-Index 4, unmittelbar nach Impuls
     * 4/Startfunke 4, "6. Puls" in Nutzerzählung): beginnt exakt
     * TICKS_GUARD nach Abschalten von Startfunke 4, nie früher als das. */
    CHECK(ereignisse[4].start_zeit == letzter_start_ende + TICKS_GUARD,
          "Rückfall-Funke beginnt exakt TICKS_GUARD nach Abschalten des letzten Startfunkens");
    CHECK(ereignisse[4].start_zeit > ideal_start_termin,
          "Rückfall-Funke zündet später als der (hier unerreichbare) Idealtermin, nie früher");
    CHECK(ereignisse[4].ende_zeit + TICKS_GUARD <= ref_impuls4 + periode,
          "Rückfall-Funke endet mit Sicherheitsabstand vor dem angenommenen Referenzimpuls 5");

    /* Referenzimpuls 5: regulärer, wieder ideal getimter Normalfunke vor
     * Referenzimpuls 6 (Zündereignis-Index 5). */
    ref_impuls5 = ref_impuls4 + periode;
    CHECK(ereignisse[5].start_zeit == ref_impuls5 + ideal_start_termin - ref_impuls4,
          "erster regulärer Normalfunke nach dem Übergang wieder exakt idealer Vorwinkel-Termin");
}

/*
 * Testfall 11: Physisch unvermeidbarer Ausfall des Übergangsfunkens
 * ("6. Puls") bei zu kurzer Periode für die eingestellte Ladezeit/den
 * Vorwinkel - konkrete Grenzfall-Reproduktion aus der Aufgabenstellung
 * (Periode 1500, START_WINKEL=20, VOR_W=40, LADEZEIT=5 -> ZEIT=750,
 * DREHZ_W=4000, 10000 U/min unterhalb DREHZ_MAX=11000). Hier reicht selbst
 * der sofortige Rückfall aus Testfall 10 nicht mehr aus: Das Abschalten
 * von Startfunke 4 (bei 833 Ticks) liegt bereits so spät, dass selbst eine
 * sofort danach beginnende volle Ladezeit ZEIT vor dem angenommenen
 * nächsten Referenzimpuls (1500 Ticks) nicht mehr sicher Platz hat. Dieser
 * eine Funke bleibt daher bewusst aus - NICHT durch verkürzte Ladezeit,
 * Überlappung oder Zündung nach dem Referenzimpuls erzwungen (siehe
 * NormalenFunkenPlanen()). Alle übrigen Impulse (insbesondere ab
 * Referenzimpuls 5) müssen weiterhin exakt und ohne Doppelzündung
 * getimt werden; OT_1/Synchronisation dürfen dadurch nicht gestört werden.
 *
 * WICHTIG: Dies ist eine gültige, aber elektrisch extreme
 * Einstellungskombination (Ladezeit + Vorwinkel beanspruchen zusammen
 * einen Großteil der Periode); ob genau dieser Fall dem vom Nutzer am
 * Oszilloskop beobachteten Bild entspricht, ist NICHT bewiesen - siehe
 * README.md.
 */
static void test_uebergang_unvermeidbarer_ausfall_bei_ueberlanger_ladezeit(void)
{
    const unsigned int periode = 1500u;
    const unsigned char start_winkel = 20u, vor_w = 40u, ladezeit = 5u;
    const unsigned int dreh_w = 4000u, dreh_max = 11000u;
    unsigned long letzter_start_termin, letzter_start_ende;
    unsigned long ref_impuls4, ref_impuls5, ref_impuls6;
    unsigned long ideal_vor_ticks, ideal_start_termin;
    int i;

    sim_reset(start_winkel, vor_w, ladezeit, dreh_w, dreh_max);
    logger_start();

    sim_sensor_edge(isr);
    for (i = 1; i <= 8; i++) {
        sim_advance(isr, periode);
        sim_sensor_edge(isr);
    }
    sim_advance(isr, periode);

    CHECK(synchronisiert == 1u, "Synchronisation bleibt trotz ausgefallenem Übergangsfunken erhalten");
    CHECK(OT_1 == 5u, "OT_1 bleibt auf 5 (kein Stillstand, kein Signalverlust ausgelöst)");
    CHECK(ereignis_zahl == 8,
          "genau 8 statt 9 Zündereignisse: 4 Startfunken + 4 Normalfunken - der eine, "
          "physisch nicht unterzubringende Übergangsfunke ('6. Puls') bleibt bewusst aus");

    for (i = 0; i < ereignis_zahl; i++) {
        CHECK(ereignisse[i].abgeschlossen, "jedes verbleibende Zündereignis wurde korrekt beendet");
        CHECK((ereignisse[i].ende_zeit - ereignisse[i].start_zeit) == ZEIT,
              "volle Ladezeit ZEIT bei allen verbleibenden Funken (keine Verkürzung)");
    }
    for (i = 1; i < ereignis_zahl; i++)
        CHECK(ereignisse[i].start_zeit > ereignisse[i - 1].ende_zeit,
              "keine überlappenden/doppelten Zündereignisse trotz ausgefallenem Übergangsfunken");

    ref_impuls4 = 4UL * periode;
    letzter_start_termin = (unsigned long)((periode * start_winkel) / 360UL);
    if (letzter_start_termin < TICKS_GUARD)
        letzter_start_termin = TICKS_GUARD;
    letzter_start_ende = ref_impuls4 + letzter_start_termin + ZEIT;
    CHECK(ereignisse[3].ende_zeit == letzter_start_ende,
          "letzter Startfunke (Index 3) schaltet exakt nach ZEIT Ticks ab");

    /* Auch der sofortige Rückfall (Testfall 10) hätte hier keinen Platz
     * mehr vor dem Referenzimpuls - das ist die Voraussetzung, die diesen
     * Testfall von Testfall 10 unterscheidet. */
    CHECK((letzter_start_ende + TICKS_GUARD + ZEIT + TICKS_GUARD) >= (ref_impuls4 + periode),
          "Testvoraussetzung: selbst der sofortige Rückfall passt nicht mehr vor Referenzimpuls 5");

    ideal_vor_ticks = ((unsigned long)periode * vor_w + 180UL) / 360UL;
    ideal_start_termin = periode - ideal_vor_ticks - ZEIT;

    /* Kein Zündereignis zwischen dem Abschalten von Startfunke 4 und
     * Referenzimpuls 5 (Index 4 ist bereits der reguläre Normalfunke vor
     * Referenzimpuls 6, nicht mehr der ausgefallene vor Referenzimpuls 5). */
    ref_impuls5 = ref_impuls4 + periode;
    ref_impuls6 = ref_impuls5 + periode;
    CHECK(ereignisse[4].start_zeit == ref_impuls5 + ideal_start_termin,
          "erster nach dem Ausfall verbleibender Normalfunke ist exakt vor Referenzimpuls 6 "
          "geplant (nicht der ausgefallene vor Referenzimpuls 5)");
    CHECK(ereignisse[4].start_zeit > ref_impuls5,
          "kein nachgeholter Funke unmittelbar bei/vor Referenzimpuls 5 selbst");
    CHECK(ereignisse[5].start_zeit == ref_impuls6 + ideal_start_termin,
          "Normalfunke vor Referenzimpuls 7 wieder exakt idealer Vorwinkel-Termin "
          "(Timing erholt sich vollständig, kein dauerhafter Phasenfehler)");
}

int main(void)
{
    test_start_und_normalbetrieb();
    test_kollision_waehrend_ladebeginn_wartet();
    test_kollision_waehrend_aktiver_ladung();
    test_gleichzeitig_ioc_und_compare_abschalten();
    test_timer_overflow_signalverlust();
    test_ticks_guard_grenzfaelle();
    test_drehzahlbegrenzer();
    test_startwinkel_grenzwerte();
    test_kurzes_ladefenster_wird_verworfen();
    test_uebergang_rueckfall_bei_spaetem_idealtermin();
    test_uebergang_unvermeidbarer_ausfall_bei_ueberlanger_ladezeit();

    if (test_fehler == 0) {
        printf("Alle Tests erfolgreich (Hostmodell, kein Zielcompiler/keine Hardware).\n");
        return 0;
    }
    printf("%d Testfehlschlag(e).\n", test_fehler);
    return 1;
}
