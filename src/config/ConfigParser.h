#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stddef.h>

// Simple INI file parser
// Format: [section] key=value # comments
// Keys stored as "section.key" internally

typedef struct {
    char *key;
    char *value;
} ConfigEntry;

typedef struct {
    ConfigEntry *entries;
    size_t count;
    size_t capacity;
} ConfigParser;

// Initialize parser
int ConfigParser_Initiate(ConfigParser *parser);

// Parse INI file from path
int ConfigParser_ParseFile(ConfigParser *parser, const char *filePath);

// Get value for key (section.key format), returns NULL if not found
const char *ConfigParser_Get(const ConfigParser *parser, const char *key);

// Get value with default fallback
const char *ConfigParser_GetOrDefault(const ConfigParser *parser, const char *key, const char *defaultValue);

// Free all resources
void ConfigParser_Shutdown(ConfigParser *parser);

#endif // CONFIG_PARSER_H
