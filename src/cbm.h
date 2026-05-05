#pragma once
#include <stdint.h>
#include <stddef.h>
#include "wcxhead.h"

/* Disk type constants */
#define DISK_D64_35     0   /* C64 1541, 35 tracks */
#define DISK_D64_40     1   /* C64 1541, 40 tracks (SpeedDos/DolphinDos) */
#define DISK_D71        2   /* C64 1571, 70 tracks */
#define DISK_D80        3   /* CBM 8050, 77 tracks */
#define DISK_D81        4   /* C64 1581, 80 tracks */
#define DISK_D82        5   /* CBM 8250, 154 tracks */
#define DISK_T64        6   /* C64 tape image */
#define DISK_UNKNOWN   -1

/* CBM file type bits (low nibble of directory file_type byte) */
#define CBM_TYPE_DEL    0
#define CBM_TYPE_SEQ    1
#define CBM_TYPE_PRG    2
#define CBM_TYPE_USR    3
#define CBM_TYPE_REL    4

#define CBM_CLOSED_BIT  0x80    /* bit 7: file properly closed */
#define CBM_LOCKED_BIT  0x40    /* bit 6: file locked */

/* D64 geometry */
#define D64_DIR_TRACK   18
#define D64_BAM_TRACK   18
#define D64_BAM_SECTOR  0
#define D64_FIRST_DIR_SECTOR 1
#define D64_BAM_NAME_OFF 0x90   /* disk name offset in BAM sector */
#define D64_BAM_ID_OFF   0xA2   /* disk ID offset in BAM sector */
#define D64_BAM_ENTRY_OFF 4     /* BAM entries start offset */

/* D71 geometry */
#define D71_BAM2_TRACK  53
#define D71_BAM2_SECTOR 0

/* D80/D82 geometry */
#define D80_DIR_TRACK   39
#define D80_BAM_TRACK   38
#define D80_FIRST_DIR_SECTOR 1

/* D81 geometry */
#define D81_DIR_TRACK   40
#define D81_FIRST_DIR_SECTOR 3
#define D81_BAM1_SECTOR 0
#define D81_BAM2_SECTOR 1

/* Sector size */
#define SECTOR_SIZE     256

/* Max files per directory (sane limit to avoid infinite loops) */
#define MAX_DIR_ENTRIES 1024

/* Max sectors to follow (cyclic chain protection) */
#define MAX_CHAIN_SECTORS 4096

/* Directory slot layout within a 256-byte directory sector:
 * - 8 slots of 32 bytes each (slots 0..7)
 * - Slot 0, bytes 0-1: chain link (next dir sector track/sector)
 * - All slots, bytes 2-31: directory entry data */
#define DIR_SLOTS_PER_SECTOR  8
#define DIR_SLOT_SIZE         32

/* Within a directory slot (offset from slot start): */
#define DSLOT_FILE_TYPE   2
#define DSLOT_TRACK       3
#define DSLOT_SECTOR      4
#define DSLOT_NAME        5    /* 16 bytes */
#define DSLOT_SS_TRACK    21
#define DSLOT_SS_SECTOR   22
#define DSLOT_REL_LEN     23
#define DSLOT_SIZE_LO     30
#define DSLOT_SIZE_HI     31

/* T64 layout */
#define T64_HEADER_SIZE    64
#define T64_ENTRY_SIZE     32
#define T64_MAGIC          "C64S"
#define T64_MAX_NAME       16

/* Archive handle */
typedef struct ArcHandle {
    uint8_t  *image;            /* disk image data in memory */
    size_t    image_size;       /* image file size */
    int       disk_type;
    char      arc_name[WCX_MAX_PATH];
    int       open_mode;        /* PK_OM_LIST or PK_OM_EXTRACT */
    int       dirty;            /* image was modified, needs writing back */

    /* Directory iteration state (for disk images) */
    int       dir_track;        /* current directory sector track */
    int       dir_sector;       /* current directory sector */
    int       dir_slot;         /* current slot within sector 0..7 */
    int       at_end;           /* nonzero when enumeration is finished */

    /* T64 state */
    int       t64_max;          /* total T64 entries */
    int       t64_used;         /* used T64 entries */
    int       t64_cur;          /* current T64 entry index */

    /* Current file info (populated by ReadHeader, consumed by ProcessFile) */
    int       cur_track;
    int       cur_sector;
    int       cur_cbm_type;     /* CBM_TYPE_PRG etc. */
    int       cur_cbm_flags;    /* raw file_type byte */
    char      cur_filename[WCX_MAX_PATH]; /* display filename (ASCII + ext) */
    uint8_t   cur_raw_name[16]; /* raw PETSCII name (for matching) */
    uint32_t  cur_size_bytes;
    uint32_t  cur_size_blocks;
    /* T64 extra */
    uint32_t  cur_t64_offset;   /* data offset within T64 file */

    /* Callbacks */
    tProcessDataProc process_data;
    tChangeVolProc   change_vol;

    /* Config (from INI) */
    int show_scratched;
    int show_only_scratched;
    int append_prg_ext;
    int ignore_error_table;
    int erase_deleted_sectors;
} ArcHandle;

/* ---- Sector layout functions ---- */
int  cbm_sectors_per_track(int disk_type, int track);
int  cbm_max_tracks(int disk_type);
long cbm_sector_offset(int disk_type, int track, int sector);
uint8_t *cbm_sector(const ArcHandle *h, int track, int sector);

/* ---- Disk type detection ---- */
int  cbm_detect_type(const char *filename, size_t filesize);
int  cbm_valid_image(const ArcHandle *h);

/* ---- PETSCII conversion ---- */
void cbm_petscii_to_utf8(const uint8_t *petscii, int len, char *out, int outsize);
void cbm_utf8_to_petscii(const char *utf8, uint8_t *petscii, int len);
void cbm_sanitize_filename(char *name);

/* ---- Disk metadata ---- */
void cbm_disk_name(const ArcHandle *h, char *name, int namesize);
const char *cbm_type_ext(int cbm_type);
const char *cbm_disk_type_name(int disk_type);

/* ---- Directory iteration ---- */
void cbm_dir_rewind(ArcHandle *h);
int  cbm_dir_next(ArcHandle *h);   /* fills h->cur_* fields; returns 0=ok, E_END_ARCHIVE, or error */

/* ---- File extraction ---- */
int  cbm_extract_file(ArcHandle *h, const char *dest_path);
int  cbm_calc_file_size(const ArcHandle *h, int start_track, int start_sector,
                        uint32_t *size_bytes, uint32_t *size_blocks);

/* ---- BAM management (write) ---- */
int  cbm_bam_is_free(const ArcHandle *h, int track, int sector);
void cbm_bam_mark_used(ArcHandle *h, int track, int sector);
void cbm_bam_mark_free(ArcHandle *h, int track, int sector);
int  cbm_bam_free_count(const ArcHandle *h, int track);
int  cbm_alloc_sector(ArcHandle *h, int near_track, int *out_track, int *out_sector);

/* ---- Image creation ---- */
int  cbm_create_image(ArcHandle *h, int disk_type, const char *disk_name, const char *disk_id);
int  cbm_write_file(ArcHandle *h, const char *src_path, const char *cbm_name, int cbm_type);
int  cbm_delete_file(ArcHandle *h, const uint8_t *raw_name, int cbm_type);
int  cbm_count_free_dir_entries(const ArcHandle *h);
int  cbm_count_free_blocks(const ArcHandle *h);

/* ---- T64 ---- */
int  cbm_t64_read_header(ArcHandle *h);
int  cbm_t64_next(ArcHandle *h);
int  cbm_t64_extract(ArcHandle *h, const char *dest_path);

/* ---- Image save ---- */
int  cbm_save_image(const ArcHandle *h);
