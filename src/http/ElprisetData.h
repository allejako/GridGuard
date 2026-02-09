#ifndef _ELPRISET_DATA_H_
#define _ELPRISET_DATA_H_

typedef struct {
    char time_start[26];    // "2026-02-09T00:00:00+01:00"
    char time_end[26];
    double SEK_per_kWh;
    double EUR_per_kWh;
    double EXR;             // Exchange rate
} ElprisetEntry;

typedef struct {
    ElprisetEntry entries[96]; // 24 hours * 4 (15-minute intervals)
    int count; // Actual number of entries returned by the API
} ElprisetResponse;

#endif
