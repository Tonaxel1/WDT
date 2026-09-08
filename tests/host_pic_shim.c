/*********************************************************************************
*        File: host_pic_shim.c
*        Zweck: Register- und Zeitmodell fuer Host-Tests (gcc), siehe Header.
************************************************************************************/
#include "host_pic_shim.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

sim_reg8 sim_LATA, sim_LATB;

uint8_t  TRISA_REG, TRISB_REG;
uint8_t  ANSELA_REG, ANSELB_REG, WPUB_REG, OPTION_REG_REG;
uint8_t  APFCON0_REG, APFCON1_REG, OSCCON_REG;
uint8_t  TXSTA_REG, RCSTA_REG, BAUDCON_REG, SPBRGH_REG, SPBRG_REG;
uint8_t  T1GCON_REG, T1CON_REG, CCP1CON_REG, CCP2CON_REG;
uint8_t  IOCBN_REG, IOCBF_REG, PIR1_REG, PIR2_REG, PIE1_REG, PIE2_REG;
uint8_t  TMR1L_REG, TMR1H_REG, CCPR1L_REG, CCPR1H_REG, CCPR2L_REG, CCPR2H_REG;
uint8_t  EEDATA_REG, EEADRL_REG, EECON2_REG;
uint8_t  TXREG_REG, TXIF_REG;

INTCONbits_t INTCONbits;
IOCBFbits_t  IOCBFbits;
IOCBNbits_t  IOCBNbits;
PIR1bits_t   PIR1bits;
PIR2bits_t   PIR2bits;
PIE1bits_t   PIE1bits;
PIE2bits_t   PIE2bits;
RCSTAbits_t  RCSTAbits;
EECON1bits_t EECON1bits;

uint8_t sim_RB4;

uint32_t sim_now_ticks;
uint32_t sim_timer1_basis;
uint32_t sim_debug_step;
uint32_t sim_guard;
uint8_t  sim_eeprom[256];
uint32_t sim_eeprom_wr_rest;
uint32_t sim_delay_ticks;
uint32_t sim_tmr1_lese_versatz;
uint32_t sim_isr_zaehler;
int      sim_takt_fehler;
uint32_t sim_faktor = 1;

/* Empfangspuffer des EUSART: zwei Bytes (FIFO) wie im Datenblatt */
static uint8_t sim_rx_byte[2];
static uint8_t sim_rx_ferr[2];
static int     sim_rx_anzahl;

static int  sim_timer_laeuft;
static int  sim_in_isr;
static uint32_t sim_tmr1_offset;        // kuenstlicher Versatz beim Lesen

/* Zugriffe aus der Firmware */
extern void isr(void);
extern void SystemStart(void);
extern void HauptSchleife(void);

/*--------------------------------------------------------------------------
  Zeitbasis
--------------------------------------------------------------------------*/
uint32_t sim_zeitstempel(void)
{
    return sim_now_ticks - sim_timer1_basis;
}

/* Roher Timer1-Stand. sim_faktor bildet den in T1CON eingestellten
   Prescaler ab: bei 8 MHz und FOSC/4 liefert 1:8 genau einen Tick je
   4 us (Faktor 1), 1:2 laeuft viermal zu schnell (Faktor 4). */
uint64_t sim_timer_roh(void)
{
    return (uint64_t)(sim_zeitstempel() + sim_tmr1_offset) * (uint64_t)sim_faktor;
}

static void sim_takt_uebernehmen(void)
{
    uint8_t quelle = (uint8_t)((T1CON_REG >> 6) & 0x03u);
    uint8_t teiler = (uint8_t)((T1CON_REG >> 4) & 0x03u);
    sim_timer_laeuft = (T1CON_REG & 0x01u) ? 1 : 0;
    sim_takt_fehler = (quelle != 0) ? 1 : 0;      // nur FOSC/4 ist modelliert
    switch (teiler)
    {
        case 0: sim_faktor = 8; break;            // 1:1  -> 0,5 us je Schritt
        case 1: sim_faktor = 4; break;            // 1:2  -> 1 us
        case 2: sim_faktor = 2; break;            // 1:4  -> 2 us
        default: sim_faktor = 1; break;           // 1:8  -> 4 us (Sollzustand)
    }
}

/* Von der Firmware nach dem Beschreiben von T1CON/TMR1H/TMR1L aufgerufen. */
void host_register_sync(void)
{
    uint32_t stand;
    sim_takt_uebernehmen();
    stand = (uint32_t)TMR1L_REG | ((uint32_t)TMR1H_REG << 8);
    /* geschriebener Timerstand wird zur neuen Phase des freien Zaehlers */
    sim_timer1_basis = sim_now_ticks - (stand / (sim_faktor ? sim_faktor : 1));
    sim_tmr1_offset = 0;
    PIR1bits.TMR1IF = (PIR1_REG & 0x01u) ? 1 : 0;
    PIR1bits.RCIF   = (sim_rx_anzahl > 0) ? 1 : 0;
    PIR1bits.CCP1IF = (PIR1_REG & 0x20u) ? 1 : 0;
    PIR2bits.CCP2IF = (PIR2_REG & 0x01u) ? 1 : 0;
    IOCBFbits.IOCBF4 = (IOCBF_REG & 0x10u) ? 1 : 0;
    IOCBNbits.IOCBN4 = (IOCBN_REG & 0x10u) ? 1 : 0;
}

/* Timerlesen: liefert immer den aktuellen Stand. sim_tmr1_lese_versatz
   laesst gezielt Zeit zwischen zwei Lesezugriffen vergehen, damit der
   Umlauf zwischen High- und Low-Byte geprueft werden kann. */
static uint16_t sim_tmr1_16(void)
{
    if (!sim_timer_laeuft)
        return (uint16_t)((uint16_t)TMR1L_REG | ((uint16_t)TMR1H_REG << 8));
    return (uint16_t)(sim_timer_roh() & 0xFFFFu);
}

static void sim_lese_versatz(void)
{
    if (sim_tmr1_lese_versatz > 0)
    {
        sim_tmr1_lese_versatz--;
        sim_tmr1_offset++;
    }
}

uint8_t sim_tmr1l(void)
{
    uint8_t v = (uint8_t)(sim_tmr1_16() & 0xFFu);
    sim_lese_versatz();
    return v;
}

uint8_t sim_tmr1h(void)
{
    uint8_t v = (uint8_t)(sim_tmr1_16() >> 8);
    sim_lese_versatz();
    return v;
}

/*--------------------------------------------------------------------------
  EUSART
--------------------------------------------------------------------------*/
static void sim_rx_flags(void)
{
    PIR1bits.RCIF = (sim_rx_anzahl > 0) ? 1 : 0;
    RCSTAbits.FERR = (sim_rx_anzahl > 0) ? sim_rx_ferr[0] : 0;
}

static void sim_rx_ablegen(uint8_t b, uint8_t ferr)
{
    if (sim_rx_anzahl >= 2)
    {
        RCSTAbits.OERR = 1;                 // Puffer voll: Byte geht verloren
        return;
    }
    sim_rx_byte[sim_rx_anzahl] = b;
    sim_rx_ferr[sim_rx_anzahl] = ferr;
    sim_rx_anzahl++;
    sim_rx_flags();
}

void sim_sende(uint8_t b)      { sim_rx_ablegen(b, 0); }
void sim_sende_ferr(uint8_t b) { sim_rx_ablegen(b, 1); }

uint8_t sim_rcreg_lesen(void)
{
    uint8_t v;
    if (sim_rx_anzahl == 0)
        return 0;
    v = sim_rx_byte[0];
    sim_rx_byte[0] = sim_rx_byte[1];
    sim_rx_ferr[0] = sim_rx_ferr[1];
    sim_rx_anzahl--;
    sim_rx_flags();                          // FERR gehoert jetzt zum naechsten Byte
    return v;
}

uint8_t sim_ferr_lesen(void)
{
    return (uint8_t)RCSTAbits.FERR;
}

void sim_cren_neustart(void)
{
    RCSTAbits.OERR = 0;                      // CREN 0 -> 1 loescht OERR
    sim_rx_anzahl = 0;
    sim_rx_flags();
}

/*--------------------------------------------------------------------------
  Interrupt-Modell
--------------------------------------------------------------------------*/
static void sim_ioc_aktualisieren(void)
{
    /* IOCIF ist nur lesbar und folgt den IOCBF-Bits. */
    INTCONbits.IOCIF = IOCBFbits.IOCBF4 ? 1 : 0;
}

static void sim_isr_bedienen(void)
{
    int aktiv = 0;
    sim_ioc_aktualisieren();
    if (!INTCONbits.GIE || sim_in_isr)
        return;
    if (INTCONbits.PEIE)
    {
        if (PIR1bits.TMR1IF && PIE1bits.TMR1IE) aktiv = 1;
        if (PIR1bits.CCP1IF && PIE1bits.CCP1IE) aktiv = 1;
        if (PIR2bits.CCP2IF && PIE2bits.CCP2IE) aktiv = 1;
        if (PIR1bits.RCIF   && PIE1bits.RCIE)   aktiv = 1;
    }
    if (INTCONbits.IOCIF && INTCONbits.IOCIE)
        aktiv = 1;
    if (!aktiv)
        return;
    sim_in_isr = 1;
    sim_isr_zaehler++;
    isr();
    sim_in_isr = 0;
    sim_ioc_aktualisieren();
}

/* Compare-Treffer im Intervall (alt, neu] suchen. Bei korrektem Prescaler
   ist das genau ein Schritt; bei falschem Prescaler mehrere. */
static int sim_treffer(uint64_t alt, uint64_t neu, uint16_t cmp)
{
    uint64_t r;
    if (neu - alt >= 0x10000u)
        return 1;
    for (r = alt + 1; r <= neu; r++)
        if ((uint16_t)(r & 0xFFFFu) == cmp)
            return 1;
    return 0;
}

void sim_schritt(uint32_t ticks)
{
    while (ticks--)
    {
        uint64_t alt, neu;

        if (++sim_guard > 400000000u)
        {
            fprintf(stderr, "ABBRUCH: Endlosschleife bei Schritt %lu (now=%lu)\n",
                    (unsigned long)sim_debug_step, (unsigned long)sim_now_ticks);
            _Exit(3);
        }

        alt = sim_timer_roh();
        sim_now_ticks++;
        neu = sim_timer_roh();

        if (sim_timer_laeuft)
        {
            if ((neu >> 16) != (alt >> 16))
                PIR1bits.TMR1IF = 1;
            /* CCPxIF wird unabhaengig von CCPxIE gesetzt (Compare-Modus). */
            if (CCP1CON_REG == 0x0Au &&
                sim_treffer(alt, neu,
                            (uint16_t)((uint16_t)CCPR1L_REG | ((uint16_t)CCPR1H_REG << 8))))
                PIR1bits.CCP1IF = 1;
            if (CCP2CON_REG == 0x0Au &&
                sim_treffer(alt, neu,
                            (uint16_t)((uint16_t)CCPR2L_REG | ((uint16_t)CCPR2H_REG << 8))))
                PIR2bits.CCP2IF = 1;
        }

        /* EEPROM: EECON1.WR bleibt waehrend des Schreibens gesetzt. */
        if (EECON1bits.WR && sim_eeprom_wr_rest == 0)
            sim_eeprom_wr_rest = SIM_EE_DAUER_TICKS;
        if (sim_eeprom_wr_rest > 0)
        {
            sim_eeprom_wr_rest--;
            if (sim_eeprom_wr_rest == 0)
            {
                sim_eeprom[EEADRL_REG] = EEDATA_REG;
                EECON1bits.WR = 0;
            }
        }

        sim_isr_bedienen();
        TMR1L_REG = (uint8_t)(sim_tmr1_16() & 0xFFu);
        TMR1H_REG = (uint8_t)(sim_tmr1_16() >> 8);
    }
}

void __delay_us(unsigned long us)
{
    uint32_t t = (uint32_t)((us + 3u) / 4u);
    sim_delay_ticks += t;
    sim_schritt(t);
}

void __delay_ms(unsigned long ms)
{
    uint32_t t = (uint32_t)(ms * 250u);
    sim_delay_ticks += t;
    sim_schritt(t);
}

void sim_hauptschleife(uint32_t durchlaeufe)
{
    while (durchlaeufe--)
    {
        HauptSchleife();
        sim_schritt(1);
    }
}

void sim_kante(void)
{
    /* Fallende Flanke an RB4: IOCBF wird gesetzt, sobald IOCBN aktiv ist,
       unabhaengig von der Interruptfreigabe IOCIE. */
    sim_RB4 = 0;
    if (IOCBNbits.IOCBN4)
    {
        IOCBFbits.IOCBF4 = 1;
        sim_ioc_aktualisieren();
        sim_isr_bedienen();
    }
}

void sim_reset(void)
{
    memset(&sim_LATA, 0, sizeof(sim_LATA));
    memset(&sim_LATB, 0, sizeof(sim_LATB));
    memset(&INTCONbits, 0, sizeof(INTCONbits));
    memset(&IOCBFbits, 0, sizeof(IOCBFbits));
    memset(&IOCBNbits, 0, sizeof(IOCBNbits));
    memset(&PIR1bits, 0, sizeof(PIR1bits));
    memset(&PIR2bits, 0, sizeof(PIR2bits));
    memset(&PIE1bits, 0, sizeof(PIE1bits));
    memset(&PIE2bits, 0, sizeof(PIE2bits));
    memset(&RCSTAbits, 0, sizeof(RCSTAbits));
    memset(&EECON1bits, 0, sizeof(EECON1bits));
    T1CON_REG = 0; CCP1CON_REG = 0; CCP2CON_REG = 0;
    CCPR1L_REG = CCPR1H_REG = CCPR2L_REG = CCPR2H_REG = 0;
    TMR1L_REG = TMR1H_REG = 0;
    PIR1_REG = PIR2_REG = IOCBF_REG = IOCBN_REG = 0;
    sim_rx_anzahl = 0;
    sim_eeprom_wr_rest = 0;
    sim_delay_ticks = 0;
    sim_tmr1_lese_versatz = 0;
    sim_tmr1_offset = 0;
    sim_isr_zaehler = 0;
    sim_takt_fehler = 0;
    sim_faktor = 1;
    sim_timer_laeuft = 0;
    sim_in_isr = 0;
    sim_RB4 = 1;
    TXIF_REG = 1;                    // Sender dauerhaft bereit (Modellannahme)
    sim_timer1_basis = sim_now_ticks;
}
