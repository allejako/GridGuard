# Changelog 2026-03-23

**Branch:** `development`

---

## Bugfixar i Compute

### Division med noll vid extremt negativa spotpriser

I logg-raden som beräknar prisspridningen delades det på minimipriset utan att kolla om det var noll. Svenska spotpriser kan faktiskt bli tillräckligt negativa för att totalkostnaden (spot + nättariff + energiskatt + moms) ska landa på noll, vilket gav `inf%` i loggen. Lagt till en nollkontroll.

### Saknad cap på antal kvartar

Beräkningsloopen saknade en övre gräns mot stack-arrayen `actualCosts[192]`. I praktiken kan det aldrig hända eftersom Parser-structen fysiskt begränsar count till 192, men defensiv koll är på plats nu.

### Sluttid för köpfönster var 15 minuter för kort

`bestBuyWindow.end` och sluttiden i `days`-signalerna i JSON-svaret pekade på *starten* av det sista BUY-kvartet istället för *slutet* av det. Ett fönster 02:00–02:45 visades som 02:00–02:30. Varaktigheten (`duration_minutes`) var rätt hela tiden, bara sluttidsstämpeln var fel.
