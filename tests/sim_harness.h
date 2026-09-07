/*********************************************************************************
 * Timer1-/CCP-Zeitmodell für Hosttests.
 *
 * Bildet exakt das nach, was der PIC16F1827 laut Datenblatt an dieser Stelle
 * tut: Timer1 zählt (falls TMR1ON) jeden Takt hoch, setzt TMR1IF beim
 * Überlauf 0xFFFF->0x0000 und setzt CCP1IF, sobald der 16-Bit-Zählerstand mit
 * CCPR1H:CCPR1L übereinstimmt (Compare-Modus 0b1010, „nur Interrupt“, siehe
 * My_Spark_1_2026.c main()). Beides geschieht unabhängig vom jeweiligen
 * Interrupt-Enable-Bit, genau wie auf echter Hardware; ob der zugehörige
 * ISR-Zweig etwas tut, entscheidet weiterhin PIE1bits.*IE im Produktionscode.
 *
 * Dies ersetzt keine Zielübersetzung/Hardwaremessung, siehe README.md.
 *********************************************************************************/
#ifndef SIM_HARNESS_H
#define SIM_HARNESS_H

#include "host_pic_shim.h"

typedef void (*isr_fn_t)(void);
typedef void (*tick_hook_t)(unsigned long sim_zeit);

static unsigned long sim_zeit;          /* Monotoner Zeitstempel, übersteht TMR1-Resets. */
static tick_hook_t sim_tick_hook;       /* Optionaler Beobachter je simuliertem Takt. */

static unsigned int sim_tmr1_get(void)
{
    return ((unsigned int)TMR1H << 8) | TMR1L;
}

static void sim_tmr1_set(unsigned int wert)
{
    TMR1H = (unsigned char)(wert >> 8);
    TMR1L = (unsigned char)wert;
}

static unsigned int sim_ccpr1_get(void)
{
    return ((unsigned int)CCPR1H << 8) | CCPR1L;
}

/* Arbeitet alle anstehenden, freigegebenen Interrupts ab (wie GIE=1 auf Hardware). */
static void sim_service(isr_fn_t isr_fn)
{
    unsigned char guard = 0;
    while (INTCONbits.GIE &&
           ((INTCONbits.PEIE && PIE1bits.CCP1IE && PIR1bits.CCP1IF) ||
            (INTCONbits.PEIE && PIE1bits.TMR1IE && PIR1bits.TMR1IF) ||
            (INTCONbits.IOCIE && INTCONbits.IOCIF))) {
        isr_fn();
        if (++guard > 20u)
            break;                  /* Endlosschleifenschutz für Testfälle. */
    }
}

/* Simuliert 'ticks' Timer1-Takte inklusive Overflow-/Compare-Hardwareverhalten. */
static void sim_advance(isr_fn_t isr_fn, unsigned int ticks)
{
    unsigned int i;

    for (i = 0; i < ticks; i++) {
        sim_zeit++;
        if (T1CONbits.TMR1ON) {
            unsigned int neu = (unsigned int)((sim_tmr1_get() + 1u) & 0xFFFFu);
            sim_tmr1_set(neu);
            if (neu == 0u)
                PIR1bits.TMR1IF = 1;
            if (neu == sim_ccpr1_get())
                PIR1bits.CCP1IF = 1;
        }
        sim_service(isr_fn);
        if (sim_tick_hook)
            sim_tick_hook(sim_zeit);
    }
}

/* Simuliert eine Sensorflanke (IOC an RB4) zum aktuellen Zeitpunkt. */
static void sim_sensor_edge(isr_fn_t isr_fn)
{
    IOCBFbits.IOCBF4 = 1;
    INTCONbits.IOCIF = 1;
    sim_service(isr_fn);
    if (sim_tick_hook)
        sim_tick_hook(sim_zeit);
}

#endif /* SIM_HARNESS_H */
