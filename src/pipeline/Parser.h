#ifndef _PARSER_H_
#define _PARSER_H_

#include "DataStructures.h"

typedef struct
{
    bool isInitialized;
} Parser;

int Parser_Initiate(Parser *parser);
int Parser_ParseWeatherData(Parser *parser, const char *jsonData, WeatherForecast *forecast);
int Parser_ParseSpotPrices(Parser *parser, const char *jsonData, SpotPriceData *spotData);
void Parser_Shutdown(Parser *parser);

#endif
