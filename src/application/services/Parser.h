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

// Parse raw JSON from Open-Meteo into OpenMeteoResponse.
int Parser_ParseOpenMeteo(Parser *parser, const char *jsonData, OpenMeteoResponse *response);

// Parse raw JSON from Elpriset.se into ElprisetResponse.
int Parser_ParseElpriset(Parser *parser, const char *jsonData, ElprisetResponse *response);

#endif
