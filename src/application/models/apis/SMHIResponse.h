#ifndef _SMHI_DATA_H_
#define _SMHI_DATA_H_

#include <time.h>

#define SMHI_MAX_PARAMETERS 24

typedef struct {
    char name[32];
    char levelType[8];
    int level;
    char unit[16];
    double value;
} SMHIParameter;

typedef struct {
    char validTime[32];
    SMHIParameter parameters[SMHI_MAX_PARAMETERS];
    int paramCount;
} SMHITimeEntry;

typedef struct {
    char approvedTime[32];
    char referenceTime[32];
    double latitude;
    double longitude;
    SMHITimeEntry timeSeries[168];
    int count;
} SMHIResponse;

const SMHIParameter* SMHI_FindParameter(const SMHITimeEntry *entry, const char *paramName);

#endif // _SMHI_DATA_H_
