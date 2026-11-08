/** @file
  YuzuBiosPatcher - entry point, config parser, dispatcher and
  patch engines.

  UEFI runtime memory patcher.  Reads Yuzu_Config.cfg from the boot volume
  and applies the described patches to UEFI modules in memory.

  Config grammar (Yuzu_Config.cfg):

    # lines containing '#' are comments
    Op <Name>            command introduction
    <argument lines>     one argument per line
    Op End               optional batch separator

  Arguments:

    LoadFromFS / LoadFromFV / Loaded / NonamePE / NonameTE /
    LoadGUIDandSavePE / UninstallProtocol / Exec / Skip / Compatibility
        <name or GUID string>

    LoadGUIDandSaveFreeform
        <file GUID>
        <freeform section subtype GUID>   (optional, RAW if omitted)

    Patch / FastPatch
        Pattern | Offset | RelNegOffset | RelPosOffset
        <hex find pattern / offset>
        <hex replacement bytes>

  Pattern text may contain '.' wildcards: one '.' matches one hex digit,
  '..' matches one arbitrary byte.  Wildcard matching runs through the
  EFI_REGULAR_EXPRESSION_PROTOCOL provided by Oniguruma.efi; FastPatch
  performs a plain exact-byte scan instead.
**/

#include "Yuzu.h"
#include "Utility.h"
#include "Opcode.h"

EFI_BOOT_SERVICES               *_gBS = NULL;
EFI_FILE                        *LogFile = NULL;
EFI_REGULAR_EXPRESSION_PROTOCOL *gRegexProtocol = NULL;

#define OP_DATA_SIZE        0xA0
#define MAX_EXTRA_MATCHES   256

//
// ---------------------------------------------------------------------------
// Regex engine bootstrap (Oniguruma.efi)
// ---------------------------------------------------------------------------
//
// "Oniguruma.efi" is the EDK2 RegularExpressionDxe module; loading it
// installs EFI_REGULAR_EXPRESSION_PROTOCOL which is used for wildcard
// patterns.  It is loaded from the boot filesystem next to this program.
//
static EFI_STATUS
InitRegexEngine (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS           Status;
  UINTN                NumHandles;
  EFI_HANDLE          *SFS_Handles;
  UINTN                Index;
  EFI_DEVICE_PATH_PROTOCOL *FilePath;
  EFI_HANDLE           OniHandle;
  UINTN                HandleSize;
  EFI_HANDLE          *Handles;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &NumHandles,
                  &SFS_Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < NumHandles; Index++) {
    FilePath = FileDevicePath (SFS_Handles[Index], L"Oniguruma.efi");
    if (FilePath == NULL) {
      continue;
    }
    Status = gBS->LoadImage (FALSE, ImageHandle, FilePath, NULL, 0, &OniHandle);
    if (EFI_ERROR (Status)) {
      continue;
    }
    gBS->StartImage (OniHandle, NULL, NULL);
    break;
  }

  HandleSize = 0;
  Status = gBS->LocateHandle (ByProtocol, &gEfiRegularExpressionProtocolGuid, NULL, &HandleSize, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Handles = AllocateZeroPool (HandleSize);
    Status = gBS->LocateHandle (ByProtocol, &gEfiRegularExpressionProtocolGuid, NULL, &HandleSize, Handles);
    if (!EFI_ERROR (Status) && HandleSize >= sizeof (EFI_HANDLE)) {
      gBS->HandleProtocol (Handles[0], &gEfiRegularExpressionProtocolGuid, (VOID **)&gRegexProtocol);
    }
    FreePool (Handles);
    Status = EFI_SUCCESS;
  }
  return Status;
}

//
// ---------------------------------------------------------------------------
// Config parser
// ---------------------------------------------------------------------------
//
static struct OP_DATA *
NewOp (
  UINT32  Id
  )
{
  struct OP_DATA  *Node = AllocateZeroPool (OP_DATA_SIZE);

  Node->ID = Id;
  return Node;
}

static VOID
AddOpTail (
  struct OP_DATA  *Start,
  struct OP_DATA  *Node
  )
{
  struct OP_DATA  *Next = Start;

  while (Next->next != NULL) {
    Next = Next->next;
  }
  Next->next = Node;
  Node->prev = Next;
}

static struct OP_DATA *
ParseConfig (
  CHAR8   *ConfigData,
  UINTN    ConfigDataSize
  )
{
  struct OP_DATA  *Start = AllocateZeroPool (OP_DATA_SIZE);
  struct OP_DATA  *Prev;
  UINTN            CurrPos = 0;
  BOOLEAN          NullByteSkipped = FALSE;

  Start->ID = NO_OP;

  //
  // Turn '\n' '\r' '\t' into NUL so the buffer becomes NUL-separated tokens.
  //
  for (UINTN i = 0; i < ConfigDataSize; i++) {
    if (ConfigData[i] == '\n' || ConfigData[i] == '\r' || ConfigData[i] == '\t') {
      ConfigData[i] = '\0';
    }
  }

  Prev = Start;
  while (CurrPos < ConfigDataSize) {
    CHAR8  *Cur;

    if (CurrPos != 0 && !NullByteSkipped) {
      CurrPos += AsciiStrLen (&ConfigData[CurrPos]);
    }
    if (ConfigData[CurrPos] == '\0') {
      CurrPos += 1;
      NullByteSkipped = TRUE;
      continue;
    }
    NullByteSkipped = FALSE;
    Cur = &ConfigData[CurrPos];

    //
    // '#' anywhere in the token marks a commented-out line.
    //
    if (AsciiStrStr (Cur, "#") != NULL) {
      CurrPos += AsciiStrLen (Cur);
      continue;
    }

    if (AsciiStrStr (Cur, "End") != NULL) {
      YuzuLog ("End OP Detected\n\r");
      continue;
    }

    if (AsciiStrStr (Cur, "Op") != NULL) {
      //
      // Compatible with 0.1.x configs: three characters ("Op ") are
      // consumed after the "Op" match position.
      //
      CurrPos += 3;
      Cur = &ConfigData[CurrPos];

      //
      // Test order matters for substring matching: "FastPatch" contains
      // "Patch", "LoadFromFV" etc. must precede "Loaded".
      //
      if (AsciiStrStr (Cur, "LoadFromFS") != NULL) {
        Prev = NewOp (LOAD_FS); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "LoadFromFV") != NULL) {
        Prev = NewOp (LOAD_FV); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "LoadGUIDandSavePE") != NULL) {
        Prev = NewOp (LOAD_GUID_SAVE_PE); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "LoadGUIDandSaveFreeform") != NULL) {
        Prev = NewOp (LOAD_GUID_SAVE_FREEFORM); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "Compatibility") != NULL) {
        Prev = NewOp (COMPATIBILITY); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "Loaded") != NULL) {
        Prev = NewOp (LOADED); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "NonamePE") != NULL) {
        Prev = NewOp (NONAME_PE); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "NonameTE") != NULL) {
        Prev = NewOp (NONAME_TE); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "FastPatch") != NULL) {
        Prev = NewOp (FAST_PATCH); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "Patch") != NULL) {
        Prev = NewOp (PATCH); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "Exec") != NULL) {
        Prev = NewOp (EXEC); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "Skip") != NULL) {
        //
        // "Op Skip" is only meaningful right after a targeting or patching
        // command (opcodes 7..13).
        //
        if (Prev->ID >= LOAD_GUID_SAVE_PE && Prev->ID <= GET_APTIO_DB) {
          Prev = NewOp (SKIP); AddOpTail (Start, Prev);
        } else {
          YuzuMsg (L"But previous one doesn't support \"Op Skip\", so nothing's going to happen\n");
        }
        continue;
      }
      if (AsciiStrStr (Cur, "UninstallProtocol") != NULL) {
        Prev = NewOp (UNINSTALL_PROTOCOL); AddOpTail (Start, Prev); continue;
      }
      if (AsciiStrStr (Cur, "UpdateDB") != NULL || AsciiStrStr (Cur, "GetAptioDB") != NULL) {
        YuzuMsg (L"This operation is disabled, you can't use it\n");
        continue;
      }
      YuzuMsg (L"Command %a Invalid\n", Cur);
      return NULL;
    }

    //
    // Argument lines.
    //
    if ((Prev->ID != NO_OP) && (NAME_ARG_OP_MASK & (1u << Prev->ID)) != 0 && Prev->Name == NULL) {
      UINTN  Len = AsciiStrLen (Cur) + 1;

      YuzuLog ("Found File %a \n\r", Cur);
      Prev->Name = AllocateZeroPool (Len);
      CopyMem (Prev->Name, Cur, Len);
      Prev->Name_Dyn_Alloc = TRUE;
      continue;
    }

    if (Prev->ID == LOAD_GUID_SAVE_FREEFORM && Prev->Name == NULL) {
      UINTN  Len = AsciiStrLen (Cur) + 1;

      Prev->Name = AllocateZeroPool (Len);
      CopyMem (Prev->Name, Cur, Len);
      Prev->Name_Dyn_Alloc = TRUE;
      YuzuLog ("FileGuid input: %a\n\r", Cur);

      CurrPos += Len;
      //
      // Peek at the next token: "Op"/"Pattern" means no section GUID.
      //
      Cur = &ConfigData[CurrPos];
      if (AsciiStrStr (Cur, "Op") != NULL || AsciiStrStr (Cur, "Pattern") != NULL) {
        YuzuMsg (L"Freeform Section Subtype GUID not specified, your input: %a, looking for RAW instead\n", Cur);
        Prev->SectionGuid = NULL;
      } else {
        UINTN  GLen = AsciiStrLen (Cur) + 1;

        Prev->SectionGuid = AllocateZeroPool (GLen);
        CopyMem (Prev->SectionGuid, Cur, GLen);
        Prev->SectionGuid_Dyn_Alloc = TRUE;
        YuzuLog ("SectionGuid input: %a\n\r", Cur);
      }
      continue;
    }

    if ((Prev->ID == PATCH || Prev->ID == FAST_PATCH) && Prev->PatterType == PATTERN_TYPE_NONE) {
      //
      // Later matches override earlier ones: "RelNegOffset" first hits
      // "Offset" and is then corrected to REL_NEG_OFFSET.
      //
      if (AsciiStrStr (Cur, "Offset") != NULL) {
        Prev->PatterType = OFFSET;
      }
      if (AsciiStrStr (Cur, "Pattern") != NULL) {
        Prev->PatterType = PATTERN;
      }
      if (AsciiStrStr (Cur, "RelNegOffset") != NULL) {
        Prev->PatterType = REL_NEG_OFFSET;
      }
      if (AsciiStrStr (Cur, "RelPosOffset") != NULL) {
        Prev->PatterType = REL_POS_OFFSET;
      }
      continue;
    }

    if ((Prev->ID == PATCH || Prev->ID == FAST_PATCH) && Prev->PatterType != PATTERN_TYPE_NONE && Prev->ARG3 == 0) {
      if (Prev->PatterType == OFFSET || Prev->PatterType == REL_NEG_OFFSET || Prev->PatterType == REL_POS_OFFSET) {
        Prev->ARG3 = AsciiStrHexToUint64 (Cur);
      }
      if (Prev->PatterType == PATTERN) {
        UINTN  TextLen = AsciiStrLen (Cur);

        Prev->ARG3 = 0xFFFFFFFF;                 // "not found" sentinel
        Prev->ARG6 = TextLen / 2;
        Prev->PatternText = AllocateZeroPool (TextLen + 1);
        CopyMem (Prev->PatternText, Cur, TextLen + 1);
        Prev->PatternText_Dyn_Alloc = TRUE;
        Prev->ARG7 = AllocateZeroPool (Prev->ARG6);
        //
        // Best-effort hex decode; wildcard patterns leave this buffer
        // zeroed - the regex engine works on PatternText instead.
        //
        AsciiStrHexToBytes (Cur, Prev->ARG6 * 2, Prev->ARG7, Prev->ARG6);
      }
      continue;
    }

    if ((Prev->ID == PATCH || Prev->ID == FAST_PATCH) && Prev->PatterType != PATTERN_TYPE_NONE && Prev->ARG3 != 0) {
      Prev->ARG4 = AsciiStrLen (Cur) / 2;
      Prev->ARG5 = AllocateZeroPool (Prev->ARG4);
      Prev->ARG5_Dyn_Alloc = TRUE;
      AsciiStrHexToBytes (Cur, Prev->ARG4 * 2, Prev->ARG5, Prev->ARG4);
      YuzuLog ("Patch Byte\n\r");
      PrintDump ((UINT16)Prev->ARG4, Prev->ARG5);
      continue;
    }
  }
  return Start;
}

//
// ---------------------------------------------------------------------------
// FastPatch engine
// ---------------------------------------------------------------------------
//
// Exact-byte search (CompareMem scan) over the loaded image, then write.
// Relative modes resolve against the shared BaseOffset.
//
static VOID
RunFastPatch (
  struct OP_DATA              *Op,
  EFI_LOADED_IMAGE_PROTOCOL   *ImageInfo,
  INT64                       *BaseOffset
  )
{
  INT64   Found;
  UINTN   i;

  if (Op->PatterType != PATTERN) {
    Found = Op->ARG3;
    goto ResolveRelative;
  }

  YuzuLog ("Finding Pattern\n\r");
  Found = -1;
  for (i = 0; i + Op->ARG6 <= ImageInfo->ImageSize; i++) {
    if (CompareMem ((UINT8 *)ImageInfo->ImageBase + i, Op->ARG7, Op->ARG6) == 0) {
      Found = (INT64)i;
      break;
    }
  }
  if (Found == -1) {
    YuzuMsg (L"No Pattern Found\n");
    Op->ARG3 = 0;
    return;
  }
  Op->ARG3 = Found;

ResolveRelative:
  if (Op->PatterType == REL_POS_OFFSET && Op->ARG3 != 0) {
    Op->ARG3 = *BaseOffset + Op->ARG3;
  }
  if (Op->PatterType == REL_NEG_OFFSET && Op->ARG3 != 0) {
    Op->ARG3 = *BaseOffset - Op->ARG3;
  }
  *BaseOffset = Op->ARG3;
  YuzuMsg (L"Offset in section: 0x%x\n", Op->ARG3);

  if (Op->ARG3 != 0) {
    YuzuMsg (L"Patched\n");
    CopyMem ((UINT8 *)ImageInfo->ImageBase + Op->ARG3, Op->ARG5, Op->ARG4);
  } else {
    YuzuMsg (L"Not patched\n");
  }
}

//
// ---------------------------------------------------------------------------
// Patch engine (wildcard / regex variant)
// ---------------------------------------------------------------------------
//
// Every byte offset of the loaded image is tested with MatchPatternAt().
// The first match is patched at once; further matches are collected into a
// table and patched afterwards.
//
static VOID
RunPatch (
  struct OP_DATA              *Op,
  EFI_LOADED_IMAGE_PROTOCOL   *ImageInfo,
  INT64                       *BaseOffset
  )
{
  UINTN    Pos;
  BOOLEAN  Found;
  UINTN    ExtraMatches[MAX_EXTRA_MATCHES];
  UINTN    ExtraCount = 0;
  UINTN    i;

  if (Op->PatterType != PATTERN) {
    goto ResolveRelative;
  }

  if (gRegexProtocol == NULL) {
    //
    // Wildcard patterns need the regex engine from Oniguruma.efi.
    //
    YuzuMsg (L"Op Patch needs the regex engine (Oniguruma.efi) - not found, skipped\n");
    Op->ARG3 = 0;
    return;
  }

  YuzuLog ("Finding Pattern\n\r");
  YuzuMsg (L"%a\n", Op->PatternText);
  YuzuMsg (L"Please, standby...\n");
  YuzuMsg (L"^ Search string ^\n");

  Op->ARG3 = -1;
  for (Pos = 0; Pos + Op->ARG6 <= ImageInfo->ImageSize; Pos++) {
    Found = FALSE;
    MatchPatternAt ((UINT8 *)ImageInfo->ImageBase + Pos, Op->PatternText,
                    (UINT16)Op->ARG6, gRegexProtocol, &Found);
    if (Found) {
      if (Op->ARG3 == (INT64)-1) {
        Op->ARG3 = (INT64)Pos;
        YuzuMsg (L"Found\n");
        PrintDump ((UINT16)Op->ARG6, (UINT8 *)ImageInfo->ImageBase + Pos);
      } else {
        YuzuMsg (L"Found another match\n");
        if (ExtraCount < MAX_EXTRA_MATCHES) {
          ExtraMatches[ExtraCount] = Pos;
          ExtraCount++;
        }
        PrintDump ((UINT16)Op->ARG6, (UINT8 *)ImageInfo->ImageBase + Pos);
      }
    }
  }

  if (Op->ARG3 == (INT64)-1) {
    YuzuMsg (L"Pattern not found\n");
    YuzuMsg (L"Not patched\n");
    return;
  }

ResolveRelative:
  if (Op->PatterType == REL_POS_OFFSET && Op->ARG3 != 0) {
    Op->ARG3 = *BaseOffset + Op->ARG3;
  }
  if (Op->PatterType == REL_NEG_OFFSET && Op->ARG3 != 0) {
    Op->ARG3 = *BaseOffset - Op->ARG3;
  }
  *BaseOffset = Op->ARG3;
  YuzuMsg (L"Offset in section: 0x%x\n", Op->ARG3);

  if (Op->ARG3 != 0) {
    YuzuMsg (L"Patched\n");
    CopyMem ((UINT8 *)ImageInfo->ImageBase + Op->ARG3, Op->ARG5, Op->ARG4);
  } else {
    YuzuMsg (L"Not patched\n");
  }

  if (ExtraCount != 0) {
    YuzuMsg (L"Additional matches for pattern: %d\n", ExtraCount);
    for (i = 0; i < ExtraCount; i++) {
      YuzuMsg (L"%d offset in RAM: 0x%x\n",
               i + 1, (UINTN)ImageInfo->ImageBase + ExtraMatches[i]);
      CopyMem ((UINT8 *)ImageInfo->ImageBase + ExtraMatches[i], Op->ARG5, Op->ARG4);
    }
  }
}

//
// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------
//
static VOID
Dispatch (
  struct OP_DATA   *Start,
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE *SystemTable
  )
{
  struct OP_DATA            *Op;
  EFI_LOADED_IMAGE_PROTOCOL *ImageInfo = NULL;
  EFI_HANDLE                 AppImageHandle = NULL;
  EFI_GUID                   CompatGuid;
  INT64                      BaseOffset = 0;
  EFI_STATUS                 Status = EFI_INVALID_PARAMETER;
  BOOLEAN                    PrevOk = FALSE;

  //
  // Default FV-style protocol for section access ("Op Compatibility"
  // overrides it).
  //
  CopyGuid (&CompatGuid, &gEfiFirmwareVolume2ProtocolGuid);

  for (Op = Start; Op != NULL; Op = Op->next) {
    PrevOk = FALSE;
    switch (Op->ID) {
    case NO_OP:
      YuzuMsg (L"NOP\n");
      break;

    case COMPATIBILITY:
      {
        EFI_GUID  Parsed;

        if (AsciiStrCmp (Op->Name, "DB9A1E3D-45CB-4ABB-853B-E5387FDB2E2D") != 0 &&
            AsciiStrCmp (Op->Name, "389F751F-1838-4388-8390-CD8154BD27F8") != 0) {
          YuzuMsg (L"Recommended protocols are:\n"
                   L"EFI_FIRMWARE_VOLUME_PROTOCOL_GUID(good for HP Insyde Rev.3)\n"
                   L"389F751F-1838-4388-8390-CD8154BD27F8\n"
                   L"EFI_LEGACY_BIOS_PROTOCOL_GUID(good for Aptio 4, Insyde Rev.3)\n"
                   L"DB9A1E3D-45CB-4ABB-853B-E5387FDB2E2D\n");
        }
        Status = YuzuStrToGuid (Op->Name, &Parsed);
        if (EFI_ERROR (Status)) {
          YuzuMsg (L"Failed to convert \"%a\" to GUID\n", Op->Name);
          break;
        }
        CopyGuid (&CompatGuid, &Parsed);
        YuzuMsg (L"Now filtering Handles by protocol: %g\n", &CompatGuid);
      }
      break;

    case LOADED:
      YuzuLog ("Executing Loaded\n\r");
      Status = FindLoadedImageFromName (ImageHandle, Op->Name, &ImageInfo, &CompatGuid);
      YuzuMsg (L"Loaded Image %r -> %x\n",
               Status, ImageInfo == NULL ? 0 : ImageInfo->ImageBase);
      PrevOk = !EFI_ERROR (Status);
      break;

    case NONAME_PE:
    case NONAME_TE:
      YuzuLog ("Executing NonamePE/NonameTE\n\r");
      Status = FindImageInRamByGuid (ImageHandle, Op->Name, &ImageInfo,
                                     Op->ID == NONAME_PE ? EFI_SECTION_PE32 : EFI_SECTION_TE,
                                     &CompatGuid);
      YuzuMsg (L"Search result: %r\n", Status);
      PrevOk = !EFI_ERROR (Status);
      break;

    case LOAD_FS:
      YuzuLog ("Executing LoadFromFS\n\r");
      Status = LoadFS (ImageHandle, Op->Name, &ImageInfo, &AppImageHandle);
      YuzuMsg (L"Loaded Image %r -> %x\n",
               Status, ImageInfo == NULL ? 0 : ImageInfo->ImageBase);
      PrevOk = !EFI_ERROR (Status);
      break;

    case LOAD_FV:
      YuzuLog ("Executing LoadFromFV\n\r");
      Status = LoadFV (ImageHandle, Op->Name, &ImageInfo, &AppImageHandle, &CompatGuid);
      YuzuMsg (L"Search result: %r\n", Status);
      if (!EFI_ERROR (Status)) {
        YuzuMsg (L"Loaded Image %r -> %x\n",
                 Status, ImageInfo->ImageBase);
      }
      PrevOk = !EFI_ERROR (Status);
      break;

    case LOAD_GUID_SAVE_PE:
      YuzuLog ("Executing LoadGUIDandSavePE\n\r");
      Status = LoadGuidSavePe (ImageHandle, Op->Name, &ImageInfo, &AppImageHandle,
                               SystemTable, &CompatGuid);
      PrevOk = !EFI_ERROR (Status);
      break;

    case LOAD_GUID_SAVE_FREEFORM:
      YuzuLog ("Executing LoadGUIDandSaveFreeform\n\r");
      if (ImageInfo == NULL) {
        break;
      }
      Status = LoadGuidSaveFreeform (ImageHandle, Op->Name,
                                     (UINT8 **)&ImageInfo->ImageBase, &ImageInfo->ImageSize,
                                     Op->SectionGuid, SystemTable, &CompatGuid);
      PrevOk = !EFI_ERROR (Status);
      break;

    case PATCH:
      //
      // Patch engines only run when the preceding operation succeeded.
      //
      if (Status != EFI_SUCCESS) {
        break;
      }
      YuzuLog ("Executing Patch\n\r");
      RunPatch (Op, ImageInfo, &BaseOffset);
      PrevOk = TRUE;
      break;

    case FAST_PATCH:
      if (Status != EFI_SUCCESS) {
        break;
      }
      YuzuLog ("Executing Fast Patch\n\r");
      RunFastPatch (Op, ImageInfo, &BaseOffset);
      PrevOk = TRUE;
      break;

    case UNINSTALL_PROTOCOL:
      YuzuLog ("Executing UninstallProtocol\n\r");
      Status = UninstallProtocol (Op->Name);
      PrevOk = !EFI_ERROR (Status);
      break;

    case EXEC:
      YuzuMsg (L"EXEC\n");
      Status = Exec (&AppImageHandle);
      PrevOk = !EFI_ERROR (Status);
      break;

    case SKIP:
      {
        UINTN  Count = AsciiStrDecimalToUint64 (Op->Name);
        UINTN  i;

        if (PrevOk) {
          YuzuMsg (L"Skipping %d commands\n", Count);
          for (i = 1; i < Count && Op != NULL; i++) {
            Op = Op->next;
          }
        } else {
          YuzuMsg (L"Last command returned: %r, skip not happened\n", Status);
        }
      }
      break;

    default:
      break;
    }
  }
}

//
// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------
//
static VOID
FreeOpChain (
  struct OP_DATA  *Start
  )
{
  struct OP_DATA  *Node;
  struct OP_DATA  *Next;

  for (Node = Start; Node != NULL; Node = Next) {
    if (Node->Name_Dyn_Alloc)        FreePool (Node->Name);
    if (Node->SectionGuid_Dyn_Alloc) FreePool (Node->SectionGuid);
    if (Node->PatterType_Dyn_Alloc)  FreePool ((VOID *)Node->PatterType);
    if (Node->ARG3_Dyn_Alloc)        FreePool ((VOID *)Node->ARG3);
    if (Node->ARG4_Dyn_Alloc)        FreePool ((VOID *)Node->ARG4);
    if (Node->ARG5_Dyn_Alloc)        FreePool (Node->ARG5);
    if (Node->ARG6_Dyn_Alloc)        FreePool ((VOID *)Node->ARG6);
    if (Node->ARG7_Dyn_Alloc)        FreePool (Node->ARG7);
    if (Node->PatternText_Dyn_Alloc) FreePool (Node->PatternText);
    Next = Node->next;
    FreePool (Node);
  }
}

//
// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
//
EFI_STATUS
EFIAPI
YuzuEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL       *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE                        *Root;
  EFI_FILE                        *ConfigFile;
  CHAR16                           FileName[255];
  EFI_GUID                         FileInfoGuid = EFI_FILE_INFO_ID;
  EFI_FILE_INFO                   *FileInfo;
  UINTN                            FileInfoSize;
  UINTN                            ConfigDataSize;
  CHAR8                           *ConfigData;
  struct OP_DATA                  *Chain;

  _gBS = SystemTable->BootServices;

  YuzuMsg (L"Welcome to YuzuBiosPatcher %s\n", YUZU_VERSION);
  YuzuMsg (L"Author: PhorusYu\n");
  YuzuMsg (L"  Bilibili: https://space.bilibili.com/1869564846\n");
  YuzuMsg (L"  GitHub:   https://github.com/PhorusYu\n");

  gBS->SetWatchdogTimer (0, 0, 0, 0);

  //
  // Wildcard patterns need the regex protocol provided by Oniguruma.efi.
  //
  InitRegexEngine (ImageHandle);
  if (gRegexProtocol == NULL) {
    YuzuMsg (L"Failed on Locating RegularExpressionProtocol: %r\n", EFI_NOT_FOUND);
  }

  //
  // Log and config live on the volume we were started from.
  //
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  Status = gBS->HandleProtocol (LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
  FileSystem->OpenVolume (FileSystem, &Root);

  Status = Root->Open (Root, &LogFile, L"Yuzu.log",
                       EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ | EFI_FILE_MODE_CREATE, 0);
  if (Status == EFI_SUCCESS) {
    //
    // Start each run with a fresh log.  Delete also closes the handle -
    // clear the pointer so message mirroring cannot touch a dead handle.
    //
    LogFile->Delete (LogFile);
    LogFile = NULL;
    YuzuMsg (L"The old log was deleted\n");
    Status = Root->Open (Root, &LogFile, L"Yuzu.log",
                         EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ | EFI_FILE_MODE_CREATE, 0);
  }
  if (Status != EFI_SUCCESS) {
    YuzuMsg (L"Failed on Opening Log File: %r\n", Status);
    return Status;
  }
  YuzuLog ("YuzuBiosPatcher Launched\n\r");

  //
  // Read Yuzu_Config.cfg.
  //
  UnicodeSPrint (FileName, sizeof (FileName), L"Yuzu_Config.cfg");
  Status = Root->Open (Root, &ConfigFile, FileName, EFI_FILE_MODE_READ, 0);
  if (Status != EFI_SUCCESS) {
    YuzuMsg (L"Failed on Opening cfg: %r\n", Status);
    return Status;
  }

  FileInfo = NULL;
  FileInfoSize = 0;
  Status = ConfigFile->GetInfo (ConfigFile, &FileInfoGuid, &FileInfoSize, FileInfo);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    FileInfo = AllocatePool (FileInfoSize);
    Status = ConfigFile->GetInfo (ConfigFile, &FileInfoGuid, &FileInfoSize, FileInfo);
    if (Status != EFI_SUCCESS) {
      YuzuMsg (L"Failed Getting Yuzu_Config Info: %r\n", Status);
      return Status;
    }
  }
  ConfigDataSize = FileInfo->FileSize + 1;
  YuzuMsg (L"Config Size: 0x%x\n", ConfigDataSize);
  ConfigData = AllocateZeroPool (ConfigDataSize);
  FreePool (FileInfo);

  Status = ConfigFile->Read (ConfigFile, &ConfigDataSize, ConfigData);
  if (Status != EFI_SUCCESS) {
    YuzuMsg (L"Failed on Reading Yuzu_Config: %r\n", Status);
    return Status;
  }
  ConfigFile->Close (ConfigFile);

  YuzuMsg (L"Parsing Config\n");
  Chain = ParseConfig (ConfigData, ConfigDataSize);
  FreePool (ConfigData);
  if (Chain == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dispatch (Chain, ImageHandle, SystemTable);

  FreeOpChain (Chain);
  YuzuMsg (L"Program has ended\n");
  return EFI_SUCCESS;
}
