/*********************************************************************************
 * Regressionstest: zeigt, dass der ungeprüfte Timer1-Reset in SensorEreignis()
 * aus PR #2 (a5816aff3cbf1918c1dc0c664b16825252c03890, Blob
 * ff745d72fbbac8514c517389a6511471ab8ddf04, Zeilen 128-133 vor dieser
 * Korrektur) bei einer Sensorflanke während einer laufenden Ladung/Zündung
 * den bereits programmierten Compare-Termin um seine Zeitbasis verschiebt.
 *
 * SensorEreignis_ALT() unten ist eine wörtliche Kopie der Funktion aus PR #2
 * vor dieser Korrektur (nur umbenannt). isr_alt() ist eine Kopie der
 * seinerzeitigen ISR, die SensorEreignis_ALT() statt der (bereits
 * korrigierten) SensorEreignis() aus My_Spark_1_2026.c aufruft; alle übrigen
 * Bausteine (CompareSetzen, NormalenFunkenPlanen, Timer1Lesen, Stillstand,
 * SpuleAus) sind der unveränderte Produktionscode.
 *
 * Dieser Test schlägt mit dem alten Verhalten (isr_alt) erwartungsgemäß fehl
 * und bestätigt damit den untersuchten Fehlerpfad; mit dem korrigierten
 * Produktionscode (isr) besteht dieselbe Prüfung (siehe
 * test_ignition_timing.c, Testfälle 2 und 3).
 *********************************************************************************/
#define HOST_TEST
#include "../My_Spark_1_2026.c"
#include "sim_harness.h"
#include "test_common.h"

static int bug_bestaetigt;

/* Prüft, dass eine dokumentierte Fehlerbedingung des alten Codepfads (PR #2)
 * tatsächlich eintritt. Zählt NICHT als Testfehler, sondern als bestätigten,
 * bereits behobenen Fehlerpfad. */
#define ERWARTE_BEKANNTEN_FEHLER(bedingung, beschreibung) \
    do { \
        if (bedingung) { \
            printf("BEKANNTER FEHLER BESTAETIGT (PR #2): %s\n", (beschreibung)); \
            bug_bestaetigt++; \
        } else { \
            printf("Hinweis: Fehlerbedingung trat in diesem Lauf nicht ein: %s\n", \
                   (beschreibung)); \
        } \
    } while (0)

/* Wörtliche Kopie von SensorEreignis() aus PR #2 vor dieser Korrektur. */
static void SensorEreignis_ALT(void)
{
    unsigned int start_termin;

    /* Der Zählerwert gehört immer zum Impulszeitpunkt, nie zu späterem Code. */
    T1CONbits.TMR1ON = 0;
    timer_wert = Timer1Lesen();
    TMR1H = 0;
    TMR1L = 0;
    PIR1bits.TMR1IF = 0;
    T1CONbits.TMR1ON = 1;
    if (!synchronisiert) {
        synchronisiert = 1;         /* Erste Teilperiode nach Stillstand verwerfen. */
        OT_1 = 1;
        return;
    }
    if (timer_wert == 0)
        return;

    DREHZAHL = 15000000UL / timer_wert;
    if (DREHZAHL > 12000UL)
        DREHZAHL = 12000UL;

    /*
     * Ein Abschalttermin wird niemals durch ein Sensorereignis überschrieben.
     * Bei Kollision wird der folgende Funken konservativ verworfen.
     */
    if (zuend_zustand != ZUEND_AUS)
        return;

    if (OT_1 < 5u) {
        OT_1++;
        if (DREHZAHL >= DREHZ_MAX)
            return;
        start_termin = (unsigned int)(((unsigned long)timer_wert *
                         START_WINKEL) / 360UL);
        if (start_termin < TICKS_GUARD)
            start_termin = TICKS_GUARD;
        if (CompareSetzen(start_termin, Timer1Lesen()))
            zuend_zustand = START_LADEN;
        return;
    }

    /* Ab Impuls 5 wird der Funken vor dem folgenden (sechsten) Impuls geplant. */
    NormalenFunkenPlanen();
}

/* Wörtliche Kopie der ISR aus PR #2, ruft SensorEreignis_ALT() statt der
 * korrigierten SensorEreignis() auf; alle übrigen Zweige unverändert. */
static void isr_alt(void)
{
    unsigned int jetzt;

    if (INTCONbits.IOCIF && IOCBFbits.IOCBF4) {
        SensorEreignis_ALT();
        IOCBFbits.IOCBF4 = 0;
        INTCONbits.IOCIF = 0;
    }

    if (PIR1bits.CCP1IF && PIE1bits.CCP1IE) {
        PIR1bits.CCP1IF = 0;
        jetzt = Timer1Lesen();
        if (zuend_zustand == START_LADEN) {
            COIL = 1;
            DREHZ = 1;
            LED_2 = 1;
            if (CompareSetzen((unsigned int)(jetzt + ZEIT), jetzt))
                zuend_zustand = START_ABSCHALTEN;
            else
                Stillstand();
        } else if (zuend_zustand == START_ABSCHALTEN) {
            SpuleAus();
            zuend_zustand = ZUEND_AUS;
            if (OT_1 >= 5u)
                NormalenFunkenPlanen();
        } else if (zuend_zustand == NORMAL_LADEN) {
            COIL = 1;
            DREHZ = 1;
            LED_2 = 1;
            if (CompareSetzen((unsigned int)(jetzt + ZEIT), jetzt))
                zuend_zustand = NORMAL_ABSCHALTEN;
            else
                Stillstand();
        } else if (zuend_zustand == NORMAL_ABSCHALTEN) {
            SpuleAus();
            PIE1bits.CCP1IE = 0;
            zuend_zustand = ZUEND_AUS;
        } else {
            PIE1bits.CCP1IE = 0;
        }
    }

    if (PIR1bits.TMR1IF && PIE1bits.TMR1IE) {
        PIR1bits.TMR1IF = 0;
        Stillstand();
    }
}

/*
 * Reproduziert den im Auftrag beschriebenen Verdachtsfall: Sensorimpuls
 * trifft ein, während die Spule bereits aktiv lädt (zuend_zustand ==
 * NORMAL_ABSCHALTEN, Abschalttermin bereits in CCPR1 programmiert).
 */
static void test_alt_kollision_haengt_termin_auf(void)
{
    unsigned int abschalt_termin_vorher;

    sim_reset(10u, 40u, 3u, 4000u, 11000u);
    synchronisiert = 1;
    OT_1 = 5u;
    timer_wert = 2000u;
    CHECK(NormalenFunkenPlanen(), "Normalfunken geplant (alter Codepfad)");
    sim_advance(isr_alt, normal_termin);       /* Ladebeginn: Spule an */
    CHECK(zuend_zustand == NORMAL_ABSCHALTEN, "Ladung aktiv (alter Codepfad)");
    abschalt_termin_vorher = sim_ccpr1_get();

    sim_advance(isr_alt, 200u);                /* mitten in der aktiven Ladung */
    sim_sensor_edge(isr_alt);                  /* kollidierender Sensorimpuls */

    /*
     * Erwartetes (fehlerhaftes) Verhalten von PR #2: Timer1 wird trotz
     * laufender Ladung auf 0 zurückgesetzt, CCPR1 bleibt unverändert stehen -
     * der Abschalttermin bezieht sich damit nicht mehr auf den aktuellen
     * Timer1-Nullpunkt und wird um bis zu eine volle Timer1-Periode
     * (~262 ms bei diesem Prescaler) verzögert, statt wie geplant nach ZEIT
     * Ticks zu erfolgen.
     */
    ERWARTE_BEKANNTEN_FEHLER(sim_tmr1_get() == 0u,
          "Timer1 wird bei Kollision waehrend laufender Ladung auf 0 zurueckgesetzt");
    ERWARTE_BEKANNTEN_FEHLER(sim_ccpr1_get() == abschalt_termin_vorher,
          "CCPR1 bleibt unveraendert stehen, bezieht sich aber nicht mehr auf Timer1==0");

    /* Nach ZEIT weiteren Ticks (relativ zur ORIGINAL geplanten Ladezeit)
     * müsste die Spule längst abgeschaltet haben - mit dem alten Code tut
     * sie das nicht, weil der Timer neu bei 0 zu zählen begonnen hat. */
    sim_advance(isr_alt, ZEIT);
    ERWARTE_BEKANNTEN_FEHLER(LATA4 == 1u,
          "Spule schaltet nach der urspruenglich geplanten Ladezeit NICHT ab (haengender Funke)");
}

int main(void)
{
    test_alt_kollision_haengt_termin_auf();

    if (test_fehler != 0) {
        printf("Unerwartet: Setup-Vorbedingungen des Regressionstests sind fehlgeschlagen.\n");
        return 1;
    }
    if (bug_bestaetigt == 0) {
        printf("Unerwartet: alter Codepfad (PR #2) hat die Regressionspruefung bestanden.\n");
        return 1;
    }
    printf("Regressionstest bestaetigt den bekannten PR-#2-Fehlerpfad: %d von 3 Symptomen\n"
           "bestaetigt (siehe test_ignition_timing.c fuer dieselbe Pruefung mit dem\n"
           "korrigierten Code, dort erwartungsgemaess ohne Fehlerbestaetigung).\n",
           bug_bestaetigt);
    return 0;
}

