#pragma once

typedef struct {
    int show_scratched;
    int show_only_scratched;
    int append_prg_ext;
    int ignore_error_table;
    int erase_deleted_sectors;
    int log_level;
} CbmConfig;

void ini_load(const char *path, CbmConfig *cfg);
void ini_defaults(CbmConfig *cfg);
