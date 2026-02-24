#ifndef _PARSER_H_
#define _PARSER_H_

#include <pthread.h>
#include <stdbool.h>
#include "OpenMeteoResponse.h"
#include "ElprisetResponse.h"

typedef struct
{
    bool isInitialized;
    pthread_mutex_t mutex;
} Parser;

int  Parser_Initiate(Parser *parser);
void Parser_Shutdown(Parser *parser);

// ── Weather sources ───────────────────────────────────────────────────────────
// Each weather API gets its own response struct in src/application/models/apis/
// and a corresponding Parse function here. ParseWorker then maps the result
// to WeatherData via a WeatherFrom<Source>() function.
//
// To add a new weather source:
//   1. Add <Source>Response.h to models/apis/
//   2. Add Parser_Parse<Source>() below and implement it in Parser.c
//   3. Add Build<Source>ApiUrl() to APIEndpoints.h/c
//   4. Add fetch + parse call in FetchWorker.c / ParseWorker.c
// ─────────────────────────────────────────────────────────────────────────────
int Parser_ParseOpenMeteo(Parser *parser, const char *jsonData, OpenMeteoResponse *response);

// ── Spot price sources ────────────────────────────────────────────────────────
// Each spot price API gets its own response struct and Parse function.
// ParseWorker maps the result to SpotPrice via ConvertToSpotPrice().
//
// To add a new spot price source:
//   1. Add <Source>Response.h to models/apis/
//   2. Add Parser_Parse<Source>() below and implement it in Parser.c
//   3. Add Build<Source>ApiUrl() to APIEndpoints.h/c
//   4. Add fetch + parse call in FetchWorker.c / ParseWorker.c
// ─────────────────────────────────────────────────────────────────────────────
int Parser_ParseElpriset(Parser *parser, const char *jsonData, ElprisetResponse *response);

#endif
