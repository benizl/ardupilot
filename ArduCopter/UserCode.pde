/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
#include <GPS.h>
#ifdef USERHOOK_INIT

void userhook_init()
{


   hal.uartC->begin(9600, 128, 128);

}
#endif

#ifdef USERHOOK_FASTLOOP
  unsigned char UWB_rectemp[25];
   unsigned char count=0;
void userhook_FastLoop()
{



      char mO2;
 while (hal.uartC->available() > 0) {
     mO2 =  hal.uartC->read();
     //hal.uartC->print(mO2);
     UWB_rectemp[count]=mO2;
     count++;
     if(count>24)
     {
            //hal.uartC->print(mO2);
            //for(unsigned)
           // hal.uartC->print(0x55);
                  O2       = (unsigned char)UWB_rectemp[2];
                  //O2       =0x55;
                  CO2      = (unsigned char)UWB_rectemp[3];
                  CO.B[0]  = (unsigned char)UWB_rectemp[5];
                  CO.B[1]  = (unsigned char)UWB_rectemp[4];
                  NO.B[0]  = (unsigned char)UWB_rectemp[7];
                  NO.B[1]  = (unsigned char)UWB_rectemp[6];
                  NO2.B[0] = (unsigned char)UWB_rectemp[9];
                  NO2.B[1] = (unsigned char)UWB_rectemp[8];
                  SO2.B[0] = (unsigned char)UWB_rectemp[11];
                  SO2.B[1] = (unsigned char)UWB_rectemp[10];
                  H2S.B[0] = (unsigned char)UWB_rectemp[13];
                  H2S.B[1] = (unsigned char)UWB_rectemp[12];

                  COC.B[0]  = (unsigned char)UWB_rectemp[15];
                  COC.B[1]  = (unsigned char)UWB_rectemp[14];
                  NOC.B[0]  = (unsigned char)UWB_rectemp[17];
                  NOC.B[1]  = (unsigned char)UWB_rectemp[16];
                  NO2C.B[0] = (unsigned char)UWB_rectemp[19];
                  NO2C.B[1] = (unsigned char)UWB_rectemp[18];
                  SO2C.B[0] = (unsigned char)UWB_rectemp[21];
                  SO2C.B[1] = (unsigned char)UWB_rectemp[20];
                  H2SC.B[0] = (unsigned char)UWB_rectemp[23];
                  H2SC.B[1] = (unsigned char)UWB_rectemp[22];
                 count=0;
    }

 }
      //hal.uartC->printf("x:%d.",55);
      // hal.uartC->printf("%x",55);
    /** while(hal.uartC->available()>0)
    {
           hal.uartC->printf("%x",hal.uartC->available());
           temp[count]=hal.uartC->read();
          // hal.uartC->print(0x55);
           count++;
           if(count>24)
           {
            //for(unsigned)
           // hal.uartC->print(0x55);
            count=0;
         }

    }**/
}
#endif

#ifdef USERHOOK_50HZLOOP
void userhook_50Hz()
{


    // put your 50Hz code here
}
#endif

#ifdef USERHOOK_MEDIUMLOOP
void userhook_MediumLoop()
{


    // put your 10Hz code here
}
#endif

#ifdef USERHOOK_SLOWLOOP
void userhook_SlowLoop()
{
    // put your 3.3Hz code here
}
#endif

#ifdef USERHOOK_SUPERSLOWLOOP
void userhook_SuperSlowLoop()
{

    // put your 1Hz code here
}
#endif