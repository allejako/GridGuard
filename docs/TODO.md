## GUSTAF DAEMON / WATCHDAWG
## ALEX CACHNING
## POWELL DAEMON / WATCHDAWG 
## KEVIN C++ ( KLIENT / CLI / PLATFORM )

## Vilka parameterar har man tillgängligt när man skaffar solceller? Vad får man för information? Vad kan man ställa in själv? Vad kan kund själv ändra manuellt. 

Vi tar emot 122000 bytes från smhi och filtrering måste hända på våran sida, innan cachning.
Kan vara bra att spara minst 24 timmar framåt i tiden för att användas vid energiberäkning eller nåt :P
just nu är smhijson i FetchWorker.h 1000000 bytes stor för att kunna ta emot hela meddelandet, lol.

Cachning sparas för tillfället bara i structar -> all cachning raderas vid omstart av server
Ska sparas i en sql-databas.

Gör man 2 requests i rad får man error, gissar att variabler inte nollas efter dem använts.
^ Kan också vara att cachningen för smhi json inte fungerar som den ska

SÅHÄR TESTAR MAN
make run-server -> ny terminal -> nc localhost 8080 -> forecast stockholm SE3