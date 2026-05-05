#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "ini.h"

void ini_defaults(CbmConfig *cfg) {
    cfg->show_scratched        = 0;
    cfg->show_only_scratched   = 0;
    cfg->append_prg_ext        = 0;
    cfg->ignore_error_table    = 0;
    cfg->erase_deleted_sectors = 0;
    cfg->log_level             = 0;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    return s;
}

static int parse_bool(const char *val) {
    return (val[0] == '1') ? 1 : 0;
}

void ini_load(const char *path, CbmConfig *cfg) {
    ini_defaults(cfg);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == ';' || *s == '#' || *s == '[' || *s == '\0') continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (strcasecmp(key, "showScratchedFiles") == 0)
            cfg->show_scratched = parse_bool(val);
        else if (strcasecmp(key, "showONLYScratchedFiles") == 0)
            cfg->show_only_scratched = parse_bool(val);
        else if (strcasecmp(key, "appendPrgExtension") == 0)
            cfg->append_prg_ext = parse_bool(val);
        else if (strcasecmp(key, "ignoreErrorTable") == 0)
            cfg->ignore_error_table = parse_bool(val);
        else if (strcasecmp(key, "eraseDeletedSectors") == 0)
            cfg->erase_deleted_sectors = parse_bool(val);
        else if (strcasecmp(key, "logLevel") == 0)
            cfg->log_level = atoi(val);
    }
    fclose(f);
}
