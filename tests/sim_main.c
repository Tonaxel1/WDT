/*********************************************************************************
*        File: sim_main.c
*        Zweck: Simulations-Hauptprogramm fuer Host-Tests.
*        Spiegelt main() aus My_Spark_1_2026.c; Aenderungen an der
*        Firmware muessen hier nachgezogen werden. Validiert NICHT
*        die Ziel-Firmware, sondern nur das Steuer-Modell.
************************************************************************************/
#include "host_pic_shim.h"
#include <string.h>

#define TIMER_UEBERLAEUFE_MAX 120
#define ROT   sim_LATB5
#define GRUEN sim_LATB6
#define BLAU  sim_LATB7
#define LED_1 sim_LATA0
#define LED_2 sim_LATA1
#define COIL  sim_LATA4
#define DREHZ sim_LATB3

/* Firmware-Schnittstelle (aus My_Spark_1_2026.c) */
extern unsigned char OT_1, LADEZEIT, WERT_1, WERT_2, WERT_3, TEMP, TXWERT;
extern unsigned char STARTFUNKE, START_WINKEL;
extern unsigned int  DREHZ_W, VOR_W, DREHZ_MAX, ZEIT;
extern unsigned long TIMER_WERT, DREHZAHL, SOLLWINKEL;
extern unsigned char LADUNG_AKTIV, EREIGNIS;
extern unsigned char EMPFANGSZAEHLER, RAHMEN_AKTIV;
extern unsigned char EE_PENDING;
extern unsigned char ueberlaeufe;
extern unsigned int  timer1_hoch;

extern void Init(void);
extern void Blink(void);
extern void Aufwachen(void);
extern void PlaneEreignisse(void);
extern void EmpfangsDienst(void);
extern void EEDienst(void);
extern void Funke(void);


void Firmware_Init(void)
{
    memset(sim_eeprom, 0xFF, sizeof(sim_eeprom));
    TXIF_REG = 1;                 // Sender bereit (Simulationsannahme)

    Init();
    Blink();                          // LED-Starttest vor der Zuendfreigabe

    Aufwachen();                      // 99 x 0xAA zum Aufwecken
    TXWERT = 0;
    while (TXWERT < 250)
    {
        while (TXIF == 0);
        TXREG = 0b11011101;           // 250 x 0xDD als Startmeldung
        TXWERT++;
    }

    OT_1 = 0;
    STARTFUNKE = 0;
    EREIGNIS = 0;
    LADUNG_AKTIV = 0;
    ueberlaeufe = 0;
    timer1_hoch = 0;
    TIMER_WERT = 0;
    DREHZAHL = 0;
    EMPFANGSZAEHLER = 0;
    RAHMEN_AKTIV = 0;
    EE_PENDING = 0;
    IOCBF = 0;
    PIR1 = 0;
    PIR2 = 0;
    INTCONbits.IOCIF = 0;
    IOCBFbits.IOCBF4 = 0;
    T1GCON = 0;                       // Timer1 Gate aus
    T1CON = 0b00010001;
    host_register_sync();
    CCP1CON = 0b00001010;
    CCP2CON = 0b00001010;
    IOCBNbits.IOCBN4 = 1;
    PIE1bits.TMR1IE = 1;
    PIE1bits.CCP1IE = 1;
    PIE2bits.CCP2IE = 1;
    PIE1bits.RCIE = 0;
    INTCONbits.PEIE = 1;
    INTCONbits.IOCIE = 1;
    INTCONbits.GIE = 1;               // ab hier laeuft die Zuendung
    sim_timer1_basis = sim_now_ticks;
    sim_timer_aktualisieren();
    sim_ausgaenge_aktualisieren();
}

void Firmware_Rumpf(void)
{
    if (ueberlaeufe >= TIMER_UEBERLAEUFE_MAX && EREIGNIS != 1)
    {
        ueberlaeufe = 0;
        DREHZAHL = 0;
        OT_1 = 0;
        STARTFUNKE = 0;
        GRUEN = 1;
        BLAU = 1;
        if (LADUNG_AKTIV && (EREIGNIS == 0 || EREIGNIS == 3))
            Funke();
        EREIGNIS = 0;
    }

    if (EREIGNIS == 1)
        PlaneEreignisse();

    EmpfangsDienst();
    EEDienst();

    if (DREHZAHL == 0)
    {
        ROT = 1;
        GRUEN = 1;
        BLAU = 1;
    }
    else if (DREHZAHL <= 500UL * WERT_1)
    {
        ROT = 1;
        GRUEN = 1;
        BLAU = 0;
    }
    else if (DREHZAHL <= 500UL * WERT_2)
    {
        ROT = 1;
        GRUEN = 0;
        BLAU = 1;
    }
    else if (DREHZAHL <= 500UL * WERT_3)
    {
        ROT = 0;
        GRUEN = 0;
        BLAU = 1;
    }
    else
    {
        ROT = 0;
        GRUEN = 1;
        BLAU = 1;
    }
}
