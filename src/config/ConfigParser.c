#define _POSIX_C_SOURCE 200809L

#include "config/ConfigParser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_CAPACITY 32
#define MAX_LINE_LENGTH 512

// Trim whitespace from both ends
static char *trim(char *str)
{
    if (!str) return NULL;

    while (isspace((unsigned char)*str)) str++;

    if (*str == '\0') return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

// Add entry to parser
static int add_entry(ConfigParser *parser, const char *key, const char *value)
{
    if (parser->count >= parser->capacity)
    {
        size_t newCapacity = parser->capacity * 2;
        ConfigEntry *newEntries = realloc(parser->entries, newCapacity * sizeof(ConfigEntry));
        if (!newEntries) return -1;

        parser->entries = newEntries;
        parser->capacity = newCapacity;
    }

    parser->entries[parser->count].key = strdup(key);
    parser->entries[parser->count].value = strdup(value);

    if (!parser->entries[parser->count].key || !parser->entries[parser->count].value)
    {
        free(parser->entries[parser->count].key);
        free(parser->entries[parser->count].value);
        return -1;
    }

    parser->count++;
    return 0;
}

int ConfigParser_Initiate(ConfigParser *parser)
{
    if (!parser) return -1;

    parser->entries = malloc(INITIAL_CAPACITY * sizeof(ConfigEntry));
    if (!parser->entries) return -1;

    parser->count = 0;
    parser->capacity = INITIAL_CAPACITY;

    return 0;
}

int ConfigParser_ParseFile(ConfigParser *parser, const char *filePath)
{
    if (!parser || !filePath) return -1;

    FILE *file = fopen(filePath, "r");
    if (!file) return -1;

    char line[MAX_LINE_LENGTH];
    char currentSection[128] = "";
    int lineNumber = 0;

    while (fgets(line, sizeof(line), file))
    {
        lineNumber++;
        char *trimmed = trim(line);

        // Skip empty lines and comments
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';')
            continue;

        // Section header
        if (trimmed[0] == '[')
        {
            char *end = strchr(trimmed, ']');
            if (!end)
            {
                fprintf(stderr, "ConfigParser: Invalid section at line %d\n", lineNumber);
                continue;
            }

            *end = '\0';
            strncpy(currentSection, trimmed + 1, sizeof(currentSection) - 1);
            currentSection[sizeof(currentSection) - 1] = '\0';
            continue;
        }

        // Key=value pair
        char *equals = strchr(trimmed, '=');
        if (!equals)
        {
            fprintf(stderr, "ConfigParser: Invalid key=value at line %d\n", lineNumber);
            continue;
        }

        *equals = '\0';
        char *key = trim(trimmed);
        char *value = trim(equals + 1);

        // Build fully qualified key: section.key
        char fullKey[256];
        if (currentSection[0] != '\0')
            snprintf(fullKey, sizeof(fullKey), "%s.%s", currentSection, key);
        else
            snprintf(fullKey, sizeof(fullKey), "%s", key);

        if (add_entry(parser, fullKey, value) != 0)
        {
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    return 0;
}

const char *ConfigParser_Get(const ConfigParser *parser, const char *key)
{
    if (!parser || !key) return NULL;

    for (size_t i = 0; i < parser->count; i++)
    {
        if (strcmp(parser->entries[i].key, key) == 0)
            return parser->entries[i].value;
    }

    return NULL;
}

const char *ConfigParser_GetOrDefault(const ConfigParser *parser, const char *key, const char *defaultValue)
{
    const char *value = ConfigParser_Get(parser, key);
    return value ? value : defaultValue;
}

void ConfigParser_Shutdown(ConfigParser *parser)
{
    if (!parser) return;

    for (size_t i = 0; i < parser->count; i++)
    {
        free(parser->entries[i].key);
        free(parser->entries[i].value);
    }

    free(parser->entries);
    parser->entries = NULL;
    parser->count = 0;
    parser->capacity = 0;
}
