#ifndef __MAIN__

#define LM35PRESENT 0
#define D18B20PRESENT 1
#define BATTERYMEASURE 1

#define DONTSLEEPPIN D5
#define DEFAULTSLEEPTIMEINSECONDS 300

#if LM35PRESENT == 1
    #define LM35SENSORPIN A0
#endif

#if D18B20PRESENT == 1
    #define DALLASPIN D6
#endif

#if BATTERYMEASURE == 1
    #define BATTERYMEASUREPIN A0
#endif

#if LM35PRESENT == 1 && BATTERYMEASURE == 1
    #error "You cannot use both battery measurement and LM35 since both require an analog input, and 8266 has only one"
#endif

#define __MAIN__
#endif
