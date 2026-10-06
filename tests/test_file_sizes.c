#include "../src/cbmwcx.c"
#include <assert.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    char path[WCX_MAX_PATH]; snprintf(path, sizeof(path), "%s/settings.ini", argv[1]);
    FILE *f = fopen(path, "w"); assert(f);
    fputs("[common]\nshowFileSizeInBlocks=1\n", f); fclose(f);
    CbmConfig cfg; ini_load(path, &cfg); assert(cfg.show_file_size_in_blocks == 1);
    ini_defaults(&cfg); assert(cfg.show_file_size_in_blocks == 0);
    for (int type = DISK_D64_35; type <= DISK_D82; type++) {
        ArcHandle h = {0};
        assert(cbm_create_image(&h, type, "SIZE TEST", "01") == 0);
        cbm_dir_rewind(&h);
        uint8_t *dir = cbm_sector(&h, h.dir_track, h.dir_sector);
        memset(dir, 0, 256); dir[1] = 255;
        dir[DSLOT_FILE_TYPE] = CBM_CLOSED_BIT | CBM_TYPE_PRG;
        dir[DSLOT_TRACK] = 1; dir[DSLOT_SECTOR] = 0;
        memset(dir + DSLOT_NAME, 0xa0, 16); dir[DSLOT_NAME] = 0x41;
        /* Deliberately differ from the chain's two sectors: use recorded count. */
        dir[DSLOT_SIZE_LO] = 7;
        uint8_t *data = cbm_sector(&h, 1, 0);
        memset(data, 'X', 256); data[0] = 1; data[1] = 1;
        data = cbm_sector(&h, 1, 1); memset(data, 'Y', 256);
        data[0] = 0; data[1] = 6;  /* five final bytes => 259 total */
        for (int enabled = 0; enabled < 2; enabled++) {
            h.show_file_size_in_blocks = enabled;
            for (int mode = PK_OM_LIST; mode <= PK_OM_EXTRACT; mode++) {
                h.open_mode = mode;
                unsigned int expected = enabled && mode == PK_OM_LIST ? 7 : 259;
                tHeaderData header; tHeaderDataEx ex; tHeaderDataExW wide;
                cbm_dir_rewind(&h); assert(ReadHeader(&h, &header) == 0);
                assert((unsigned int)header.UnpSize == expected);
                cbm_dir_rewind(&h); assert(ReadHeaderEx(&h, &ex) == 0);
                assert(ex.UnpSize == expected && ex.UnpSizeHigh == 0);
                cbm_dir_rewind(&h); assert(ReadHeaderExW(&h, &wide) == 0);
                assert(wide.UnpSize == expected && wide.UnpSizeHigh == 0);
                assert(h.cur_size_bytes == 259 && h.cur_size_blocks == 7);
                snprintf(path, sizeof(path), "%s/extracted.prg", argv[1]);
                assert(ProcessFile(&h, PK_EXTRACT, NULL, path) == 0);
                struct stat st; assert(stat(path, &st) == 0 && st.st_size == 259);
            }
        }
        dir[DSLOT_FILE_TYPE] = CBM_CLOSED_BIT | CBM_TYPE_DEL;
        dir[DSLOT_TRACK] = 0; dir[DSLOT_SIZE_LO] = 0;
        h.open_mode = PK_OM_LIST; h.show_file_size_in_blocks = 1;
        cbm_dir_rewind(&h); tHeaderDataExW wide;
        assert(ReadHeaderExW(&h, &wide) == 0 && wide.UnpSize == 0);
        free(h.image);
    }
    ArcHandle tape = {0}; tape.disk_type = DISK_T64;
    tape.show_file_size_in_blocks = 1; tape.open_mode = PK_OM_LIST;
    tape.cur_size_bytes = 259; tape.cur_size_blocks = 2;
    assert(header_file_size(&tape) == 259);
    puts("Byte/block sizes passed for all disk formats and header APIs; extraction preserved");
    return 0;
}
