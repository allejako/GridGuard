#ifndef _ELPRISET_DATA_H_
#define _ELPRISET_DATA_H_

typedef struct
{
    char   time_start[26];
    char   time_end[26];
    double SEK_per_kWh;
    double EUR_per_kWh;
    double EXR;
} ElprisetEntry;

typedef struct
{
    ElprisetEntry entries[192];  // 2 days × 96 quarters/day (today + tomorrow to cover rolling 24h window)
    int count;
} ElprisetResponse;

#endif // _ELPRISET_DATA_H_
