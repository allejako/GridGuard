#ifndef _API_PARSER_H_
#define _API_PARSER_H_

#include <pthread.h>
#include <stdbool.h>
#include "api/OpenMeteoResponse.h"
#include "api/ElprisetResponse.h"

typedef struct
{
    bool isInitialized;
    pthread_mutex_t mutex;
} APIParser;

int  APIParser_Initiate(APIParser *parser);
void APIParser_Shutdown(APIParser *parser);

// Parse raw JSON from Open-Meteo into OpenMeteoResponse.
int APIParser_ParseOpenMeteo(APIParser *parser, const char *jsonData, OpenMeteoResponse *response);

// Parse raw JSON from Elpriset.se into ElprisetResponse.
int APIParser_ParseElpriset(APIParser *parser, const char *jsonData, ElprisetResponse *response);

#endif
