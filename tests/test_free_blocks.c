#include "../src/cbmwcx.c"
#include <assert.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    char source[WCX_MAX_PATH]; snprintf(source, sizeof(source), "%s/file.prg", argv[1]);
    FILE *f = fopen(source, "wb"); assert(f); fputs("HELLO", f); fclose(f);
    for (int type = DISK_D64_35; type <= DISK_D64_40; type++) {
        ArcHandle h = {0};
        assert(cbm_create_image(&h, type, "TEST", "01") == 0);
        assert(cbm_d64_free_blocks(&h) == 664);
        h.show_free_blocks = 1; h.open_mode = PK_OM_LIST;
        cbm_dir_rewind(&h);
        tHeaderDataExW wide; tHeaderDataEx ex; tHeaderData ansi;
        assert(ReadHeaderExW(&h, &wide) == 0 && h.cur_is_disk_info);
        assert(wide.UnpSize == 0 && (wide.FileAttr & FA_READONLY));
        assert(strcmp(h.cur_filename, type == DISK_D64_35 ? "[664 blocks free]" :
                      "[664 blocks free (tracks 1-35)]") == 0);
        assert(ProcessFile(&h, PK_SKIP, NULL, NULL) == 0);
        assert(ProcessFile(&h, PK_TEST, NULL, NULL) == 0);
        assert(ProcessFile(&h, PK_EXTRACT, NULL, source) == E_NOT_SUPPORTED);
        assert(ReadHeaderExW(&h, &wide) == E_END_ARCHIVE);
        assert(cbm_write_file(&h, source, "hello", CBM_TYPE_PRG) == 0);
        assert(cbm_d64_free_blocks(&h) == 663);
        cbm_dir_rewind(&h); h.disk_info_emitted = 0;
        assert(ReadHeaderEx(&h, &ex) == 0);
        assert(strstr(ex.FileName, "663 blocks free"));
        assert(ReadHeader(&h, &ansi) == 0 && !h.cur_is_disk_info);
        uint8_t name[16]; memcpy(name, h.cur_raw_name, 16);
        assert(cbm_delete_file(&h, name, CBM_TYPE_PRG) == 0);
        assert(cbm_d64_free_blocks(&h) == 664);
        cbm_dir_rewind(&h); h.disk_info_emitted = 0; h.open_mode = PK_OM_EXTRACT;
        assert(ReadHeaderExW(&h, &wide) == E_END_ARCHIVE);
        cbm_dir_rewind(&h); h.show_free_blocks = 0; h.open_mode = PK_OM_LIST;
        assert(ReadHeaderExW(&h, &wide) == E_END_ARCHIVE);
        /* Invalid BAM counter must not become invented free capacity. */
        cbm_sector(&h, 18, 0)[4] = 255;
        assert(cbm_d64_free_blocks(&h) == -1);
        cbm_dir_rewind(&h); h.show_free_blocks = 1; h.disk_info_emitted = 0;
        assert(ReadHeader(&h, &ansi) == 0);
        assert(strcmp(ansi.FileName, "[free blocks unavailable]") == 0);
        free(h.image);
    }
    ArcHandle unsupported = {0}; unsupported.disk_type = DISK_T64;
    unsupported.show_free_blocks = 1;
    assert(cbm_d64_free_blocks(&unsupported) == -1);
    assert(next_header_entry(&unsupported) == E_END_ARCHIVE);
    puts("D64 free-block counts, write/delete updates, virtual rows and extraction isolation passed");
    return 0;
}
