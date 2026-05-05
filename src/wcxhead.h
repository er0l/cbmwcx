#pragma once
#include <stdint.h>

/* Error codes */
#define E_END_ARCHIVE       10
#define E_NO_MEMORY         11
#define E_BAD_DATA          12
#define E_BAD_ARCHIVE       13
#define E_UNKNOWN_FORMAT    14
#define E_EOPEN             15
#define E_ECREATE           16
#define E_ECLOSE            17
#define E_EREAD             18
#define E_EWRITE            19
#define E_SMALL_BUF         20
#define E_EABORTED          21
#define E_NO_FILES          22
#define E_TOO_MANY_FILES    23
#define E_NOT_SUPPORTED     24

/* OpenArchive open modes */
#define PK_OM_LIST          0
#define PK_OM_EXTRACT       1

/* ProcessFile operations */
#define PK_SKIP             0
#define PK_TEST             1
#define PK_EXTRACT          2

/* GetPackerCaps capability flags */
#define PK_CAPS_NEW         1
#define PK_CAPS_MODIFY      2
#define PK_CAPS_MULTIPLE    4
#define PK_CAPS_DELETE      8
#define PK_CAPS_OPTIONS     16
#define PK_CAPS_MEMPACK     32
#define PK_CAPS_BY_CONTENT  64
#define PK_CAPS_SEARCHTEXT  128
#define PK_CAPS_HIDE        256

/* PackFiles flags */
#define PK_PACK_MOVE_FILES  1
#define PK_PACK_SAVE_PATHS  2

/* File attributes */
#define FA_READONLY         0x01
#define FA_HIDDEN           0x02
#define FA_SYSTEM           0x04
#define FA_LABEL            0x08
#define FA_DIREC            0x10
#define FA_ARCH             0x20

#define WCX_MAX_PATH        1024

/* ANSI open archive data */
typedef struct {
    char *ArcName;
    int   OpenMode;
    int   OpenResult;
    char *CmtBuf;
    int   CmtBufSize;
    int   CmtSize;
    int   CmtState;
} tOpenArchiveData;

/* Unicode open archive data (UTF-16LE) */
typedef struct {
    uint16_t *ArcName;
    int       OpenMode;
    int       OpenResult;
    char     *CmtBuf;
    int       CmtBufSize;
    int       CmtSize;
    int       CmtState;
} tOpenArchiveDataW;

/* Legacy header (limited path length) */
typedef struct {
    char ArcName[WCX_MAX_PATH];
    char FileName[WCX_MAX_PATH];
    int  Flags;
    int  PackSize;
    int  UnpSize;
    int  HostOS;
    long FileCRC;
    int  FileTime;
    int  Method;
    int  FileAttr;
    char *CmtBuf;
    int  CmtBufSize;
    int  CmtSize;
    int  CmtState;
} tHeaderData;

/* Extended header with 64-bit sizes */
typedef struct {
    char         ArcName[WCX_MAX_PATH];
    char         FileName[WCX_MAX_PATH];
    int          Flags;
    unsigned int PackSize;
    unsigned int PackSizeHigh;
    unsigned int UnpSize;
    unsigned int UnpSizeHigh;
    int          HostOS;
    unsigned int FileCRC;
    int          FileTime;
    int          Method;
    int          FileAttr;
    char        *CmtBuf;
    int          CmtBufSize;
    int          CmtSize;
    int          CmtState;
} tHeaderDataEx;

/* Unicode extended header (UTF-16LE filenames) */
typedef struct {
    uint16_t     ArcName[WCX_MAX_PATH];
    uint16_t     FileName[WCX_MAX_PATH];
    int          Flags;
    unsigned int PackSize;
    unsigned int PackSizeHigh;
    unsigned int UnpSize;
    unsigned int UnpSizeHigh;
    int          HostOS;
    unsigned int FileCRC;
    int          FileTime;
    int          Method;
    int          FileAttr;
    char        *CmtBuf;
    int          CmtBufSize;
    int          CmtSize;
    int          CmtState;
} tHeaderDataExW;

typedef int (*tChangeVolProc)(char *ArcName, int Mode);
typedef int (*tChangeVolProcW)(uint16_t *ArcName, int Mode);
typedef int (*tProcessDataProc)(char *FileName, int Size);
typedef int (*tProcessDataProcW)(uint16_t *FileName, int Size);

typedef struct {
    int      size;
    uint32_t PluginInterfaceVersionLow;
    uint32_t PluginInterfaceVersionHi;
    char     DefaultIniName[WCX_MAX_PATH];
} PackDefaultParamStruct;

/* Double Commander extension startup */
typedef struct {
    int     size;
    void   *PluginNumber;
    void   *PackDefaultParamStruct;
    void  (*MessageBox)(void *PluginNumber, int MsgType, const char *Title, const char *Text);
    void  *reserved[32];
} TExtensionStartupInfo;
