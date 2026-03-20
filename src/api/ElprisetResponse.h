#ifndef _ELPRISET_DATA_H_
#define _ELPRISET_DATA_H_

typedef struct
{
    char   timeStart[26];
    char   timeEnd[26];
    double sekPerKwh;
    double eurPerKwh;
    double exr;
} ElprisetEntry;

typedef struct
{
    ElprisetEntry entries[192];  // 2 days × 96 quarters/day (today + tomorrow to cover rolling 24h window)
    int count;
} ElprisetResponse;

#endif // _ELPRISET_DATA_H_
