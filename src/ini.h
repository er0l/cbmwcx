#pragma once

typedef struct {
    int petscii_mode;          /* 0=legacy, 1=Unicode, 2=Style64 font */
    int petscii_lowercase;     /* 0=uppercase/graphics, 1=lowercase/uppercase */
    int show_file_size_in_blocks; /* listing only; disk directory block count */
    int show_free_blocks;
    int show_scratched;
    int show_only_scratched;
    int append_prg_ext;
    int ignore_error_table;
    int erase_deleted_sectors;
    int log_level;
} CbmConfig;

void ini_load(const char *path, CbmConfig *cfg);
void ini_defaults(CbmConfig *cfg);
