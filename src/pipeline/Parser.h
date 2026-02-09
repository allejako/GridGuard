#ifndef _PARSER_H_
#define _PARSER_H_

#include "DataStructures.h"
#include "OpenMeteoData.h"
#include "ElprisetData.h"
#include "PipelineData.h"

typedef struct
{
    bool isInitialized;
} Parser;

int Parser_Initiate(Parser *parser);

// Legacy functions (kept for backwards compatibility)
int Parser_ParseWeatherData(Parser *parser, const char *jsonData, WeatherForecast *forecast);
int Parser_ParseSpotPrices(Parser *parser, const char *jsonData, SpotPriceData *spotData);

// New API-specific parse functions
int Parser_ParseOpenMeteo(Parser *parser, const char *jsonData, OpenMeteoResponse *response);
int Parser_ParseElpriset(Parser *parser, const char *jsonData, ElprisetResponse *response);

// Combine raw API data into pipeline-agnostic format
int Parser_BuildPipelineData(const OpenMeteoResponse *weather, const ElprisetResponse *prices, PipelineData *pipeline);

void Parser_Shutdown(Parser *parser);

#endif
