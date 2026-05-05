#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <libgen.h>
#include <errno.h>
#include "wcxhead.h"
#include "cbm.h"
#include "ini.h"

/* Diagnostic log — written only when log_level > 0 in cbmwcx.ini */
#define DLOG(fmt, ...) do { \
    if (g_cfg.log_level > 0) { \
        FILE *_lf = fopen("/tmp/cbmwcx_debug.log", "a"); \
        if (_lf) { fprintf(_lf, fmt "\n", ##__VA_ARGS__); fclose(_lf); } \
    } \
} while(0)

/* ============================================================
 * Plugin path discovery  (constructor runs at .so load time)
 * ============================================================ */

static char g_plugin_dir[WCX_MAX_PATH] = "";
static CbmConfig g_cfg;
static tProcessDataProc g_process_data = NULL;
static tChangeVolProc   g_change_vol   = NULL;

static void __attribute__((constructor)) plugin_init(void) {
    DLOG("plugin_init: start");
    ini_defaults(&g_cfg);
    Dl_info info;
    if (dladdr((void *)plugin_init, &info) && info.dli_fname) {
        char tmp[WCX_MAX_PATH];
        strncpy(tmp, info.dli_fname, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *slash = strrchr(tmp, '/');
        if (slash) {
            slash[1] = '\0';
            strncpy(g_plugin_dir, tmp, sizeof(g_plugin_dir) - 1);
        }
    }
    /* Load config */
    char ini_path[WCX_MAX_PATH];
    snprintf(ini_path, sizeof(ini_path), "%scbmwcx.ini", g_plugin_dir);
    ini_load(ini_path, &g_cfg);
    DLOG("plugin_init: done, dir=%s log_level=%d", g_plugin_dir, g_cfg.log_level);
}

/* ============================================================
 * UTF-16LE ↔ UTF-8 helpers
 * ============================================================ */

static void wcs_to_utf8(const uint16_t *src, char *dst, int dstlen) {
    int i = 0;
    while (*src && i < dstlen - 4) {
        uint32_t cp = *src++;
        /* Handle surrogates */
        if (cp >= 0xD800 && cp <= 0xDBFF && *src >= 0xDC00 && *src <= 0xDFFF)
            cp = 0x10000 + ((cp - 0xD800) << 10) + (*src++ - 0xDC00);

        if (cp < 0x80) {
            dst[i++] = (char)cp;
        } else if (cp < 0x800) {
            dst[i++] = 0xC0 | (cp >> 6);
            dst[i++] = 0x80 | (cp & 0x3F);
        } else if (cp < 0x10000) {
            dst[i++] = 0xE0 | (cp >> 12);
            dst[i++] = 0x80 | ((cp >> 6) & 0x3F);
            dst[i++] = 0x80 | (cp & 0x3F);
        } else {
            dst[i++] = 0xF0 | (cp >> 18);
            dst[i++] = 0x80 | ((cp >> 12) & 0x3F);
            dst[i++] = 0x80 | ((cp >> 6) & 0x3F);
            dst[i++] = 0x80 | (cp & 0x3F);
        }
    }
    dst[i] = '\0';
}

static void utf8_to_wcs(const char *src, uint16_t *dst, int dstlen) {
    int i = 0;
    const unsigned char *s = (const unsigned char *)src;
    while (*s && i < dstlen - 1) {
        uint32_t cp;
        if (*s < 0x80)              cp = *s++;
        else if ((*s & 0xE0) == 0xC0) { cp = (*s++ & 0x1F) << 6; cp |= (*s++ & 0x3F); }
        else if ((*s & 0xF0) == 0xE0) { cp = (*s++ & 0x0F) << 12; cp |= (*s++ & 0x3F) << 6; cp |= (*s++ & 0x3F); }
        else                        { cp = (*s++ & 0x07) << 18; cp |= (*s++ & 0x3F) << 12; cp |= (*s++ & 0x3F) << 6; cp |= (*s++ & 0x3F); }

        if (cp < 0x10000) {
            dst[i++] = (uint16_t)cp;
        } else {
            cp -= 0x10000;
            dst[i++] = 0xD800 | (cp >> 10);
            if (i < dstlen - 1) dst[i++] = 0xDC00 | (cp & 0x3FF);
        }
    }
    dst[i] = 0;
}

/* ============================================================
 * Handle allocation / free
 * ============================================================ */

static ArcHandle *alloc_handle(void) {
    ArcHandle *h = calloc(1, sizeof(ArcHandle));
    if (!h) return NULL;
    h->show_scratched        = g_cfg.show_scratched;
    h->show_only_scratched   = g_cfg.show_only_scratched;
    h->append_prg_ext        = g_cfg.append_prg_ext;
    h->ignore_error_table    = g_cfg.ignore_error_table;
    h->erase_deleted_sectors = g_cfg.erase_deleted_sectors;
    return h;
}

static int open_image(ArcHandle *h, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return E_EOPEN;

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsz <= 0) { fclose(f); return E_BAD_ARCHIVE; }

    h->image = malloc((size_t)fsz);
    if (!h->image) { fclose(f); return E_NO_MEMORY; }

    if (fread(h->image, 1, (size_t)fsz, f) != (size_t)fsz) {
        fclose(f); free(h->image); h->image = NULL; return E_EREAD;
    }
    fclose(f);
    h->image_size = (size_t)fsz;
    return 0;
}

static void free_handle(ArcHandle *h) {
    if (!h) return;
    free(h->image);
    free(h);
}

/* Extract source filename CBM name + type from a host filename.
 * E.g. "hello.prg" → name="hello", type=CBM_TYPE_PRG */
static void parse_src_filename(const char *src, char *cbm_name, int cbm_namelen,
                                int *cbm_type) {
    /* Take basename */
    char tmp[WCX_MAX_PATH];
    strncpy(tmp, src, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *base = basename(tmp);

    char *dot = strrchr(base, '.');
    *cbm_type = CBM_TYPE_PRG;  /* default */
    if (dot) {
        if (strcasecmp(dot, ".seq") == 0) *cbm_type = CBM_TYPE_SEQ;
        else if (strcasecmp(dot, ".usr") == 0) *cbm_type = CBM_TYPE_USR;
        else if (strcasecmp(dot, ".rel") == 0) *cbm_type = CBM_TYPE_REL;
        else if (strcasecmp(dot, ".prg") == 0) *cbm_type = CBM_TYPE_PRG;
        *dot = '\0';  /* strip extension */
    }

    /* Truncate to 16 PETSCII characters */
    strncpy(cbm_name, base, cbm_namelen - 1);
    cbm_name[cbm_namelen - 1] = '\0';
    if ((int)strlen(cbm_name) > 16) cbm_name[16] = '\0';
}

/* Build full destination path from dest_path + filename */
static void build_dest_path(const char *dest_path, const char *filename,
                             char *out, int outlen) {
    if (dest_path && *dest_path) {
        snprintf(out, outlen, "%s/%s", dest_path, filename);
    } else {
        snprintf(out, outlen, "%s", filename);
    }
}

/* Ensure directory exists, creating it if needed */
static void ensure_dir(const char *path) {
    char tmp[WCX_MAX_PATH];
    strncpy(tmp, path, sizeof(tmp) - 1);
    char *slash = strrchr(tmp, '/');
    if (!slash) return;
    *slash = '\0';
    if (*tmp) mkdir(tmp, 0755);  /* ignore error if exists */
}

/* ============================================================
 * WCX API — ANSI versions
 * ============================================================ */

void *OpenArchive(tOpenArchiveData *ArchiveData) {
    DLOG("OpenArchive: %s", ArchiveData ? (ArchiveData->ArcName ? ArchiveData->ArcName : "(null name)") : "(null)");
    if (!ArchiveData) return NULL;
    if (!ArchiveData->ArcName) { ArchiveData->OpenResult = E_EOPEN; return NULL; }

    ArcHandle *h = alloc_handle();
    if (!h) { ArchiveData->OpenResult = E_NO_MEMORY; return NULL; }

    strncpy(h->arc_name, ArchiveData->ArcName, sizeof(h->arc_name) - 1);
    h->open_mode = ArchiveData->OpenMode;

    int ret = open_image(h, h->arc_name);
    if (ret != 0) {
        ArchiveData->OpenResult = ret;
        free_handle(h);
        return NULL;
    }

    h->disk_type = cbm_detect_type(h->arc_name, h->image_size);
    if (h->disk_type == DISK_UNKNOWN) {
        ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
        free_handle(h);
        return NULL;
    }

    if (!cbm_valid_image(h)) {
        ArchiveData->OpenResult = E_BAD_ARCHIVE;
        free_handle(h);
        return NULL;
    }

    if (h->disk_type == DISK_T64) {
        if (cbm_t64_read_header(h) != 0) {
            ArchiveData->OpenResult = E_BAD_ARCHIVE;
            free_handle(h);
            return NULL;
        }
    } else {
        cbm_dir_rewind(h);
    }

    ArchiveData->OpenResult = 0;
    return h;
}

int ReadHeader(void *hArcData, tHeaderData *HeaderData) {
    DLOG("ReadHeader");
    ArcHandle *h = (ArcHandle *)hArcData;
    if (!h || h->at_end) return E_END_ARCHIVE;

    int ret = (h->disk_type == DISK_T64) ? cbm_t64_next(h) : cbm_dir_next(h);
    if (ret != 0) return ret;

    memset(HeaderData, 0, sizeof(*HeaderData));
    strncpy(HeaderData->ArcName,  h->arc_name,      sizeof(HeaderData->ArcName) - 1);
    strncpy(HeaderData->FileName, h->cur_filename,   sizeof(HeaderData->FileName) - 1);
    HeaderData->PackSize = (int)h->cur_size_blocks;
    HeaderData->UnpSize  = (int)h->cur_size_bytes;
    HeaderData->FileAttr = ((h->cur_cbm_flags & 0x0F) == CBM_TYPE_DEL) ? FA_HIDDEN : FA_ARCH;
    if (h->cur_cbm_flags & CBM_LOCKED_BIT) HeaderData->FileAttr |= FA_READONLY;
    return 0;
}

int ReadHeaderEx(void *hArcData, tHeaderDataEx *HeaderData) {
    DLOG("ReadHeaderEx");
    ArcHandle *h = (ArcHandle *)hArcData;
    if (!h || h->at_end) return E_END_ARCHIVE;

    int ret = (h->disk_type == DISK_T64) ? cbm_t64_next(h) : cbm_dir_next(h);
    if (ret != 0) return ret;

    memset(HeaderData, 0, sizeof(*HeaderData));
    strncpy(HeaderData->ArcName,  h->arc_name,     sizeof(HeaderData->ArcName) - 1);
    strncpy(HeaderData->FileName, h->cur_filename,  sizeof(HeaderData->FileName) - 1);
    HeaderData->PackSize    = h->cur_size_blocks;
    HeaderData->UnpSize     = h->cur_size_bytes;
    HeaderData->FileAttr    = ((h->cur_cbm_flags & 0x0F) == CBM_TYPE_DEL) ? FA_HIDDEN : FA_ARCH;
    if (h->cur_cbm_flags & CBM_LOCKED_BIT) HeaderData->FileAttr |= FA_READONLY;
    return 0;
}

int ReadHeaderExW(void *hArcData, tHeaderDataExW *HeaderData) {
    DLOG("ReadHeaderExW");
    ArcHandle *h = (ArcHandle *)hArcData;
    if (!h || h->at_end) return E_END_ARCHIVE;

    int ret = (h->disk_type == DISK_T64) ? cbm_t64_next(h) : cbm_dir_next(h);
    if (ret != 0) return ret;

    memset(HeaderData, 0, sizeof(*HeaderData));
    utf8_to_wcs(h->arc_name,     HeaderData->ArcName,  WCX_MAX_PATH);
    utf8_to_wcs(h->cur_filename, HeaderData->FileName, WCX_MAX_PATH);
    HeaderData->PackSize = h->cur_size_blocks;
    HeaderData->UnpSize  = h->cur_size_bytes;
    HeaderData->FileAttr = ((h->cur_cbm_flags & 0x0F) == CBM_TYPE_DEL) ? FA_HIDDEN : FA_ARCH;
    if (h->cur_cbm_flags & CBM_LOCKED_BIT) HeaderData->FileAttr |= FA_READONLY;
    return 0;
}

int ProcessFile(void *hArcData, int Operation,
                char *DestPath, char *DestName) {
    ArcHandle *h = (ArcHandle *)hArcData;
    if (!h) return E_BAD_ARCHIVE;

    if (Operation == PK_SKIP) return 0;

    if (Operation == PK_TEST) {
        /* Just verify the sector chain */
        if (h->disk_type == DISK_T64) return 0;
        uint32_t sz;
        return cbm_calc_file_size(h, h->cur_track, h->cur_sector, &sz, NULL);
    }

    if (Operation != PK_EXTRACT) return E_NOT_SUPPORTED;

    char out_path[WCX_MAX_PATH];
    if (DestName && *DestName) {
        snprintf(out_path, sizeof(out_path), "%s", DestName);
    } else {
        build_dest_path(DestPath, h->cur_filename, out_path, sizeof(out_path));
    }

    ensure_dir(out_path);

    if (h->disk_type == DISK_T64)
        return cbm_t64_extract(h, out_path);

    return cbm_extract_file(h, out_path);
}

int ProcessFileW(void *hArcData, int Operation,
                 uint16_t *DestPath, uint16_t *DestName) {
    char dpath[WCX_MAX_PATH] = "", dname[WCX_MAX_PATH] = "";
    if (DestPath) wcs_to_utf8(DestPath, dpath, sizeof(dpath));
    if (DestName) wcs_to_utf8(DestName, dname, sizeof(dname));
    return ProcessFile(hArcData, Operation,
                       dpath[0] ? dpath : NULL,
                       dname[0] ? dname : NULL);
}

int CloseArchive(void *hArcData) {
    DLOG("CloseArchive");
    ArcHandle *h = (ArcHandle *)hArcData;
    if (!h) return E_BAD_ARCHIVE;

    int ret = 0;
    if (h->dirty) ret = cbm_save_image(h);
    free_handle(h);
    return ret;
}

/* ============================================================
 * WCX API — Capabilities
 * ============================================================ */

int GetPackerCaps(void) {
    DLOG("GetPackerCaps");
    return PK_CAPS_NEW      |   /* can create new archives */
           PK_CAPS_MODIFY   |   /* can modify existing archives */
           PK_CAPS_MULTIPLE |   /* multiple files per archive */
           PK_CAPS_DELETE   |   /* can delete files */
           PK_CAPS_BY_CONTENT;  /* detect by content (T64 magic) */
}

int GetBackgroundFlags(void) {
    DLOG("GetBackgroundFlags");
    return 0;
}

/* ============================================================
 * WCX API — Create archive (PackFiles)
 * ============================================================ */

int PackFiles(char *PackedFile, char *SubPath, char *SrcPath,
              char *AddList, int Flags) {
    (void)SubPath; (void)Flags;  /* CBM disks are flat */
    DLOG("PackFiles: pf=%s srcp=%s", PackedFile ? PackedFile : "(null)", SrcPath ? SrcPath : "(null)");

    if (!PackedFile || !*PackedFile) return E_NOT_SUPPORTED;
    if (!AddList) { DLOG("PackFiles: null AddList"); return E_NO_FILES; }

    DLOG("PackFiles: AddList[0]=%s", AddList[0] ? AddList : "(empty)");

    /* Detect desired disk type from extension */
    int disk_type = cbm_detect_type(PackedFile, 0);
    DLOG("PackFiles: disk_type=%d", disk_type);
    if (disk_type == DISK_UNKNOWN || disk_type == DISK_T64)
        return E_NOT_SUPPORTED;

    ArcHandle *h = alloc_handle();
    if (!h) return E_NO_MEMORY;

    strncpy(h->arc_name, PackedFile, sizeof(h->arc_name) - 1);
    h->open_mode = PK_OM_LIST;

    int ret = 0;
    struct stat st;

    if (stat(PackedFile, &st) == 0) {
        DLOG("PackFiles: opening existing image");
        ret = open_image(h, PackedFile);
        if (ret != 0) goto done;
        h->disk_type = cbm_detect_type(PackedFile, h->image_size);
        if (h->disk_type == DISK_UNKNOWN) { ret = E_BAD_ARCHIVE; goto done; }
    } else {
        DLOG("PackFiles: creating new image");
        char tmp[WCX_MAX_PATH];
        strncpy(tmp, PackedFile, sizeof(tmp) - 1);
        char *base = basename(tmp);
        char *dot  = strrchr(base, '.');
        if (dot) *dot = '\0';
        size_t blen = strlen(base);
        if (blen > 16) base[16] = '\0';

        char disk_id[3] = "00";
        DLOG("PackFiles: cbm_create_image disk_name=%s", base);
        ret = cbm_create_image(h, disk_type, base, disk_id);
        DLOG("PackFiles: cbm_create_image ret=%d", ret);
        if (ret != 0) goto done;
    }

    /* Add each file in the AddList */
    for (const char *p = AddList; *p; p += strlen(p) + 1) {
        char src_full[WCX_MAX_PATH];
        if (SrcPath && *SrcPath)
            snprintf(src_full, sizeof(src_full), "%s/%s", SrcPath, p);
        else
            strncpy(src_full, p, sizeof(src_full) - 1);

        char cbm_name[32];
        int cbm_type;
        parse_src_filename(p, cbm_name, sizeof(cbm_name), &cbm_type);

        DLOG("PackFiles: writing %s -> cbm_name=%s type=%d", src_full, cbm_name, cbm_type);
        ret = cbm_write_file(h, src_full, cbm_name, cbm_type);
        DLOG("PackFiles: cbm_write_file ret=%d", ret);
        if (ret != 0) break;
    }

    if (ret == 0) ret = cbm_save_image(h);
    h->dirty = 0;  /* already saved */

done:
    free_handle(h);
    return ret;
}

int PackFilesW(uint16_t *PackedFile, uint16_t *SubPath, uint16_t *SrcPath,
               uint16_t *AddList, int Flags) {
    DLOG("PackFilesW: enter");
    char pf[WCX_MAX_PATH]="", sp[WCX_MAX_PATH]="", srcp[WCX_MAX_PATH]="";
    if (PackedFile) wcs_to_utf8(PackedFile, pf, sizeof(pf));
    if (SubPath)    wcs_to_utf8(SubPath,    sp, sizeof(sp));
    if (SrcPath)    wcs_to_utf8(SrcPath,  srcp, sizeof(srcp));

    /* Rebuild ANSI AddList from wide chars */
    if (!AddList) return PackFiles(pf, sp[0]?sp:NULL, srcp[0]?srcp:NULL, "\0", Flags);

    /* Count AddList total length (in uint16_t units) */
    const uint16_t *ap = AddList;
    while (*ap) { while (*ap) ap++; ap++; }
    ap++;  /* include final double-null */
    size_t addlen = (size_t)(ap - AddList);

    char *ansi_add = malloc(addlen * 3 + 1);  /* worst case UTF-8 expansion */
    if (!ansi_add) return E_NO_MEMORY;

    char *out = ansi_add;
    ap = AddList;
    while (*ap) {
        char item[WCX_MAX_PATH];
        wcs_to_utf8(ap, item, sizeof(item));
        size_t ilen = strlen(item) + 1;
        memcpy(out, item, ilen);
        out += ilen;
        while (*ap) ap++;
        ap++;
    }
    *out = '\0';

    int ret = PackFiles(pf, sp[0]?sp:NULL, srcp[0]?srcp:NULL, ansi_add, Flags);
    free(ansi_add);
    return ret;
}

/* ============================================================
 * WCX API — Delete files
 * ============================================================ */

int DeleteFiles(char *PackedFile, char *DeleteList) {
    if (!PackedFile || !*PackedFile) return E_NOT_SUPPORTED;
    if (!DeleteList) return 0;

    ArcHandle *h = alloc_handle();
    if (!h) return E_NO_MEMORY;

    strncpy(h->arc_name, PackedFile, sizeof(h->arc_name) - 1);
    h->open_mode = PK_OM_LIST;

    int ret = open_image(h, PackedFile);
    if (ret != 0) goto done;

    h->disk_type = cbm_detect_type(PackedFile, h->image_size);
    if (h->disk_type == DISK_UNKNOWN || h->disk_type == DISK_T64) {
        ret = E_NOT_SUPPORTED; goto done;
    }

    for (const char *p = DeleteList; *p; p += strlen(p) + 1) {
        /* Parse filename to get CBM name + type */
        char cbm_name[32];
        int cbm_type;
        parse_src_filename(p, cbm_name, sizeof(cbm_name), &cbm_type);

        uint8_t petscii[16];
        cbm_utf8_to_petscii(cbm_name, petscii, 16);

        /* Try to delete; ignore "not found" errors for batch delete */
        cbm_delete_file(h, petscii, cbm_type);
    }

    if (h->dirty) ret = cbm_save_image(h);
    h->dirty = 0;

done:
    free_handle(h);
    return ret;
}

int DeleteFilesW(uint16_t *PackedFile, uint16_t *DeleteList) {
    char pf[WCX_MAX_PATH] = "";
    if (PackedFile) wcs_to_utf8(PackedFile, pf, sizeof(pf));
    if (!DeleteList) return DeleteFiles(pf, "\0");

    size_t dlen = 0;
    const uint16_t *dp = DeleteList;
    while (*dp) { while (*dp) dp++; dp++; dlen += (dp - DeleteList); }
    dlen++;

    char *ansi_del = malloc(dlen * 3 + 1);
    if (!ansi_del) return E_NO_MEMORY;

    char *out = ansi_del;
    dp = DeleteList;
    while (*dp) {
        char item[WCX_MAX_PATH];
        wcs_to_utf8(dp, item, sizeof(item));
        size_t ilen = strlen(item) + 1;
        memcpy(out, item, ilen);
        out += ilen;
        while (*dp) dp++;
        dp++;
    }
    *out = '\0';

    int ret = DeleteFiles(pf, ansi_del);
    free(ansi_del);
    return ret;
}

/* ============================================================
 * WCX API — Content detection
 * ============================================================ */

int CanYouHandleThisFile(char *FileName) {
    DLOG("CanYouHandleThisFile: %s", FileName ? FileName : "(null)");
    if (!FileName) return 0;

    /* Check by extension */
    const char *dot = strrchr(FileName, '.');
    if (dot) {
        if (strcasecmp(dot, ".d64") == 0) return 1;
        if (strcasecmp(dot, ".d71") == 0) return 1;
        if (strcasecmp(dot, ".d80") == 0) return 1;
        if (strcasecmp(dot, ".d81") == 0) return 1;
        if (strcasecmp(dot, ".d82") == 0) return 1;
        if (strcasecmp(dot, ".t64") == 0) return 1;
    }

    /* Check by content: T64 magic "C64S" */
    FILE *f = fopen(FileName, "rb");
    if (f) {
        char magic[4];
        int n = (int)fread(magic, 1, 4, f);
        fclose(f);
        if (n >= 3 && memcmp(magic, "C64", 3) == 0) return 1;
    }

    /* Check by file size for disk images */
    struct stat st;
    if (stat(FileName, &st) == 0) {
        int t = cbm_detect_type(FileName, (size_t)st.st_size);
        if (t != DISK_UNKNOWN) return 1;
    }

    return 0;
}

int CanYouHandleThisFileW(uint16_t *FileName) {
    DLOG("CanYouHandleThisFileW");
    if (!FileName) return 0;
    char fn[WCX_MAX_PATH];
    wcs_to_utf8(FileName, fn, sizeof(fn));
    return CanYouHandleThisFile(fn);
}

/* ============================================================
 * WCX API — Unicode OpenArchive
 * ============================================================ */

void *OpenArchiveW(tOpenArchiveDataW *ArchiveData) {
    DLOG("OpenArchiveW: enter");
    if (!ArchiveData) { DLOG("OpenArchiveW: null ArchiveData"); return NULL; }
    if (!ArchiveData->ArcName) { DLOG("OpenArchiveW: null ArcName"); ArchiveData->OpenResult = E_EOPEN; return NULL; }
    char arc_name[WCX_MAX_PATH];
    wcs_to_utf8(ArchiveData->ArcName, arc_name, sizeof(arc_name));
    DLOG("OpenArchiveW: %s", arc_name);

    tOpenArchiveData ad;
    ad.ArcName    = arc_name;
    ad.OpenMode   = ArchiveData->OpenMode;
    ad.OpenResult = 0;
    ad.CmtBuf     = ArchiveData->CmtBuf;
    ad.CmtBufSize = ArchiveData->CmtBufSize;
    ad.CmtSize    = 0;
    ad.CmtState   = 0;

    void *h = OpenArchive(&ad);
    ArchiveData->OpenResult = ad.OpenResult;
    return h;
}

/* ============================================================
 * WCX API — Callbacks
 * ============================================================ */

void SetChangeVolProc(void *hArcData, tChangeVolProc pChangeVolProc) {
    DLOG("SetChangeVolProc: h=%p", hArcData);
    g_change_vol = pChangeVolProc;
    /* Do NOT dereference hArcData — DC may pass an invalid handle for pack ops */
}

void SetChangeVolProcW(void *hArcData, tChangeVolProcW pChangeVolProcW) {
    DLOG("SetChangeVolProcW: h=%p", hArcData);
    (void)hArcData; (void)pChangeVolProcW;
}

void SetProcessDataProc(void *hArcData, tProcessDataProc pProcessDataProc) {
    DLOG("SetProcessDataProc: h=%p", hArcData);
    g_process_data = pProcessDataProc;
    /* Do NOT dereference hArcData — DC may pass an invalid handle for pack ops */
}

void SetProcessDataProcW(void *hArcData, tProcessDataProcW pProcessDataProcW) {
    DLOG("SetProcessDataProcW: h=%p", hArcData);
    (void)hArcData; (void)pProcessDataProcW;
}

/* ============================================================
 * WCX API — Config dialog (no GUI on Linux)
 * ============================================================ */

void ConfigurePacker(void *hwndParent, void *hDllInstance) {
    (void)hwndParent; (void)hDllInstance;
    /* On Linux there's no Windows dialog.  Users edit cbmwcx.ini directly. */
}

/* ============================================================
 * WCX API — Default params (receive INI path from host)
 * ============================================================ */

void PackSetDefaultParams(PackDefaultParamStruct *dps) {
    if (!dps) return;
    DLOG("PackSetDefaultParams: ini=%s (ignored — using plugin-dir ini)", dps->DefaultIniName[0] ? dps->DefaultIniName : "(empty)");
    /* Do NOT reload from DC's shared wcx.ini — it has no plugin-specific section
     * and would reset all settings to defaults.  Our constructor already loaded
     * the correct cbmwcx.ini from the plugin directory. */
}

/* ============================================================
 * Double Commander extension entry point
 * ============================================================ */

void ExtensionInitialize(TExtensionStartupInfo *info) {
    DLOG("ExtensionInitialize: called");
    (void)info;  /* No DC-specific services needed */
}
