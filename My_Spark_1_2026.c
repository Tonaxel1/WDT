/*********************************************************************************
 * Controller: PIC16F1827, Compiler: HI-TECH C, interner Takt: 8 MHz
 * Sensorsignal auf RB4, Funke beim Abschalten von COIL (RA4).
 *********************************************************************************/
#include <htc.h>
#include <pic16f1827.h>

__CONFIG(FOSC_INTOSC & PWRTE_ON & BOREN_ON & MCLRE_OFF & WDTE_OFF & CP_OFF & PLLEN_OFF);
__CONFIG(LVP_OFF);

#define _XTAL_FREQ 8000000
#define SENSOR RB4
#define LED_1 LATA0
#define LED_2 LATA1
#define COIL LATA4
#define DREHZ LATB3
#define ROT LATB5
#define GRUEN LATB6
#define BLAU LATB7

#define TICKS_GUARD 16u
#define UART_TIMEOUT_TICKS 2u
#define EEPROM_BYTES 10u

#define ZUEND_AUS 0u
#define START_LADEN 1u
#define START_ABSCHALTEN 2u
#define NORMAL_LADEN 3u
#define NORMAL_ABSCHALTEN 4u

volatile unsigned char OT_1, LADEZEIT, WERT_1, WERT_2, WERT_3;
volatile unsigned char START_WINKEL, zuend_zustand, synchronisiert;
volatile unsigned int DREHZ_W, VOR_W, DREHZ_MAX, ZEIT;
volatile unsigned int timer_wert, normal_termin;
volatile unsigned long DREHZAHL;
volatile unsigned char rx_puffer[11], rx_index, rx_fertig, rx_timeout;
volatile unsigned char zeitbasis, blink_schritte, blink_teiler, tx_rest;
volatile unsigned char eeprom_ausstehend, eeprom_index;

static unsigned char eeprom_puffer[EEPROM_BYTES];

void Init(void);
void EELese(void);
void EmpfangAuswerten(void);
void EEPROMDienst(void);
void Anzeige(void);
void Blink(void);
void Aufwachen(void);

static unsigned int Timer1Lesen(void)
{
    unsigned char niedrig, hoch;

    niedrig = TMR1L;                /* Das Lesen von L latcht H auf diesem PIC. */
    hoch = TMR1H;
    return ((unsigned int)hoch << 8) | niedrig;
}

static void SpuleAus(void)
{
    COIL = 0;
    DREHZ = 0;
    LED_2 = 0;
}

static void Stillstand(void)
{
    PIE1bits.CCP1IE = 0;
    PIR1bits.CCP1IF = 0;
    zuend_zustand = ZUEND_AUS;
    synchronisiert = 0;
    OT_1 = 0;
    DREHZAHL = 0;
    normal_termin = 0;
    SpuleAus();
}

/* Compare nur setzen, wenn der Termin noch sicher vor uns liegt. */
static unsigned char CompareSetzen(unsigned int termin, unsigned int jetzt)
{
    if ((termin <= jetzt) || ((unsigned int)(termin - jetzt) < TICKS_GUARD))
        return 0;

    PIE1bits.CCP1IE = 0;
    PIR1bits.CCP1IF = 0;
    CCPR1H = (unsigned char)(termin >> 8);
    CCPR1L = (unsigned char)termin;
    PIR1bits.CCP1IF = 0;
    PIE1bits.CCP1IE = 1;
    return 1;
}

static unsigned char NormalenFunkenPlanen(void)
{
    unsigned long vor_ticks;
    unsigned long abschalt_termin;
    unsigned int start_termin;
    unsigned int vorwinkel;

    if ((!synchronisiert) || (DREHZAHL >= DREHZ_MAX))
        return 0;

    if (DREHZAHL < DREHZ_W)
        vorwinkel = (unsigned int)(((unsigned long)DREHZAHL * VOR_W +
                      (DREHZ_W >> 1)) / DREHZ_W);
    else
        vorwinkel = VOR_W;

    vor_ticks = ((unsigned long)timer_wert * vorwinkel + 180UL) / 360UL;
    if ((vor_ticks + ZEIT) >= timer_wert)
        return 0;                   /* Ladung wäre vor dem Referenzimpuls nötig. */

    abschalt_termin = (unsigned long)timer_wert - vor_ticks;
    start_termin = (unsigned int)(abschalt_termin - ZEIT);
    normal_termin = start_termin;
    if (!CompareSetzen(start_termin, Timer1Lesen()))
        return 0;                   /* Termin ist durch ISR-/Rechenzeit überholt. */

    zuend_zustand = NORMAL_LADEN;
    return 1;
}

static void SensorEreignis(void)
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

void interrupt isr(void)
{
    unsigned int jetzt;

    if (INTCONbits.IOCIF && IOCBFbits.IOCBF4) {
        SensorEreignis();
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
            /* Startfunken 5 zuerst abschalten, danach Funken 6 planen. */
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

    /* Ein gleichzeitiger Sensorimpuls gewinnt; dessen ISR hat TMR1IF gelöscht. */
    if (PIR1bits.TMR1IF && PIE1bits.TMR1IE) {
        PIR1bits.TMR1IF = 0;
        Stillstand();
    }

    if (INTCONbits.TMR0IF && INTCONbits.TMR0IE) {
        INTCONbits.TMR0IF = 0;
        zeitbasis++;
        if (rx_index != 0u && ++rx_timeout >= UART_TIMEOUT_TICKS)
            rx_index = 0;
        if (blink_schritte != 0u && ++blink_teiler >= 6u) {
            blink_teiler = 0;
            LED_1 = !LED_1;
            blink_schritte--;
        }
    }

    if (PIR1bits.RCIF && PIE1bits.RCIE) {
        unsigned char zeichen;
        if (RCSTAbits.OERR) {
            RCSTAbits.CREN = 0;
            RCSTAbits.CREN = 1;
            rx_index = 0;
        }
        zeichen = RCREG;             /* FERR-Byte verwerfen und neu synchronisieren. */
        if (RCSTAbits.FERR) {
            rx_index = 0;
        } else if (rx_fertig == 0u) {
            if (rx_index == 0u && zeichen == (unsigned char)'S') {
                rx_puffer[0] = zeichen;
                rx_index = 1;
                rx_timeout = 0;
            } else if (rx_index != 0u) {
                rx_puffer[rx_index++] = zeichen;
                rx_timeout = 0;
                if (rx_index == 11u) {
                    rx_fertig = 1;
                    rx_index = 0;
                }
            }
        }
    }
}

static unsigned char EinstellungenGueltig(unsigned char start, unsigned char vor,
                                          unsigned char lade, unsigned int dreh_w,
                                          unsigned int dreh_max, unsigned char wert1,
                                          unsigned char wert2, unsigned char wert3)
{
    if ((start > 20u) || (vor == 0u) || (vor > 40u))
        return 0;
    if ((lade < 1u) || (lade > 5u))
        return 0;
    if ((dreh_w < 2000u) || (dreh_w > 6000u) ||
        (dreh_max < 4000u) || (dreh_max > 11000u) || (dreh_max < dreh_w))
        return 0;
    if ((wert1 == 0u) || (wert3 > 30u) ||
        (wert1 > wert2) || (wert2 > wert3))
        return 0;
    return 1;
}

static void EinstellungenUebernehmen(unsigned char start, unsigned char vor,
                                     unsigned char lade, unsigned int dreh_w,
                                     unsigned int dreh_max, unsigned char wert1,
                                     unsigned char wert2, unsigned char wert3)
{
    unsigned char gie = INTCONbits.GIE;

    INTCONbits.GIE = 0;
    START_WINKEL = start;
    VOR_W = vor;
    LADEZEIT = lade;
    ZEIT = (unsigned int)lade * 125u + 125u; /* 1..5 ms: 250..750 Timer1-Ticks */
    DREHZ_W = dreh_w;
    DREHZ_MAX = dreh_max;
    WERT_1 = wert1;
    WERT_2 = wert2;
    WERT_3 = wert3;
    INTCONbits.GIE = gie;
}

void EmpfangAuswerten(void)
{
    unsigned char daten[11];
    unsigned char i, gie;
    unsigned int dreh_w, dreh_max;

    if (!rx_fertig)
        return;
    gie = INTCONbits.GIE;
    INTCONbits.GIE = 0;
    for (i = 0; i < 11u; i++)
        daten[i] = rx_puffer[i];
    rx_fertig = 0;
    INTCONbits.GIE = gie;

    dreh_w = ((unsigned int)daten[4] << 8) | daten[5];
    dreh_max = ((unsigned int)daten[6] << 8) | daten[7];
    if (!EinstellungenGueltig(daten[1], daten[2], daten[3], dreh_w, dreh_max,
                              daten[8], daten[9], daten[10]))
        return;

    EinstellungenUebernehmen(daten[1], daten[2], daten[3], dreh_w, dreh_max,
                             daten[8], daten[9], daten[10]);
    for (i = 0; i < EEPROM_BYTES; i++)
        eeprom_puffer[i] = daten[i + 1u];
    eeprom_index = 0;
    eeprom_ausstehend = 1;
    Blink();
}

void EEPROMDienst(void)
{
    unsigned char gie;

    /* Schreiben nur im sicheren Stillstand; WR läuft dann ohne Warten weiter. */
    if ((!eeprom_ausstehend) || synchronisiert || EECON1bits.WR)
        return;
    if (eeprom_index >= EEPROM_BYTES) {
        eeprom_ausstehend = 0;
        return;
    }

    EEADRL = (unsigned char)(eeprom_index + 1u);
    EEDATA = eeprom_puffer[eeprom_index++];
    EECON1bits.EEPGD = 0;
    EECON1bits.WREN = 1;
    gie = INTCONbits.GIE;
    INTCONbits.GIE = 0;
    EECON2 = 0x55;
    EECON2 = 0xAA;
    EECON1bits.WR = 1;
    INTCONbits.GIE = gie;
    EECON1bits.WREN = 0;
}

void Blink(void)
{
    blink_schritte = 6u;
    blink_teiler = 0;
}

void Aufwachen(void)
{
    tx_rest = 100u;
}

void Anzeige(void)
{
    unsigned long drehzahl;
    unsigned char ist_synchronisiert, gie;

    gie = INTCONbits.GIE;
    INTCONbits.GIE = 0;
    drehzahl = DREHZAHL;
    ist_synchronisiert = synchronisiert;
    INTCONbits.GIE = gie;
    if (!ist_synchronisiert) {
        GRUEN = 1;
        BLAU = 1;
        return;
    }
    if (drehzahl <= (unsigned long)WERT_1 * 500UL) {
        ROT = 1; GRUEN = 1; BLAU = 0;
    } else if (drehzahl <= (unsigned long)WERT_2 * 500UL) {
        ROT = 1; GRUEN = 0; BLAU = 1;
    } else if (drehzahl <= (unsigned long)WERT_3 * 500UL) {
        ROT = 0; GRUEN = 0; BLAU = 1;
    } else {
        ROT = 0; GRUEN = 1; BLAU = 1;
    }
}

void EELese(void)
{
    EECON1bits.EEPGD = 0;
    EECON1bits.RD = 1;
}

void Init(void)
{
    unsigned char start, vor, lade, wert1, wert2, wert3;
    unsigned int dreh_w, dreh_max;

    LATA = 0;
    LATB = 0;
    ANSELA = 0;
    ANSELB = 0;
    TRISA = 0b00000000;             /* COIL-Latch ist vor der Ausgangsfreigabe aus. */
    TRISB = 0b00010111;             /* RB7 (BLAU) ist Ausgang. */
    WPUB = 0b00010111;
    OPTION_REG = 0b00000111;        /* Timer0: Fosc/4, 1:256, Überlauf 32,768 ms. */
    OSCCON = 0b01110010;            /* interner 8-MHz-Takt */
    APFCON = 0;                     /* EUSART Standard: RX RB1, TX RB2; RB4 bleibt Sensor. */
    RCSTA = 0b10010000;
    SPBRGH = 0;
    SPBRG = 207;                    /* 9600 Baud, BRG16 und BRGH */
    TXSTA = 0b10100100;
    BAUDCON = 0b00001000;

    EEADRL = 1; EELese(); start = EEDATA;
    EEADRL = 2; EELese(); vor = EEDATA;
    EEADRL = 3; EELese(); lade = EEDATA;
    EEADRL = 4; EELese(); dreh_w = (unsigned int)EEDATA << 8;
    EEADRL = 5; EELese(); dreh_w |= EEDATA;
    EEADRL = 6; EELese(); dreh_max = (unsigned int)EEDATA << 8;
    EEADRL = 7; EELese(); dreh_max |= EEDATA;
    EEADRL = 8; EELese(); wert1 = EEDATA;
    EEADRL = 9; EELese(); wert2 = EEDATA;
    EEADRL = 10; EELese(); wert3 = EEDATA;

    if (!EinstellungenGueltig(start, vor, lade, dreh_w, dreh_max,
                              wert1, wert2, wert3))
        EinstellungenUebernehmen(10u, 40u, 3u, 4000u, 11000u, 4u, 8u, 12u);
    else
        EinstellungenUebernehmen(start, vor, lade, dreh_w, dreh_max,
                                 wert1, wert2, wert3);
}

void main(void)
{
    Init();
    COIL = 0;
    DREHZ = 0;
    ROT = GRUEN = BLAU = 1;
    Aufwachen();

    TMR1H = 0;
    TMR1L = 0;
    T1CON = 0b00110001;             /* Fosc/4, 1:8, Timer1 an */
    CCP1CON = 0b00001010;           /* Compare, nur Interrupt */
    IOCBNbits.IOCBN4 = 1;
    IOCBFbits.IOCBF4 = 0;
    INTCONbits.IOCIF = 0;
    PIR1bits.TMR1IF = 0;
    PIR1bits.CCP1IF = 0;
    PIE1bits.TMR1IE = 1;
    PIE1bits.CCP1IE = 0;
    PIE1bits.RCIE = 1;
    INTCONbits.TMR0IF = 0;
    INTCONbits.TMR0IE = 1;
    INTCONbits.IOCIE = 1;
    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;

    while (1) {
        EmpfangAuswerten();
        EEPROMDienst();
        if (tx_rest != 0u && PIR1bits.TXIF) {
            TXREG = 0b10101010;
            tx_rest--;
        }
        Anzeige();
    }
}
