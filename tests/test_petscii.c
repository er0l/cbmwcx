/* Integration checks: compile with src/cbm.c and src/ini.c.
 * Including the host adapter lets tests select INI modes without loading
 * a plugin from the user's installation directory. */
#include "../src/cbmwcx.c"
#include <assert.h>

static void check_conversion(void) {
    char text[64];
    uint8_t raw[16], restored[16];
    const uint8_t symbols[] = {0x41, 0x5c, 0x5e, 0x5f, 0x61, 0x73, 0xa1, 0xa0};
    cbm_petscii_display(symbols, sizeof(symbols), text, sizeof(text), 1, 0);
    assert(strcmp(text, "A£↑←♠♥▌") == 0);
    cbm_display_to_petscii(text, restored, 16, 1, 0);
    assert(memcmp(restored, symbols, 7) == 0);
    cbm_petscii_display(symbols, sizeof(symbols), text, sizeof(text), 1, 1);
    assert(strncmp(text, "a£↑←A", strlen("a£↑←A")) == 0);
    cbm_petscii_display(symbols, sizeof(symbols), text, sizeof(text), 0, 0);
    assert(strcmp(text, "a______") == 0);
    for (int lowercase = 0; lowercase < 2; lowercase++) {
        for (int c = 0; c <= 255; c++) {
            raw[0] = c; raw[1] = 0x41;
            cbm_petscii_display(raw, 2, text, sizeof(text), 2, lowercase);
            cbm_display_to_petscii(text, restored, 2, 2, lowercase);
            assert(restored[0] == c && restored[1] == 0x41);
        }
    }
    memset(raw, 0xc1, sizeof(raw));
    cbm_petscii_display(raw, 16, text, sizeof(text), 2, 0);
    assert(strlen(text) == 48);
    cbm_petscii_display(raw, 16, text, 3, 2, 0);
    assert(text[0] == 0);  /* No incomplete UTF-8 character. */
    cbm_petscii_display(raw, 16, text, 4, 2, 0);
    assert(strlen(text) == 3);
    cbm_display_to_petscii("\xe2\x82", restored, 16, 1, 0);
    assert(restored[0] == '_');
    cbm_display_to_petscii("\xc0\xaf", restored, 16, 1, 0);
    assert(restored[0] == '_' && restored[1] == '_');
}

static void check_archive(const char *folder, int mode, int disk_type) {
    char image_path[WCX_MAX_PATH], source[WCX_MAX_PATH], extracted[WCX_MAX_PATH];
    snprintf(image_path, sizeof(image_path), "%s/test.d64", folder);
    snprintf(source, sizeof(source), "%s/input.prg", folder);
    snprintf(extracted, sizeof(extracted), "%s/output.prg", folder);
    FILE *f = fopen(source, "wb"); assert(f);
    assert(fwrite("\x01\x08HELLO", 1, 7, f) == 7); fclose(f);
    ArcHandle image = {0};
    strcpy(image.arc_name, image_path);
    assert(cbm_create_image(&image, disk_type, "TEST", "01") == 0);
    assert(cbm_write_file(&image, source, "original", CBM_TYPE_PRG) == 0);
    cbm_dir_rewind(&image); assert(cbm_dir_next(&image) == 0);
    uint8_t *dir = cbm_sector(&image, image.dir_track, image.dir_sector);
    /* Use the $c1 alias, not canonical $61, to verify raw-byte deletion. */
    memset(dir + DSLOT_NAME, 0xc1, 16);
    assert(cbm_save_image(&image) == 0); free(image.image);
    ini_defaults(&g_cfg); g_cfg.petscii_mode = mode;
    tOpenArchiveData open = {0}; open.ArcName = image_path;
    ArcHandle *h = OpenArchive(&open); assert(h && open.OpenResult == 0);
    tHeaderDataExW header;
    assert(ReadHeaderExW(h, &header) == 0);
    assert(header.FileName[0] == (mode == 2 ? 0xe0c1 : 0x2660));
    assert(header.FileName[15] == header.FileName[0]);
    assert(header.FileName[16] == '.');
    assert(ProcessFile(h, PK_EXTRACT, NULL, extracted) == 0);
    f = fopen(extracted, "rb"); assert(f);
    char data[8]; assert(fread(data, 1, sizeof(data), f) == 7);
    assert(memcmp(data, "\x01\x08HELLO", 7) == 0); fclose(f);
    assert(CloseArchive(h) == 0);
    uint16_t wide_path[WCX_MAX_PATH], list[WCX_MAX_PATH] = {0};
    utf8_to_wcs(image_path, wide_path, WCX_MAX_PATH);
    memcpy(list, header.FileName, sizeof(header.FileName));
    assert(DeleteFilesW(wide_path, list) == 0);
    h = OpenArchive(&open); assert(h);
    assert(ReadHeaderExW(h, &header) == E_END_ARCHIVE);
    assert(CloseArchive(h) == 0);

    /* Add a graphical filename through the WCX host API, then reopen it. */
    g_cfg.petscii_mode = mode;
    uint8_t raw_name[16]; memset(raw_name, 0xa0, sizeof(raw_name));
    memset(raw_name, 0x73, 16); raw_name[1] = 0x41;
    char add_list[WCX_MAX_PATH] = {0}, graphical_name[64];
    cbm_petscii_display(raw_name, 16, graphical_name, sizeof(graphical_name), mode, 0);
    snprintf(add_list, sizeof(add_list), "%s.prg", graphical_name);
    snprintf(source, sizeof(source), "%s/%s", folder, add_list);
    f = fopen(source, "wb"); assert(f); fputs("PAYLOAD", f); fclose(f);
    assert(PackFiles(image_path, NULL, (char *)folder, add_list, 0) == 0);
    h = OpenArchive(&open); assert(h);
    assert(ReadHeaderExW(h, &header) == 0);
    assert(memcmp(h->cur_raw_name, raw_name, 16) == 0);
    assert(CloseArchive(h) == 0);
}

static void check_ambiguous_delete(const char *folder) {
    ArcHandle image = {0};
    char path[WCX_MAX_PATH], source[WCX_MAX_PATH];
    snprintf(path, sizeof(path), "%s/ambiguous.d64", folder);
    snprintf(source, sizeof(source), "%s/input.prg", folder);
    strcpy(image.arc_name, path);
    assert(cbm_create_image(&image, DISK_D64_35, "TEST", "01") == 0);
    assert(cbm_write_file(&image, source, "one", CBM_TYPE_PRG) == 0);
    assert(cbm_write_file(&image, source, "two", CBM_TYPE_PRG) == 0);
    uint8_t *dir = cbm_sector(&image, 18, 1);
    memset(dir + DSLOT_NAME, 0xa0, 16); dir[DSLOT_NAME] = 0x61;
    memset(dir + 32 + DSLOT_NAME, 0xa0, 16); dir[32 + DSLOT_NAME] = 0xc1;
    assert(cbm_save_image(&image) == 0); free(image.image);
    ini_defaults(&g_cfg);
    char list[] = "♠.prg\0";
    assert(DeleteFiles(path, list) == E_BAD_DATA);
    tOpenArchiveData open = {0}; open.ArcName = path;
    ArcHandle *h = OpenArchive(&open); assert(h);
    tHeaderDataExW header;
    assert(ReadHeaderExW(h, &header) == 0);
    assert(ReadHeaderExW(h, &header) == 0);
    assert(ReadHeaderExW(h, &header) == E_END_ARCHIVE);
    assert(CloseArchive(h) == 0);
}

static void check_t64(const char *folder) {
    char path[WCX_MAX_PATH]; snprintf(path, sizeof(path), "%s/test.t64", folder);
    uint8_t tape[99] = {0}; memcpy(tape, "C64S", 4);
    tape[34] = 1; tape[36] = 1;
    uint8_t *entry = tape + 64; entry[0] = 1; entry[1] = 0x82;
    entry[3] = 8; entry[4] = 3; entry[5] = 8; entry[8] = 96;
    memset(entry + 16, 0xc1, 16); memcpy(tape + 96, "ABC", 3);
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(tape, 1, sizeof(tape), f) == sizeof(tape)); fclose(f);
    ini_defaults(&g_cfg); g_cfg.petscii_mode = 2;
    tOpenArchiveData open = {0}; open.ArcName = path;
    ArcHandle *h = OpenArchive(&open); assert(h);
    tHeaderDataExW header; assert(ReadHeaderExW(h, &header) == 0);
    assert(header.FileName[15] == 0xe0c1 && header.FileName[16] == '.');
    assert(CloseArchive(h) == 0);
    char list[] = "anything.prg\0";
    assert(DeleteFiles(path, list) == E_NOT_SUPPORTED);
    assert(PackFiles(path, NULL, (char *)folder, list, 0) == E_NOT_SUPPORTED);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    check_conversion();
    int types[] = {DISK_D64_35, DISK_D64_40, DISK_D71, DISK_D81};
    for (unsigned int i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        for (int mode = 1; mode <= 2; mode++) check_archive(argv[1], mode, types[i]);
    }
    check_ambiguous_delete(argv[1]);
    check_t64(argv[1]);
    puts("PETSCII conversion and WCX listing, extraction, deletion and addition passed");
    return 0;
}
