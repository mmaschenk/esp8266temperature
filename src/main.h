#ifndef __MAIN__

#define LM35PRESENT 1
#define D18B20PRESENT 1

#define DONTSLEEPPIN D5
#define DEFAULTSLEEPTIMEINSECONDS 300

#if LM35PRESENT == 1
    #define SENSORPIN A0
#endif

#if D18B20PRESENT == 1
    #define DALLASPIN D6
#endif

#define __MAIN__
#endif
