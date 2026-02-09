#include <stdio.h>
#include "common/Logger.h"

int main(void)
{
    // Init logger - skriver till både konsol och fil
    if (Logger_Initiate("test.log", LOG_LEVEL_DEBUG) != 0) {
        printf("Failed to init logger\n");
        return 1;
    }

    // Testa alla loggnivåer
    LOG_DEBUG("Detta är ett debug-meddelande");
    LOG_INFO("Server startad på port %d", 8080);
    LOG_WARNING("Varning: cache är %d%% full", 85);
    LOG_ERROR("Kunde inte ansluta till klient: %s", "timeout");
    LOG_FATAL("Kritiskt fel!");

    // Testa att ändra nivå
    printf("\n--- Ändrar till WARNING-nivå ---\n\n");
    Logger_SetLevel(LOG_LEVEL_WARNING);

    LOG_DEBUG("Detta ska INTE synas");
    LOG_INFO("Detta ska INTE heller synas");
    LOG_WARNING("Detta SKA synas");
    LOG_ERROR("Detta SKA också synas");

    Logger_Shutdown();

    printf("\n--- Kolla test.log för filutdata ---\n");
    return 0;
}
