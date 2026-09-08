/*********************************************************************************
*        File: sim_main.c
*        Zweck: Duenne Testhuelle um die ECHTE Startsequenz und den echten
*        Hauptschleifenrumpf aus My_Spark_1_2026.c. Es wird bewusst KEINE
*        zweite Kopie von main() gepflegt: die Tests sollen die Firmware
*        pruefen, nicht ein nachgebautes Modell davon.
*        Validiert trotzdem NICHT die Ziel-Firmware (kein HI-TECH C / XC8,
*        kein echtes PIC-Timing, kein Oszilloskop-Nachweis).
************************************************************************************/
#include "host_pic_shim.h"
#include <string.h>

extern void SystemStart(void);
extern void HauptSchleife(void);

/* Kaltstart: Shim-Zustand loeschen und die Firmware-Startsequenz laufen
   lassen. Das EEPROM bleibt erhalten, damit der Test die Parameter vorher
   setzen kann. */
void Firmware_Init(void)
{
    sim_reset();
    SystemStart();
}

void Firmware_Rumpf(void)
{
    HauptSchleife();
}
