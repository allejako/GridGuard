#ifndef _ELPRISET_DATA_H_
#define _ELPRISET_DATA_H_

typedef struct {
    char time_start[26];
    char time_end[26];
    double SEK_per_kWh;
    double EUR_per_kWh;
    double EXR;
} ElprisetEntry;

typedef struct {
    ElprisetEntry entries[96];
    int count;
} ElprisetResponse;

#endif
