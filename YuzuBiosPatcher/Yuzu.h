/** @file
  YuzuBiosPatcher - common declarations.

  UEFI runtime memory patcher with Chinese/English bilingual UI.
  Upstream: https://github.com/barlowhaydnb/SmokelessRuntimeEFIPatcher
**/

#pragma once

#include <Uefi.h>
#include <PiDxe.h>
#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/RegularExpressionProtocol.h>
#include <Protocol/ShellParameters.h>

#define YUZU_VERSION L"0.2.0"

//
// Command opcodes (Yuzu_Config.cfg "Op <name>").
//
enum OPCODE
{
  NO_OP = 0,
  COMPATIBILITY = 1,
  LOADED = 2,
  NONAME_PE = 3,
  NONAME_TE = 4,
  LOAD_FS = 5,
  LOAD_FV = 6,
  LOAD_GUID_SAVE_PE = 7,
  LOAD_GUID_SAVE_FREEFORM = 8,
  PATCH = 9,
  FAST_PATCH = 10,
  UNINSTALL_PROTOCOL = 11,
  UPDATE_DB = 12,      // reserved, currently disabled
  GET_APTIO_DB = 13,   // reserved, currently disabled
  EXEC = 14,
  SKIP = 15
};

//
// Patch addressing modes.
//
enum PATTER_TYPE
{
  PATTERN_TYPE_NONE = 0,
  OFFSET = 1,
  PATTERN = 2,
  REL_NEG_OFFSET = 3,
  REL_POS_OFFSET = 4
};

//
// Bitmask of opcodes whose first argument line is a name / GUID string.
//
#define NAME_ARG_OP_MASK  0x98FEu

//
// One parsed command.
//
struct OP_DATA
{
  UINT32          ID;                     // enum OPCODE
  CHAR8          *Name;                  // file name / GUID string
  CHAR8          *SectionGuid;           // freeform section subtype GUID (text)
  BOOLEAN         Name_Dyn_Alloc;
  BOOLEAN         SectionGuid_Dyn_Alloc;
  UINT64          PatterType;            // enum PATTER_TYPE
  BOOLEAN         PatterType_Dyn_Alloc;
  INT64           ARG3;                  // offset value / found position
  BOOLEAN         ARG3_Dyn_Alloc;
  UINT64          ARG4;                  // replacement length in bytes
  BOOLEAN         ARG4_Dyn_Alloc;
  UINT8          *ARG5;                  // replacement bytes
  BOOLEAN         ARG5_Dyn_Alloc;
  UINT64          ARG6;                  // search pattern length in bytes
  BOOLEAN         ARG6_Dyn_Alloc;
  UINT8          *ARG7;                  // search pattern bytes (exact match)
  BOOLEAN         ARG7_Dyn_Alloc;
  CHAR8          *PatternText;           // raw pattern text ('.' wildcards kept)
  BOOLEAN         PatternText_Dyn_Alloc;
  struct OP_DATA *next;
  struct OP_DATA *prev;
};

//
// Globals.
//
extern EFI_BOOT_SERVICES               *_gBS;
extern EFI_FILE                        *LogFile;
extern EFI_REGULAR_EXPRESSION_PROTOCOL *gRegexProtocol;

//
// Messaging: YuzuMsg prints to the screen,
// YuzuLog appends an ASCII line to Yuzu.log.
//
VOID
YuzuMsg (
  CONST CHAR16  *Format,
  ...
  );

VOID
YuzuLog (
  CONST CHAR8  *Fmt,
  ...
  );

VOID
LogToFile (
  EFI_FILE  *File,
  CHAR8     *String
  );

//
// Boot breadcrumb - appends one line to \boot.log (black-screen diagnosis).
//
EFI_STATUS
EFIAPI
Crumb (
  IN CONST CHAR8  *Msg
  );

VOID
PrintDump (
  UINT16  Size,
  UINT8   *DUMP
  );
