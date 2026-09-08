/*********************************************************************************
*        File: host_pic_shim.c
*        Zweck: Register-Shim + Zeitmodell fuer Host-Tests (gcc)
************************************************************************************/
#include "host_pic_shim.h"
#include <string.h>
#include <stdio.h>

uint8_t  LATA_REG, LATB_REG, TRISA_REG, TRISB_REG;
uint8_t  ANSELA_REG, ANSELB_REG, WPUB_REG, OPTION_REG_REG;
uint8_t  APFCON0_REG, APFCON1_REG, OSCCON_REG;
uint8_t  TXSTA_REG, RCSTA_REG, BAUDCON_REG, SPBRGH_REG, SPBRG_REG;
uint8_t  T1GCON_REG, T1CON_REG, CCP1CON_REG, CCP2CON_REG;
uint8_t  IOCBN_REG, IOCBF_REG, PIR1_REG, PIR2_REG, PIE1_REG, PIE2_REG, INTCON_REG;
uint8_t  TMR1L_REG, TMR1H_REG, CCPR1L_REG, CCPR1H_REG, CCPR2L_REG, CCPR2H_REG;
uint8_t  EEDATA_REG, EEADRL_REG, EECON1_REG, EECON2_REG;
uint8_t  RCREG_REG, TXREG_REG, TXIF_REG;

INTCONbits_t INTCONbits;
IOCBFbits_t  IOCBFbits;
IOCBNbits_t  IOCBNbits;
PIR1bits_t   PIR1bits;
PIR2bits_t   PIR2bits;
PIE1bits_t   PIE1bits;
PIE2bits_t   PIE2bits;
T1CONbits_t  T1CONbits;
RCSTAbits_t  RCSTAbits;
EECON1bits_t EECON1bits;

uint8_t sim_RB4, sim_LATA0, sim_LATA1, sim_LATA4, sim_LATB3, sim_LATB5, sim_LATB6, sim_LATB7;

uint32_t sim_now_ticks;
uint32_t sim_timer1_basis;
uint32_t sim_debug_step;
uint32_t sim_guard;
uint8_t  sim_eeprom[256];
uint32_t sim_eeprom_wr_rest;
uint32_t sim_delay_ticks;

/* Zugriffe aus der Firmware (sim_main.c) */
extern void isr(void);
extern void Firmware_Rumpf(void);

uint32_t sim_zeitstempel(void)
{
    return sim_now_ticks - sim_timer1_basis;
}

void host_register_sync(void)
{
    /* Byte-Register in Bitfelder spiegeln (Schreibrichtung Firmware -> Shim) */
    T1CONbits.TMR1ON = (T1CON_REG & 0x01) ? 1 : 0;
    PIR1bits.TMR1IF = (PIR1_REG & 0x01) ? 1 : 0;
    PIR1bits.RCIF   = (PIR1_REG & 0x04) ? 1 : 0;
    PIR1bits.CCP1IF = (PIR1_REG & 0x20) ? 1 : 0;
    PIR2bits.CCP2IF = (PIR2_REG & 0x01) ? 1 : 0;
    IOCBFbits.IOCBF4 = (IOCBF_REG & 0x10) ? 1 : 0;
}

void sim_timer_aktualisieren(void)
{
    uint32_t t;
    if (!T1CONbits.TMR1ON)
        return;
    t = sim_zeitstempel() & 0xFFFFu;
    TMR1L_REG = (uint8_t)(t & 0xFF);
    TMR1H_REG = (uint8_t)(t >> 8);
}

void __delay_us(unsigned long us)
{
    sim_delay_ticks += (us + 3u) / 4u;
}

void __delay_ms(unsigned long ms)
{
    sim_delay_ticks += ms * 250u;
}

void sim_ausgaenge_aktualisieren(void)
{
    /* LATA/LATB Bitnamen werden von Hand gespiegelt */
    sim_LATA0 = (uint8_t)((LATA_REG >> 0) & 1u);
    sim_LATA1 = (uint8_t)((LATA_REG >> 1) & 1u);
    sim_LATA4 = (uint8_t)((LATA_REG >> 4) & 1u);
    sim_LATB3 = (uint8_t)((LATB_REG >> 3) & 1u);
    sim_LATB5 = (uint8_t)((LATB_REG >> 5) & 1u);
    sim_LATB6 = (uint8_t)((LATB_REG >> 6) & 1u);
    sim_LATB7 = (uint8_t)((LATB_REG >> 7) & 1u);
}

void sim_kante(void)
{
    if (IOCBNbits.IOCBN4 && INTCONbits.IOCIE)
    {
        IOCBFbits.IOCBF4 = 1;
        INTCONbits.IOCIF = 1;
    }
}

void sim_sende(uint8_t b)
{
    RCREG_REG = b;
    PIR1bits.RCIF = 1;
}

static void sim_isr_bedienen(void)
{
    int aktiv = 0;
    int ioc;
    if (!INTCONbits.GIE)
        return;
    ioc = (INTCONbits.IOCIF && IOCBFbits.IOCBF4 && INTCONbits.IOCIE);
    if (INTCONbits.PEIE)
    {
        if (PIR1bits.TMR1IF && PIE1bits.TMR1IE) aktiv = 1;
        if (PIR1bits.CCP1IF && PIE1bits.CCP1IE) aktiv = 1;
        if (PIR2bits.CCP2IF && PIE2bits.CCP2IE) aktiv = 1;
    }
    if (ioc)
        aktiv = 1;
    if (!aktiv)
        return;
    isr();
    sim_ausgaenge_aktualisieren();
}

void sim_schritt(uint32_t ticks)
{
    while (ticks--)
    {
        uint32_t alt32;
        if (++sim_guard > 100000000u)
        {
            fprintf(stderr, "ABBRUCH: Endlosschleife bei Schritt %lu (now=%lu)\n",
                    (unsigned long)sim_debug_step, (unsigned long)sim_now_ticks);
            _Exit(3);
        }
        alt32 = sim_zeitstempel();
        sim_now_ticks++;

        if (T1CONbits.TMR1ON)
        {
            uint16_t alt = (uint16_t)(alt32 & 0xFFFFu);
            uint16_t neu = (uint16_t)(sim_zeitstempel() & 0xFFFFu);
            if (neu < alt)
                PIR1bits.TMR1IF = 1;
            if (!PIR1bits.CCP1IF)
            {
                uint16_t cmp = (uint16_t)CCPR1L_REG | ((uint16_t)CCPR1H_REG << 8);
                if (neu == cmp)
                    PIR1bits.CCP1IF = 1;
            }
            if (!PIR2bits.CCP2IF)
            {
                uint16_t cmp = (uint16_t)CCPR2L_REG | ((uint16_t)CCPR2H_REG << 8);
                if (neu == cmp)
                    PIR2bits.CCP2IF = 1;
            }
        }

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
        sim_timer_aktualisieren();
    }
}

void sim_hauptschleife(uint32_t durchlaeufe)
{
    while (durchlaeufe--)
    {
        Firmware_Rumpf();
        sim_ausgaenge_aktualisieren();
        sim_timer_aktualisieren();
        sim_schritt(1);
    }
}

void sim_eeprom_schreiben_starten(void)
{
    EECON1bits.WR = 1;
    sim_eeprom_wr_rest = SIM_EE_DAUER_TICKS;
}
