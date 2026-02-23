#ifndef _PARSER_H_
#define _PARSER_H_

#include <pthread.h>
#include "SMHIResponse.h"
#include "OpenMeteoResponse.h"
#include "ElprisetResponse.h"
#include "Forecast.h"

typedef struct
{
    bool isInitialized;
    pthread_mutex_t mutex;
} Parser;

// Parser initialization
int Parser_Initiate(Parser *parser);
void Parser_Shutdown(Parser *parser);

// Parse individual API responses
int Parser_ParseSMHI(Parser *parser, const char *jsonData, SMHIResponse *response);
int Parser_ParseOpenMeteo(Parser *parser, const char *jsonData, OpenMeteoResponse *response);
int Parser_ParseElpriset(Parser *parser, const char *jsonData, ElprisetResponse *response);

// DEPRECATED: Use combined parsing in ParseWorker instead
int Parser_BuildForecast(const OpenMeteoResponse *weather, const ElprisetResponse *prices, ForecastData *forecast);

#endif
