#ifndef _PARSER_H_
#define _PARSER_H_

#include "OpenMeteoData.h"
#include "ElprisetData.h"
#include "ForecastData.h"

typedef struct
{
    bool isInitialized;
} Parser;

int Parser_Initiate(Parser *parser);
int Parser_ParseOpenMeteo(Parser *parser, const char *jsonData, OpenMeteoResponse *response);
int Parser_ParseElpriset(Parser *parser, const char *jsonData, ElprisetResponse *response);
int Parser_BuildForecast(const OpenMeteoResponse *weather, const ElprisetResponse *prices, ForecastData *forecast);
void Parser_Shutdown(Parser *parser);

#endif
