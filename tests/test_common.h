/*********************************************************************************
 * Gemeinsame Testhilfen. Muss NACH My_Spark_1_2026.c (HOST_TEST) und
 * sim_harness.h eingebunden werden.
 *********************************************************************************/
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>

static int test_fehler;

#define CHECK(bedingung, beschreibung) \
    do { \
        if (!(bedingung)) { \
            printf("FEHLGESCHLAGEN: %s (%s:%d)\n", (beschreibung), __FILE__, __LINE__); \
            test_fehler++; \
        } \
    } while (0)

/* Setzt Register und Produktionszustand wie nach main()-Initialisierung zurück. */
static void sim_reset(unsigned char start_winkel, unsigned char vor_w,
                      unsigned char lade, unsigned int dreh_w, unsigned int dreh_max)
{
    TMR1H = 0; TMR1L = 0;
    CCPR1H = 0; CCPR1L = 0;
    T1CONbits.TMR1ON = 1;
    PIE1bits.TMR1IE = 1;
    PIE1bits.CCP1IE = 0;
    PIR1bits.TMR1IF = 0;
    PIR1bits.CCP1IF = 0;
    INTCONbits.IOCIE = 1;
    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;
    INTCONbits.IOCIF = 0;
    IOCBFbits.IOCBF4 = 0;
    LATA4 = 0;   /* COIL */
    LATB3 = 0;   /* DREHZ */
    LATA1 = 0;   /* LED_2 */

    zuend_zustand = ZUEND_AUS;
    synchronisiert = 0;
    OT_1 = 0;
    DREHZAHL = 0;
    normal_termin = 0;
    timer_wert = 0;
    sim_zeit = 0;
    sim_tick_hook = 0;

    EinstellungenUebernehmen(start_winkel, vor_w, lade, dreh_w, dreh_max, 4u, 8u, 12u);
}

#endif /* TEST_COMMON_H */
