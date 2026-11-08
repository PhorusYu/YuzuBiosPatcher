/** @file
  Utility helpers: bilingual messaging, GUID conversion, wildcard matching.
**/

#include "Utility.h"

//
// ---------------------------------------------------------------------------
// Messaging
// ---------------------------------------------------------------------------
//
VOID
LogToFile (
  EFI_FILE  *File,
  CHAR8     *String
  )
{
  UINTN  Size = AsciiStrLen (String);

  File->Write (File, &Size, String);
  File->Flush (File);
}

VOID
YuzuMsg (
  CONST CHAR16  *Format,
  ...
  )
{
  CHAR16   Buf[256];
  VA_LIST  Args;

  VA_START (Args, Format);
  UnicodeVSPrint (Buf, sizeof (Buf), Format, Args);
  VA_END (Args);
  Print (L"%s", Buf);

  //
  // Mirror every screen message into Yuzu.log so results can be reviewed
  // after the run (ASCII; non-ASCII characters become '?').
  //
  if (LogFile != NULL) {
    CHAR8  A[256];
    UINTN  i;

    for (i = 0; i < 255 && Buf[i] != L'\0'; i++) {
      A[i] = (CHAR8)(Buf[i] < 0x80 ? Buf[i] : '?');
    }
    A[i] = '\0';
    LogToFile (LogFile, A);
  }
}

VOID
YuzuLog (
  CONST CHAR8  *Fmt,
  ...
  )
{
  CHAR8    Buf[256];
  VA_LIST  Args;

  VA_START (Args, Fmt);
  AsciiVSPrint (Buf, sizeof (Buf), Fmt, Args);
  VA_END (Args);
  LogToFile (LogFile, Buf);
}

VOID
PrintDump (
  UINT16  Size,
  UINT8   *DUMP
  )
{
  CHAR8  Buf[64];

  for (UINT16 i = 0; i < Size; i++) {
    if (i % 0x10 == 0) {
      LogToFile (LogFile, "\n\t");
    }
    AsciiSPrint (Buf, sizeof (Buf), "%02x ", DUMP[i]);
    LogToFile (LogFile, Buf);
  }
  LogToFile (LogFile, "\n\t");
}

//
// ---------------------------------------------------------------------------
// GUID / name helpers
// ---------------------------------------------------------------------------
//
CHAR16 *
FindLoadedImageFileName (
  IN EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage
  )
{
  EFI_GUID                           *NameGuid;
  EFI_STATUS                         Status;
  EFI_FIRMWARE_VOLUME2_PROTOCOL      *Fv;
  VOID                               *Buffer;
  UINTN                              BufferSize;
  UINT32                             AuthenticationStatus;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  *FvFileNode;

  if ((LoadedImage == NULL) || (LoadedImage->FilePath == NULL)) {
    return NULL;
  }

  //
  // The device path must start with a MEDIA_PIWG_FW_FILE node
  // (Type 4, SubType 6); the FV file GUID follows the node header.
  //
  FvFileNode = (MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)LoadedImage->FilePath;
  if (FvFileNode->Header.Type != MEDIA_DEVICE_PATH ||
      FvFileNode->Header.SubType != MEDIA_PIWG_FW_FILE_DP) {
    return NULL;
  }
  NameGuid = &FvFileNode->FvFileName;

  Status = gBS->HandleProtocol (
                  LoadedImage->DeviceHandle,
                  &gEfiFirmwareVolume2ProtocolGuid,
                  (VOID **)&Fv
                  );
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  Buffer = NULL;
  Status = Fv->ReadSection (
                 Fv,
                 NameGuid,
                 EFI_SECTION_USER_INTERFACE,
                 0,
                 &Buffer,
                 &BufferSize,
                 &AuthenticationStatus
                 );
  if (EFI_ERROR (Status)) {
    return NULL;
  }
  return Buffer;
}

EFI_STATUS
YuzuStrToGuid (
  IN  CHAR8    *Str,
  OUT EFI_GUID *Guid
  )
{
  CHAR16  Buf[64];

  if (Str == NULL || Guid == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  UnicodeSPrint (Buf, sizeof (Buf), L"%a", Str);
  return StrToGuid (Buf, Guid);
}

//
// ---------------------------------------------------------------------------
// Wildcard matching
// ---------------------------------------------------------------------------
//
EFI_STATUS
MatchPatternAt (
  IN  UINT8                          *Data,
  IN  CHAR8                          *PatternText,
  IN  UINT16                          Len,
  IN  EFI_REGULAR_EXPRESSION_PROTOCOL *Regex,
  OUT BOOLEAN                        *Found
  )
{
  EFI_STATUS  Status;
  CHAR16      HexStr[0x200];
  CHAR16      OneByte[0x200];
  CHAR16      PatWide[0x1FE];
  UINTN       CapturesSize;
  UINTN       Index;

  ZeroMem (HexStr, sizeof (HexStr));
  ZeroMem (OneByte, sizeof (OneByte));
  ZeroMem (PatWide, sizeof (PatWide));

  //
  // Encode the candidate bytes as UPPERCASE hex; the pattern text is also
  // upper-cased below so matching never depends on the case the user typed.
  //
  for (Index = 0; Index < Len; Index++) {
    UnicodeSPrint (OneByte, sizeof (OneByte), L"%02X", Data[Index]);
    StrCatS (HexStr, sizeof (HexStr) / sizeof (CHAR16), OneByte);
  }

  for (Index = 0; PatternText[Index] != '\0'; Index++) {
    CHAR8  C = PatternText[Index];

    PatWide[Index] = (CHAR16)(UINT8)((C >= 'a' && C <= 'z') ? (C - 'a' + 'A') : C);
  }

  //
  // Match(This, String, Pattern, SyntaxType=POSIX, &Result,
  //       Captures=NULL, &CapturesSize)
  //
  CapturesSize = 0;
  Status = Regex->MatchString (
                    Regex,
                    HexStr,
                    PatWide,
                    0,
                    Found,
                    NULL,
                    &CapturesSize
                    );
  return Status;
}

UINTN
YuzuAsciiStrLen (
  IN CONST CHAR8  *String
  )
{
  UINTN  Len;

  for (Len = 0; String[Len] != '\0'; Len++) {
  }
  return Len;
}
