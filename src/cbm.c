#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include "cbm.h"

/* ============================================================
 * Sector layout tables
 * ============================================================ */

/* D64: 35-track + 5 extra tracks for 40-track variants */
static const int d64_spt[] = {
    0,                                                  /* [0] unused */
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21, /* 1-17 */
    19,19,19,19,19,19,19,                               /* 18-24 */
    18,18,18,18,18,18,                                  /* 25-30 */
    17,17,17,17,17,                                     /* 31-35 */
    17,17,17,17,17                                      /* 36-40 */
};

/* D80: 77 tracks */
static const int d80_spt[] = {
    0,
    29,29,29,29,29,29,29,29,29,29, /* 1-10 */
    29,29,29,29,29,29,29,29,29,29, /* 11-20 */
    29,29,29,29,29,29,29,29,29,29, /* 21-30 */
    29,29,29,29,29,29,29,29,29,    /* 31-39 */
    27,27,27,27,27,27,27,27,27,27, /* 40-49 */
    27,27,27,27,                   /* 50-53 */
    25,25,25,25,25,25,25,25,25,25,25, /* 54-64 */
    23,23,23,23,23,23,23,23,23,23,23,23,23  /* 65-77 */
};

int cbm_sectors_per_track(int disk_type, int track) {
    switch (disk_type) {
    case DISK_D64_35:
        if (track < 1 || track > 35) return -1;
        return d64_spt[track];
    case DISK_D64_40:
        if (track < 1 || track > 40) return -1;
        return d64_spt[track];
    case DISK_D71:
        if (track < 1 || track > 70) return -1;
        /* Side 2 mirrors side 1 */
        return (track <= 35) ? d64_spt[track] : d64_spt[track - 35];
    case DISK_D80:
        if (track < 1 || track > 77) return -1;
        return d80_spt[track];
    case DISK_D82:
        if (track < 1 || track > 154) return -1;
        return (track <= 77) ? d80_spt[track] : d80_spt[track - 77];
    case DISK_D81:
        if (track < 1 || track > 80) return -1;
        return 40;
    default:
        return -1;
    }
}

int cbm_max_tracks(int disk_type) {
    switch (disk_type) {
    case DISK_D64_35: return 35;
    case DISK_D64_40: return 40;
    case DISK_D71:    return 70;
    case DISK_D80:    return 77;
    case DISK_D81:    return 80;
    case DISK_D82:    return 154;
    default:          return 0;
    }
}

long cbm_sector_offset(int disk_type, int track, int sector) {
    if (track < 1) return -1;
    int max = cbm_max_tracks(disk_type);
    if (track > max) return -1;
    int spt = cbm_sectors_per_track(disk_type, track);
    if (spt < 0 || sector < 0 || sector >= spt) return -1;

    long offset = 0;
    for (int t = 1; t < track; t++) {
        int s = cbm_sectors_per_track(disk_type, t);
        if (s < 0) return -1;
        offset += s;
    }
    offset += sector;
    return offset * SECTOR_SIZE;
}

uint8_t *cbm_sector(const ArcHandle *h, int track, int sector) {
    long off = cbm_sector_offset(h->disk_type, track, sector);
    if (off < 0 || (size_t)(off + SECTOR_SIZE) > h->image_size)
        return NULL;
    return h->image + off;
}

/* ============================================================
 * Disk type detection
 * ============================================================ */

int cbm_detect_type(const char *filename, size_t filesize) {
    /* First check by file size */
    switch (filesize) {
    case 174848: return DISK_D64_35;  /* D64 35-track no error */
    case 175531: return DISK_D64_35;  /* D64 35-track with error table */
    case 196608: return DISK_D64_40;  /* D64 40-track no error */
    case 197376: return DISK_D64_40;  /* D64 40-track with error table */
    case 349696: return DISK_D71;     /* D71 no error */
    case 351062: return DISK_D71;     /* D71 with error table */
    case 533248: return DISK_D80;
    case 819200: return DISK_D81;
    case 1066496: return DISK_D82;
    }

    /* Extension fallback */
    if (!filename) return DISK_UNKNOWN;
    const char *dot = strrchr(filename, '.');
    if (!dot) return DISK_UNKNOWN;
    const char *ext = dot + 1;

    if (strcasecmp(ext, "d64") == 0) return DISK_D64_35;
    if (strcasecmp(ext, "d71") == 0) return DISK_D71;
    if (strcasecmp(ext, "d80") == 0) return DISK_D80;
    if (strcasecmp(ext, "d81") == 0) return DISK_D81;
    if (strcasecmp(ext, "d82") == 0) return DISK_D82;
    if (strcasecmp(ext, "t64") == 0) return DISK_T64;

    return DISK_UNKNOWN;
}

int cbm_valid_image(const ArcHandle *h) {
    if (!h->image || h->image_size == 0) return 0;
    if (h->disk_type == DISK_UNKNOWN) return 0;
    if (h->disk_type == DISK_T64) return 1;

    /* Verify directory track/BAM sector is accessible */
    int dir_t = (h->disk_type == DISK_D80 || h->disk_type == DISK_D82)
                    ? D80_DIR_TRACK
                    : (h->disk_type == DISK_D81) ? D81_DIR_TRACK : D64_DIR_TRACK;
    return (cbm_sector(h, dir_t, 0) != NULL);
}

/* ============================================================
 * PETSCII conversion
 * ============================================================ */

void cbm_petscii_to_utf8(const uint8_t *petscii, int len, char *out, int outsize) {
    int j = 0;
    for (int i = 0; i < len && j < outsize - 1; i++) {
        uint8_t c = petscii[i];
        char mapped;
        if (c >= 0x41 && c <= 0x5A)
            mapped = 'a' + (c - 0x41);         /* PETSCII lowercase a-z */
        else if (c >= 0xC1 && c <= 0xDA)
            mapped = 'A' + (c - 0xC1);         /* PETSCII uppercase A-Z */
        else if (c >= 0x01 && c <= 0x1A)
            mapped = 'A' + (c - 0x01);         /* shifted A-Z (some editors) */
        else if (c == 0xA0)
            break;                              /* shifted space = CBM padding, stop */
        else if (c >= 0x20 && c <= 0x40)
            mapped = (char)c;                  /* ASCII-compatible range */
        else if (c == 0x5B)
            mapped = '[';
        else if (c == 0x5D)
            mapped = ']';
        else if (c == 0x00)
            break;
        else
            mapped = '_';                       /* graphics / control chars */
        out[j++] = mapped;
    }
    out[j] = '\0';
}

void cbm_utf8_to_petscii(const char *utf8, uint8_t *petscii, int len) {
    memset(petscii, 0xA0, len);  /* pad with shifted space */
    for (int i = 0; i < len && utf8[i]; i++) {
        unsigned char c = (unsigned char)utf8[i];
        uint8_t mapped;
        if (c >= 'a' && c <= 'z')
            mapped = 0x41 + (c - 'a');          /* lowercase → PETSCII */
        else if (c >= 'A' && c <= 'Z')
            mapped = 0xC1 + (c - 'A');          /* uppercase → PETSCII */
        else if (c >= 0x20 && c <= 0x40)
            mapped = c;
        else if (c == '[')
            mapped = 0x5B;
        else if (c == ']')
            mapped = 0x5D;
        else
            mapped = '_';
        petscii[i] = mapped;
    }
}

/* Replace characters illegal on the host filesystem with '_' */
void cbm_sanitize_filename(char *name) {
    static const char illegal[] = "\\/:*?\"<>|";
    for (char *p = name; *p; p++) {
        if (strchr(illegal, *p))
            *p = '_';
    }
}

/* ============================================================
 * Disk metadata
 * ============================================================ */

const char *cbm_type_ext(int cbm_type) {
    switch (cbm_type) {
    case CBM_TYPE_PRG: return ".prg";
    case CBM_TYPE_SEQ: return ".seq";
    case CBM_TYPE_USR: return ".usr";
    case CBM_TYPE_REL: return ".rel";
    case CBM_TYPE_DEL: return ".del";
    default:           return ".prg";
    }
}

const char *cbm_disk_type_name(int disk_type) {
    switch (disk_type) {
    case DISK_D64_35: return "D64 (35-track)";
    case DISK_D64_40: return "D64 (40-track)";
    case DISK_D71:    return "D71 (1571)";
    case DISK_D80:    return "D80 (8050)";
    case DISK_D81:    return "D81 (1581)";
    case DISK_D82:    return "D82 (8250)";
    case DISK_T64:    return "T64 (tape)";
    default:          return "unknown";
    }
}

void cbm_disk_name(const ArcHandle *h, char *name, int namesize) {
    int htrack, hsector;
    int name_off;

    if (h->disk_type == DISK_T64) {
        /* T64: tape name at offset 40 in header (24 bytes) */
        if (h->image_size < 64) { name[0] = '\0'; return; }
        cbm_petscii_to_utf8(h->image + 40, 24, name, namesize);
        return;
    }

    switch (h->disk_type) {
    case DISK_D64_35: case DISK_D64_40: case DISK_D71:
        htrack = 18; hsector = 0; name_off = 0x90; break;
    case DISK_D80: case DISK_D82:
        htrack = 38; hsector = 0; name_off = 0x06; break;
    case DISK_D81:
        htrack = 40; hsector = 0; name_off = 0x16; break;
    default:
        name[0] = '\0'; return;
    }

    const uint8_t *sec = cbm_sector(h, htrack, hsector);
    if (!sec) { name[0] = '\0'; return; }
    cbm_petscii_to_utf8(sec + name_off, 16, name, namesize);
}

/* ============================================================
 * Directory iteration (disk images)
 * ============================================================ */

static void get_dir_start(int disk_type, int *track, int *sector) {
    switch (disk_type) {
    case DISK_D80: case DISK_D82:
        *track = D80_DIR_TRACK; *sector = D80_FIRST_DIR_SECTOR; break;
    case DISK_D81:
        *track = D81_DIR_TRACK; *sector = D81_FIRST_DIR_SECTOR; break;
    default:  /* D64, D71 */
        *track = D64_DIR_TRACK; *sector = D64_FIRST_DIR_SECTOR; break;
    }
}

void cbm_dir_rewind(ArcHandle *h) {
    get_dir_start(h->disk_type, &h->dir_track, &h->dir_sector);
    h->dir_slot = 0;
    h->at_end = 0;
    h->t64_cur = 0;
}

/* Build display filename from raw PETSCII name + CBM file type.
 * Handles append_prg_ext setting. */
static void make_display_name(ArcHandle *h, const uint8_t *raw_name, int cbm_type,
                               char *out, int outsize) {
    char ascii[32];
    cbm_petscii_to_utf8(raw_name, 16, ascii, sizeof(ascii));
    cbm_sanitize_filename(ascii);

    if (h->append_prg_ext || cbm_type != CBM_TYPE_PRG) {
        snprintf(out, outsize, "%s%s", ascii, cbm_type_ext(cbm_type));
    } else {
        snprintf(out, outsize, "%s%s", ascii, cbm_type_ext(cbm_type));
    }
}

/* Advance directory to the next non-empty slot.  Returns:
 *   0              – h->cur_* populated with valid file
 *   E_END_ARCHIVE  – no more files
 *   E_BAD_ARCHIVE  – image corrupted */
int cbm_dir_next(ArcHandle *h) {
    int visited = 0;

    while (!h->at_end) {
        const uint8_t *sec = cbm_sector(h, h->dir_track, h->dir_sector);
        if (!sec) {
            h->at_end = 1;
            return E_BAD_ARCHIVE;
        }

        while (h->dir_slot < DIR_SLOTS_PER_SECTOR) {
            int slot = h->dir_slot++;
            int base = slot * DIR_SLOT_SIZE;
            uint8_t ftype = sec[base + DSLOT_FILE_TYPE];
            int cbm_type  = ftype & 0x0F;

            /* Deleted/empty slots */
            if (cbm_type == CBM_TYPE_DEL) {
                if (h->show_scratched || h->show_only_scratched) {
                    /* Show scratched files as hidden */
                } else {
                    continue;  /* skip */
                }
            } else {
                if (h->show_only_scratched) continue;
            }

            /* Skip unclosed files (bit 7 = 0) unless it's DEL */
            if (cbm_type != CBM_TYPE_DEL && !(ftype & CBM_CLOSED_BIT)) continue;

            uint8_t start_track  = sec[base + DSLOT_TRACK];
            uint8_t start_sector = sec[base + DSLOT_SECTOR];

            /* DEL files have no data chain */
            if (cbm_type == CBM_TYPE_DEL) {
                start_track = 0; start_sector = 0;
            }

            const uint8_t *raw_name = sec + base + DSLOT_NAME;
            uint32_t size_blocks =
                (uint32_t)sec[base + DSLOT_SIZE_LO] |
                ((uint32_t)sec[base + DSLOT_SIZE_HI] << 8);

            /* Populate current file info */
            h->cur_track       = start_track;
            h->cur_sector      = start_sector;
            h->cur_cbm_type    = cbm_type;
            h->cur_cbm_flags   = ftype;
            h->cur_size_blocks = size_blocks;
            memcpy(h->cur_raw_name, raw_name, 16);

            /* Compute actual byte size by traversing sector chain */
            if (start_track != 0 && cbm_type != CBM_TYPE_DEL) {
                cbm_calc_file_size(h, start_track, start_sector,
                                   &h->cur_size_bytes, NULL);
            } else {
                h->cur_size_bytes = 0;
            }

            make_display_name(h, raw_name, cbm_type,
                              h->cur_filename, sizeof(h->cur_filename));
            return 0;
        }

        /* Move to next directory sector */
        /* Chain link is at slot 0, bytes 0-1 */
        uint8_t next_track  = sec[0];
        uint8_t next_sector = sec[1];

        if (next_track == 0) {
            h->at_end = 1;
            return E_END_ARCHIVE;
        }

        /* Cyclic chain protection */
        if (++visited > MAX_DIR_ENTRIES) {
            h->at_end = 1;
            return E_BAD_ARCHIVE;
        }

        h->dir_track  = next_track;
        h->dir_sector = next_sector;
        h->dir_slot   = 0;
    }

    return E_END_ARCHIVE;
}

/* ============================================================
 * File size calculation
 * ============================================================ */

int cbm_calc_file_size(const ArcHandle *h, int start_track, int start_sector,
                       uint32_t *size_bytes, uint32_t *size_blocks) {
    if (start_track == 0) {
        if (size_bytes)  *size_bytes  = 0;
        if (size_blocks) *size_blocks = 0;
        return 0;
    }

    uint32_t bytes = 0, blocks = 0;
    int visited = 0;
    int t = start_track, s = start_sector;

    while (t != 0 && visited < MAX_CHAIN_SECTORS) {
        const uint8_t *sec = cbm_sector(h, t, s);
        if (!sec) break;

        uint8_t next_t = sec[0];
        uint8_t next_s = sec[1];
        blocks++;

        if (next_t == 0) {
            /* Last sector: next_s is last used byte index (2 = only byte[2] used) */
            bytes += (next_s > 1) ? (next_s - 1) : 0;
        } else {
            bytes += 254;  /* full data sector: bytes 2..255 */
        }

        t = next_t;
        s = next_s;
        visited++;
    }

    if (size_bytes)  *size_bytes  = bytes;
    if (size_blocks) *size_blocks = blocks;
    return 0;
}

/* ============================================================
 * File extraction
 * ============================================================ */

int cbm_extract_file(ArcHandle *h, const char *dest_path) {
    if (h->cur_track == 0) {
        /* Zero-length file: create empty file */
        FILE *f = fopen(dest_path, "wb");
        if (!f) return E_ECREATE;
        fclose(f);
        return 0;
    }

    FILE *out = fopen(dest_path, "wb");
    if (!out) return E_ECREATE;

    int t = h->cur_track, s = h->cur_sector;
    int visited = 0;
    int ret = 0;

    while (t != 0 && visited < MAX_CHAIN_SECTORS) {
        const uint8_t *sec = cbm_sector(h, t, s);
        if (!sec) { ret = E_BAD_DATA; break; }

        uint8_t next_t = sec[0];
        uint8_t next_s = sec[1];

        size_t write_len;
        if (next_t == 0) {
            write_len = (next_s > 1) ? (size_t)(next_s - 1) : 0;
        } else {
            write_len = 254;
        }

        if (write_len > 0) {
            if (fwrite(sec + 2, 1, write_len, out) != write_len) {
                ret = E_EWRITE; break;
            }
        }

        if (h->process_data) {
            char fname_trunc[32];
            snprintf(fname_trunc, sizeof(fname_trunc), "%.31s", h->cur_filename);
            if (!h->process_data(fname_trunc, (int)write_len)) {
                ret = E_EABORTED; break;
            }
        }

        t = next_t;
        s = next_s;
        visited++;
    }

    fclose(out);
    if (ret != 0 && ret != E_EABORTED) {
        /* Remove partial output on error */
        remove(dest_path);
    }
    return ret;
}

/* ============================================================
 * BAM management (D64/D71 only for now)
 * ============================================================ */

static uint8_t *d64_bam_entry(ArcHandle *h, int track) {
    /* Returns pointer to 4-byte BAM entry for track (1-based) */
    int bam_track, bam_sector;
    if (h->disk_type == DISK_D71 && track > 35) {
        bam_track = D71_BAM2_TRACK;
        bam_sector = D71_BAM2_SECTOR;
        track -= 35;
    } else {
        bam_track = D64_BAM_TRACK;
        bam_sector = D64_BAM_SECTOR;
    }
    uint8_t *bam = cbm_sector(h, bam_track, bam_sector);
    if (!bam) return NULL;
    return bam + D64_BAM_ENTRY_OFF + (track - 1) * 4;
}

static uint8_t *d81_bam_entry(ArcHandle *h, int track) {
    int bam_sector = (track <= 40) ? D81_BAM1_SECTOR : D81_BAM2_SECTOR;
    int rel_track  = (track <= 40) ? track : track - 40;
    uint8_t *bam = cbm_sector(h, D81_DIR_TRACK, bam_sector);
    if (!bam) return NULL;
    /* D81 BAM: 6 bytes per track starting at offset $10 (BAM1) or $02 (BAM2) */
    int base = (track <= 40) ? 0x10 : 0x02;
    return bam + base + (rel_track - 1) * 6;
}

static uint8_t *d80_bam_entry(ArcHandle *h, int track) {
    /* D80/D82: BAM sectors at T38, multiple sectors
     * BAM sector 0: tracks 1-50 (approx), BAM sector 3: tracks 51-77
     * Each entry: 1 free byte + ceil(sectors/8) bitmap bytes */
    int rel_track;
    int bam_sector_num;

    if (h->disk_type == DISK_D82 && track > 77) {
        /* Side 2 */
        int bam2_track = 116;  /* T116/S0 and T116/S3 for D82 side 2 */
        track -= 77;
        rel_track = track;
        if (rel_track <= 50) {
            uint8_t *bam = cbm_sector(h, bam2_track, 0);
            if (!bam) return NULL;
            return bam + 0x06 + (rel_track - 1) * 5;
        } else {
            uint8_t *bam = cbm_sector(h, bam2_track, 3);
            if (!bam) return NULL;
            return bam + 0x06 + (rel_track - 51) * 5;
        }
    }

    if (track <= 50) {
        bam_sector_num = 0;
        rel_track = track;
        uint8_t *bam = cbm_sector(h, D80_BAM_TRACK, bam_sector_num);
        if (!bam) return NULL;
        return bam + 0x06 + (rel_track - 1) * 5;
    } else {
        bam_sector_num = 3;
        rel_track = track - 50;
        uint8_t *bam = cbm_sector(h, D80_BAM_TRACK, bam_sector_num);
        if (!bam) return NULL;
        return bam + 0x06 + (rel_track - 1) * 5;
    }
}

int cbm_bam_is_free(const ArcHandle *h, int track, int sector) {
    /* Cast away const – we don't modify, just read */
    ArcHandle *hm = (ArcHandle *)h;
    uint8_t *entry = NULL;
    int bmap_byte_size;

    switch (h->disk_type) {
    case DISK_D64_35: case DISK_D64_40: case DISK_D71:
        entry = d64_bam_entry(hm, track);
        if (!entry) return 0;
        /* 3-byte bitmap starting at entry[1] */
        return (entry[1 + sector / 8] >> (sector % 8)) & 1;
    case DISK_D81:
        entry = d81_bam_entry(hm, track);
        if (!entry) return 0;
        /* 5-byte bitmap starting at entry[1] */
        return (entry[1 + sector / 8] >> (sector % 8)) & 1;
    case DISK_D80: case DISK_D82:
        entry = d80_bam_entry(hm, track);
        if (!entry) return 0;
        /* 4-byte bitmap starting at entry[1] (for up to 29 sectors) */
        bmap_byte_size = (cbm_sectors_per_track(h->disk_type, track) + 7) / 8;
        if (sector / 8 >= bmap_byte_size) return 0;
        return (entry[1 + sector / 8] >> (sector % 8)) & 1;
    default:
        return 0;
    }
}

void cbm_bam_mark_used(ArcHandle *h, int track, int sector) {
    uint8_t *entry = NULL;
    switch (h->disk_type) {
    case DISK_D64_35: case DISK_D64_40: case DISK_D71:
        entry = d64_bam_entry(h, track);
        if (!entry) return;
        if ((entry[1 + sector / 8] >> (sector % 8)) & 1) {
            entry[1 + sector / 8] &= ~(1 << (sector % 8));
            if (entry[0] > 0) entry[0]--;
        }
        break;
    case DISK_D81:
        entry = d81_bam_entry(h, track);
        if (!entry) return;
        if ((entry[1 + sector / 8] >> (sector % 8)) & 1) {
            entry[1 + sector / 8] &= ~(1 << (sector % 8));
            if (entry[0] > 0) entry[0]--;
        }
        break;
    case DISK_D80: case DISK_D82:
        entry = d80_bam_entry(h, track);
        if (!entry) return;
        if ((entry[1 + sector / 8] >> (sector % 8)) & 1) {
            entry[1 + sector / 8] &= ~(1 << (sector % 8));
            if (entry[0] > 0) entry[0]--;
        }
        break;
    }
    h->dirty = 1;
}

void cbm_bam_mark_free(ArcHandle *h, int track, int sector) {
    uint8_t *entry = NULL;
    switch (h->disk_type) {
    case DISK_D64_35: case DISK_D64_40: case DISK_D71:
        entry = d64_bam_entry(h, track);
        if (!entry) return;
        if (!((entry[1 + sector / 8] >> (sector % 8)) & 1)) {
            entry[1 + sector / 8] |= (1 << (sector % 8));
            entry[0]++;
        }
        break;
    case DISK_D81:
        entry = d81_bam_entry(h, track);
        if (!entry) return;
        if (!((entry[1 + sector / 8] >> (sector % 8)) & 1)) {
            entry[1 + sector / 8] |= (1 << (sector % 8));
            entry[0]++;
        }
        break;
    case DISK_D80: case DISK_D82:
        entry = d80_bam_entry(h, track);
        if (!entry) return;
        if (!((entry[1 + sector / 8] >> (sector % 8)) & 1)) {
            entry[1 + sector / 8] |= (1 << (sector % 8));
            entry[0]++;
        }
        break;
    }
    h->dirty = 1;
}

int cbm_bam_free_count(const ArcHandle *h, int track) {
    ArcHandle *hm = (ArcHandle *)h;
    uint8_t *entry = NULL;
    switch (h->disk_type) {
    case DISK_D64_35: case DISK_D64_40: case DISK_D71:
        entry = d64_bam_entry(hm, track); break;
    case DISK_D81:
        entry = d81_bam_entry(hm, track); break;
    case DISK_D80: case DISK_D82:
        entry = d80_bam_entry(hm, track); break;
    default: return 0;
    }
    return entry ? entry[0] : 0;
}

/* Find a free sector near near_track using a simple interleave scan.
 * Returns 0 on success, E_NO_FILES if disk full. */
int cbm_alloc_sector(ArcHandle *h, int near_track, int *out_track, int *out_sector) {
    int max_tracks = cbm_max_tracks(h->disk_type);
    int dir_track;
    switch (h->disk_type) {
    case DISK_D80: case DISK_D82: dir_track = D80_DIR_TRACK; break;
    case DISK_D81: dir_track = D81_DIR_TRACK; break;
    default: dir_track = D64_DIR_TRACK; break;
    }

    /* Search from near_track outwards, skip dir/BAM track */
    for (int delta = 0; delta < max_tracks; delta++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            int t = near_track + sign * delta;
            if (delta == 0 && sign == 1) continue;  /* don't double-count */
            if (t < 1 || t > max_tracks) continue;
            if (t == dir_track) continue;

            int spt = cbm_sectors_per_track(h->disk_type, t);
            if (spt <= 0) continue;
            if (cbm_bam_free_count(h, t) == 0) continue;

            for (int s = 0; s < spt; s++) {
                if (cbm_bam_is_free(h, t, s)) {
                    cbm_bam_mark_used(h, t, s);
                    *out_track = t;
                    *out_sector = s;
                    return 0;
                }
            }
        }
    }
    return E_NO_FILES;
}

int cbm_count_free_blocks(const ArcHandle *h) {
    int total = 0;
    int max = cbm_max_tracks(h->disk_type);
    int dir_track;
    switch (h->disk_type) {
    case DISK_D80: case DISK_D82: dir_track = D80_DIR_TRACK; break;
    case DISK_D81: dir_track = D81_DIR_TRACK; break;
    default: dir_track = D64_DIR_TRACK; break;
    }
    for (int t = 1; t <= max; t++) {
        if (t == dir_track) continue;
        total += cbm_bam_free_count(h, t);
    }
    return total;
}

/* ============================================================
 * Image creation
 * ============================================================ */

/* Compute total image size (sectors) for disk type */
static size_t disk_image_size(int disk_type) {
    int max = cbm_max_tracks(disk_type);
    size_t sectors = 0;
    for (int t = 1; t <= max; t++) {
        int spt = cbm_sectors_per_track(disk_type, t);
        if (spt > 0) sectors += spt;
    }
    return sectors * SECTOR_SIZE;
}

static void init_d64_bam(ArcHandle *h, const char *disk_name, const char *disk_id,
                          int disk_type) {
    uint8_t *bam = cbm_sector(h, D64_BAM_TRACK, D64_BAM_SECTOR);
    if (!bam) return;

    memset(bam, 0, SECTOR_SIZE);
    bam[0] = D64_DIR_TRACK;    /* link to first dir sector */
    bam[1] = D64_FIRST_DIR_SECTOR;
    bam[2] = 0x41;              /* DOS version 'A' */
    bam[3] = (disk_type == DISK_D71) ? 0x80 : 0x00;

    int max_t = (disk_type == DISK_D64_40) ? 40 :
                (disk_type == DISK_D71)    ? 35 : 35;

    for (int t = 1; t <= max_t; t++) {
        if (t == D64_DIR_TRACK) continue;  /* skip dir track */
        int spt = cbm_sectors_per_track(disk_type, t);
        uint8_t *entry = bam + D64_BAM_ENTRY_OFF + (t - 1) * 4;
        entry[0] = (uint8_t)spt;
        /* Set all sector bits free: fill bitmap bytes */
        uint32_t mask = (1u << spt) - 1;
        entry[1] = mask & 0xFF;
        entry[2] = (mask >> 8) & 0xFF;
        entry[3] = (mask >> 16) & 0xFF;
    }
    /* Dir track: mark all sectors used except dir sectors themselves
     * (sector 0 = BAM, sectors 1..18 = dir) */
    {
        uint8_t *entry = bam + D64_BAM_ENTRY_OFF + (D64_DIR_TRACK - 1) * 4;
        entry[0] = 0;
        entry[1] = 0; entry[2] = 0; entry[3] = 0;
    }

    /* Disk name (padded with $A0) */
    memset(bam + D64_BAM_NAME_OFF, 0xA0, 18);
    if (disk_name) {
        uint8_t petscii[16];
        cbm_utf8_to_petscii(disk_name, petscii, 16);
        memcpy(bam + D64_BAM_NAME_OFF, petscii, 16);
    }
    /* Disk ID */
    bam[D64_BAM_ID_OFF] = disk_id ? (uint8_t)disk_id[0] : '0';
    bam[D64_BAM_ID_OFF + 1] = disk_id ? (uint8_t)disk_id[1] : '0';
    bam[0xA4] = 0xA0;
    bam[0xA5] = '2';
    bam[0xA6] = 'A';
    for (int i = 0xA7; i < SECTOR_SIZE; i++) bam[i] = 0xA0;
}

static void init_d64_dir(ArcHandle *h) {
    uint8_t *dir = cbm_sector(h, D64_DIR_TRACK, D64_FIRST_DIR_SECTOR);
    if (!dir) return;
    memset(dir, 0, SECTOR_SIZE);
    dir[0] = 0x00;  /* no more dir sectors */
    dir[1] = 0xFF;
    /* All 8 directory slots are cleared to $00 (DEL/empty) */
}

static void init_d81_bam(ArcHandle *h, const char *disk_name, const char *disk_id) {
    uint8_t *bam1 = cbm_sector(h, D81_DIR_TRACK, D81_BAM1_SECTOR);
    uint8_t *bam2 = cbm_sector(h, D81_DIR_TRACK, D81_BAM2_SECTOR);
    if (!bam1 || !bam2) return;

    memset(bam1, 0, SECTOR_SIZE);
    memset(bam2, 0, SECTOR_SIZE);

    /* BAM1 link to BAM2 */
    bam1[0] = D81_DIR_TRACK;
    bam1[1] = D81_BAM2_SECTOR;
    bam1[2] = 0x44;  /* DOS 'D' */
    bam1[3] = 0x00;
    /* Disk ID at offset 4 */
    bam1[4] = disk_id ? (uint8_t)disk_id[0] : '0';
    bam1[5] = disk_id ? (uint8_t)disk_id[1] : '0';
    bam1[6] = 0x32;  /* '2' */
    bam1[7] = 0x44;  /* 'D' */

    /* Disk name at offset $16 in BAM1 */
    memset(bam1 + 0x16, 0xA0, 16);
    if (disk_name) {
        uint8_t petscii[16];
        cbm_utf8_to_petscii(disk_name, petscii, 16);
        memcpy(bam1 + 0x16, petscii, 16);
    }

    /* BAM1: 6-byte entries for tracks 1-40 starting at offset $10 */
    for (int t = 1; t <= 40; t++) {
        if (t == D81_DIR_TRACK) continue;
        int spt = 40;
        uint8_t *entry = bam1 + 0x10 + (t - 1) * 6;
        entry[0] = (uint8_t)spt;
        /* 40-bit bitmap: set all bits */
        entry[1] = 0xFF; entry[2] = 0xFF;
        entry[3] = 0xFF; entry[4] = 0xFF;
        entry[5] = 0xFF;
    }
    /* Dir track itself: all used */
    {
        uint8_t *entry = bam1 + 0x10 + (D81_DIR_TRACK - 1) * 6;
        entry[0] = 0;
        memset(entry + 1, 0, 5);
    }

    /* BAM2 link to first dir sector */
    bam2[0] = D81_DIR_TRACK;
    bam2[1] = D81_FIRST_DIR_SECTOR;
    /* BAM2: 6-byte entries for tracks 41-80 starting at offset $02 */
    for (int t = 41; t <= 80; t++) {
        uint8_t *entry = bam2 + 0x02 + (t - 41) * 6;
        entry[0] = 40;
        entry[1] = 0xFF; entry[2] = 0xFF;
        entry[3] = 0xFF; entry[4] = 0xFF;
        entry[5] = 0xFF;
    }
}

int cbm_create_image(ArcHandle *h, int disk_type, const char *disk_name,
                     const char *disk_id) {
    if (disk_type == DISK_T64) return E_NOT_SUPPORTED;

    size_t sz = disk_image_size(disk_type);
    if (sz == 0) return E_NOT_SUPPORTED;

    uint8_t *img = calloc(1, sz);
    if (!img) return E_NO_MEMORY;

    /* Fill unused data with 0x01 (CBM unformatted track filler) */
    memset(img, 0x01, sz);

    free(h->image);
    h->image      = img;
    h->image_size = sz;
    h->disk_type  = disk_type;
    h->dirty      = 1;

    switch (disk_type) {
    case DISK_D64_35: case DISK_D64_40: case DISK_D71:
        init_d64_bam(h, disk_name, disk_id, disk_type);
        init_d64_dir(h);
        /* For D71: also init side 2 BAM at T53/S0 */
        if (disk_type == DISK_D71) {
            /* Copy D64 BAM structure for tracks 36-70 into T53/S0 */
            uint8_t *bam2 = cbm_sector(h, D71_BAM2_TRACK, D71_BAM2_SECTOR);
            if (bam2) {
                memset(bam2, 0, SECTOR_SIZE);
                bam2[0] = 0x00; bam2[1] = 0xFF;
                bam2[2] = 0x44;  /* 'D' - 1571 DOS */
                for (int t = 36; t <= 70; t++) {
                    int spt = cbm_sectors_per_track(disk_type, t);
                    uint8_t *entry = bam2 + 4 + (t - 36) * 4;
                    entry[0] = spt;
                    uint32_t mask = (1u << spt) - 1;
                    entry[1] = mask & 0xFF;
                    entry[2] = (mask >> 8) & 0xFF;
                    entry[3] = (mask >> 16) & 0xFF;
                }
            }
        }
        break;
    case DISK_D81:
        init_d81_bam(h, disk_name, disk_id);
        {
            uint8_t *dir = cbm_sector(h, D81_DIR_TRACK, D81_FIRST_DIR_SECTOR);
            if (dir) { memset(dir, 0, SECTOR_SIZE); dir[0] = 0; dir[1] = 0xFF; }
        }
        break;
    default:
        /* D80/D82: basic init */
        break;
    }

    return 0;
}

/* ============================================================
 * Directory management
 * ============================================================ */

/* Find a free directory entry. Returns pointer to the 30-byte data area
 * within the slot, or NULL if directory is full.
 * may_extend: if nonzero, try to add a new directory sector. */
static uint8_t *find_free_dir_slot(ArcHandle *h) {
    int t, s;
    get_dir_start(h->disk_type, &t, &s);
    int dir_track = t;
    int visited = 0;

    while (t != 0 && visited < MAX_DIR_ENTRIES) {
        uint8_t *sec = cbm_sector(h, t, s);
        if (!sec) return NULL;

        for (int slot = 0; slot < DIR_SLOTS_PER_SECTOR; slot++) {
            /* Skip slot 0 of first dir sector (bytes 0-1 are chain link) */
            uint8_t *slot_base = sec + slot * DIR_SLOT_SIZE;
            uint8_t ftype = slot_base[DSLOT_FILE_TYPE];
            if ((ftype & 0x0F) == CBM_TYPE_DEL) {
                /* Empty or scratched slot: reuse it */
                memset(slot_base + 2, 0, DIR_SLOT_SIZE - 2);
                return slot_base + 2;  /* points to file_type field */
            }
        }

        uint8_t next_t = sec[0];
        uint8_t next_s = sec[1];

        if (next_t == 0) {
            /* Allocate a new directory sector */
            int new_t, new_s;
            if (cbm_alloc_sector(h, dir_track, &new_t, &new_s) != 0)
                return NULL;
            /* Mark current sector as pointing to new */
            sec[0] = (uint8_t)new_t;
            sec[1] = (uint8_t)new_s;
            h->dirty = 1;

            uint8_t *new_sec = cbm_sector(h, new_t, new_s);
            if (!new_sec) return NULL;
            memset(new_sec, 0, SECTOR_SIZE);
            new_sec[0] = 0; new_sec[1] = 0xFF;

            /* Return first slot in new sector */
            return new_sec + DIR_SLOT_SIZE * 0 + 2;
        }

        t = next_t; s = next_s;
        visited++;
    }
    return NULL;
}

int cbm_count_free_dir_entries(const ArcHandle *h) {
    int t, s;
    get_dir_start(h->disk_type, &t, &s);
    int count = 0, visited = 0;

    while (t != 0 && visited < MAX_DIR_ENTRIES) {
        const uint8_t *sec = cbm_sector(h, t, s);
        if (!sec) break;
        for (int slot = 0; slot < DIR_SLOTS_PER_SECTOR; slot++) {
            const uint8_t *slot_base = sec + slot * DIR_SLOT_SIZE;
            if ((slot_base[DSLOT_FILE_TYPE] & 0x0F) == CBM_TYPE_DEL) count++;
        }
        uint8_t next_t = sec[0], next_s = sec[1];
        if (next_t == 0) break;
        t = next_t; s = next_s;
        visited++;
    }
    return count;
}

/* ============================================================
 * File writing
 * ============================================================ */

int cbm_write_file(ArcHandle *h, const char *src_path,
                   const char *cbm_name, int cbm_type) {
    FILE *in = fopen(src_path, "rb");
    if (!in) return E_EOPEN;

    /* Determine file size */
    fseek(in, 0, SEEK_END);
    fseek(in, 0, SEEK_SET);

    /* Find a free directory slot */
    uint8_t *dent = find_free_dir_slot(h);
    if (!dent) { fclose(in); return E_NO_FILES; }

    int first_t = 0, first_s = 0;
    uint8_t *prev_sec = NULL;
    uint32_t blocks = 0;
    uint8_t buf[254];

    int near_track;
    switch (h->disk_type) {
    case DISK_D80: case DISK_D82: near_track = D80_DIR_TRACK - 1; break;
    case DISK_D81: near_track = D81_DIR_TRACK - 1; break;
    default: near_track = D64_DIR_TRACK - 1; break;
    }
    if (near_track < 1) near_track = 1;

    while (1) {
        size_t n = fread(buf, 1, 254, in);
        if (n == 0 && blocks > 0) break;  /* done */

        int new_t, new_s;
        if (cbm_alloc_sector(h, near_track, &new_t, &new_s) != 0) {
            fclose(in);
            return E_NO_FILES;
        }
        near_track = new_t;

        uint8_t *sec = cbm_sector(h, new_t, new_s);
        if (!sec) { fclose(in); return E_BAD_DATA; }
        memset(sec, 0, SECTOR_SIZE);

        if (n == 0) {
            /* Empty file: write one sector with no data */
            sec[0] = 0;
            sec[1] = 0x01;  /* 0 data bytes */
        } else if (n < 254) {
            /* Last sector */
            sec[0] = 0x00;
            sec[1] = (uint8_t)(n + 1);  /* last used byte index */
            memcpy(sec + 2, buf, n);
        } else {
            sec[0] = 0x00;  /* will be updated when we get next sector */
            sec[1] = 0xFF;
            memcpy(sec + 2, buf, 254);
        }

        if (prev_sec) {
            prev_sec[0] = (uint8_t)new_t;
            prev_sec[1] = (uint8_t)new_s;
        } else {
            first_t = new_t;
            first_s = new_s;
        }
        prev_sec = sec;
        blocks++;

        if (n < 254) break;  /* last sector written */
    }
    fclose(in);

    /* Fill directory entry */
    uint8_t petscii_name[16];
    cbm_utf8_to_petscii(cbm_name, petscii_name, 16);

    dent[0] = (uint8_t)(CBM_CLOSED_BIT | cbm_type);  /* file type, closed */
    dent[1] = (uint8_t)first_t;
    dent[2] = (uint8_t)first_s;
    memcpy(dent + 3, petscii_name, 16);
    memset(dent + 3 + 16, 0, 6);  /* side sector, rel len, unused */
    dent[28] = (uint8_t)(blocks & 0xFF);
    dent[29] = (uint8_t)(blocks >> 8);

    h->dirty = 1;
    return 0;
}

/* ============================================================
 * File deletion
 * ============================================================ */

int cbm_delete_file(ArcHandle *h, const uint8_t *raw_name, int cbm_type) {
    int t, s;
    get_dir_start(h->disk_type, &t, &s);
    int visited = 0;

    while (t != 0 && visited < MAX_DIR_ENTRIES) {
        uint8_t *sec = cbm_sector(h, t, s);
        if (!sec) break;

        for (int slot = 0; slot < DIR_SLOTS_PER_SECTOR; slot++) {
            uint8_t *slot_base = sec + slot * DIR_SLOT_SIZE;
            uint8_t ftype = slot_base[DSLOT_FILE_TYPE];
            if ((ftype & 0x0F) == CBM_TYPE_DEL) continue;
            if ((ftype & 0x0F) != cbm_type) continue;
            if (memcmp(slot_base + DSLOT_NAME, raw_name, 16) != 0) continue;

            /* Found: free the sector chain */
            int ft = slot_base[DSLOT_TRACK];
            int fs = slot_base[DSLOT_SECTOR];
            int chain_visited = 0;

            while (ft != 0 && chain_visited < MAX_CHAIN_SECTORS) {
                uint8_t *dsec = cbm_sector(h, ft, fs);
                if (!dsec) break;
                int next_ft = dsec[0];
                int next_fs = dsec[1];

                if (h->erase_deleted_sectors)
                    memset(dsec, 0, SECTOR_SIZE);

                cbm_bam_mark_free(h, ft, fs);
                ft = next_ft; fs = next_fs;
                chain_visited++;
            }

            /* Scratch the directory entry */
            slot_base[DSLOT_FILE_TYPE] = CBM_TYPE_DEL;
            h->dirty = 1;
            return 0;
        }

        uint8_t next_t = sec[0], next_s = sec[1];
        if (next_t == 0) break;
        t = next_t; s = next_s;
        visited++;
    }
    return E_NO_FILES;
}

/* ============================================================
 * T64 support
 * ============================================================ */

int cbm_t64_read_header(ArcHandle *h) {
    if (h->image_size < T64_HEADER_SIZE) return E_BAD_ARCHIVE;

    /* Check magic: first bytes should be "C64S" */
    if (memcmp(h->image, "C64S", 4) != 0 &&
        memcmp(h->image, "C64 ", 4) != 0) {
        /* Some T64 files start with "C64s tape image" */
        if (strncmp((char *)h->image, "C64", 3) != 0)
            return E_BAD_ARCHIVE;
    }

    /* Bytes 32-33: tape version; 34-35: max entries; 36-37: used entries */
    h->t64_max  = h->image[34] | ((int)h->image[35] << 8);
    h->t64_used = h->image[36] | ((int)h->image[37] << 8);

    /* Sanity check */
    if (h->t64_max > 1000) h->t64_max = 1000;
    if (h->t64_used > h->t64_max) h->t64_used = h->t64_max;
    if (h->t64_used == 0 && h->t64_max > 0) h->t64_used = h->t64_max;

    h->t64_cur = 0;
    return 0;
}

int cbm_t64_next(ArcHandle *h) {
    while (h->t64_cur < h->t64_used) {
        int idx = h->t64_cur++;
        size_t entry_off = T64_HEADER_SIZE + (size_t)idx * T64_ENTRY_SIZE;
        if (entry_off + T64_ENTRY_SIZE > h->image_size) {
            h->at_end = 1;
            return E_END_ARCHIVE;
        }

        const uint8_t *e = h->image + entry_off;
        uint8_t c64s_type = e[0];
        /* c64s_type: 0 = free/empty slot, 1 = normal tape, 3 = ROM */
        if (c64s_type == 0) continue;

        uint16_t start_addr = e[2] | ((uint16_t)e[3] << 8);
        uint16_t end_addr   = e[4] | ((uint16_t)e[5] << 8);
        uint32_t data_off   = e[8]  | ((uint32_t)e[9] << 8) |
                             ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);

        uint32_t data_len = (end_addr > start_addr) ? (end_addr - start_addr) : 0;

        /* T64 "off by one" bug: some tools write wrong end_addr */
        if (data_off + data_len > h->image_size)
            data_len = (uint32_t)(h->image_size - data_off);

        h->cur_track      = 0;
        h->cur_sector     = 0;
        h->cur_cbm_type   = e[1] ? (e[1] & 0x0F) : CBM_TYPE_PRG;
        h->cur_cbm_flags  = CBM_CLOSED_BIT | h->cur_cbm_type;
        h->cur_size_bytes  = data_len;
        h->cur_size_blocks = data_len;  /* T64: both sizes in bytes */
        h->cur_t64_offset  = data_off;
        memcpy(h->cur_raw_name, e + 16, 16);

        char ascii_name[32];
        cbm_petscii_to_utf8(e + 16, 16, ascii_name, sizeof(ascii_name));
        cbm_sanitize_filename(ascii_name);
        snprintf(h->cur_filename, sizeof(h->cur_filename), "%s.prg", ascii_name);
        return 0;
    }
    h->at_end = 1;
    return E_END_ARCHIVE;
}

int cbm_t64_extract(ArcHandle *h, const char *dest_path) {
    if (h->cur_t64_offset + h->cur_size_bytes > h->image_size)
        return E_BAD_DATA;

    FILE *out = fopen(dest_path, "wb");
    if (!out) return E_ECREATE;

    size_t written = fwrite(h->image + h->cur_t64_offset, 1,
                            h->cur_size_bytes, out);
    fclose(out);
    return (written == h->cur_size_bytes) ? 0 : E_EWRITE;
}

/* ============================================================
 * Image save
 * ============================================================ */

int cbm_save_image(const ArcHandle *h) {
    FILE *f = fopen(h->arc_name, "wb");
    if (!f) return E_ECREATE;
    size_t written = fwrite(h->image, 1, h->image_size, f);
    fclose(f);
    return (written == h->image_size) ? 0 : E_EWRITE;
}
