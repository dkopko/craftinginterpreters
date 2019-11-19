#ifndef _DANDEBUG_H_
#define _DANDEBUG_H_

#include <libgen.h>
#include <stdio.h>
#include <string.h>

#define DANDEBUG0(FMT, ARGS...) \
  do { \
    char shortfilename[sizeof(__FILE__)]; \
    strcpy(shortfilename, __FILE__); \
    printf("DANDEBUG %s:%d:%s() " FMT, basename(shortfilename), __LINE__, __FUNCTION__, ##ARGS); \
  } while(0)

#define DANDEBUG(FMT, ARGS...) DANDEBUG0(FMT "\n", ##ARGS)

#endif //_DANDEBUG_H_
