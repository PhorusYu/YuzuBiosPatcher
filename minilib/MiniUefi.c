/** @file
  MiniUefi - a minimal UEFI runtime library used by the lightweight
  (compiler-only) build of YuzuBiosPatcher.

  It implements exactly the EDK2 library API surface that
  YuzuBiosPatcher/ sources call, so the project can be compiled with any
  freestanding x86-64 PE compiler (e.g. MinGW GCC) without the EDK2 build
  system:

    gcc ... -I edk2/MdePkg/Include ... minilib/MiniUefi.c + the project sources
    gcc -nostdlib -Wl,--subsystem=efi_application -Wl,--entry=efi_main ...

  All functions use EFIAPI (MS x64 ABI) exactly as declared in the EDK2
  headers.
**/

#include <Uefi.h>
#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/RegularExpressionProtocol.h>
#include <Protocol/ShellParameters.h>

//
// Globals normally provided by UefiBootServicesTableLib.
//
EFI_BOOT_SERVICES  *gBS = NULL;
EFI_SYSTEM_TABLE   *gST = NULL;
EFI_HANDLE          gImageHandle = NULL;

//
// Protocol GUIDs (values from MdePkg.dec / ShellPkg.dec).
//
EFI_GUID gEfiLoadedImageProtocolGuid =
  { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }};
EFI_GUID gEfiSimpleFileSystemProtocolGuid =
  { 0x964E5B22, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }};
EFI_GUID gEfiFirmwareVolume2ProtocolGuid =
  { 0x220E73B6, 0x6BDB, 0x4413, { 0x84, 0x05, 0xB9, 0x74, 0xB1, 0x08, 0x61, 0x9A }};
EFI_GUID gEfiRegularExpressionProtocolGuid =
  { 0xB3F79D9A, 0x436C, 0xDC11, { 0xB0, 0x52, 0xCD, 0x85, 0xDF, 0x52, 0x4C, 0xE6 }};
EFI_GUID gEfiShellParametersProtocolGuid =
  { 0x752F3136, 0x4E16, 0x4FDC, { 0xA2, 0x2A, 0xE5, 0xF4, 0x68, 0x12, 0xF4, 0xCA }};
EFI_GUID gEfiDevicePathProtocolGuid =
  { 0x09576E91, 0x6D3F, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }};

//
// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
//
VOID *
EFIAPI
CopyMem (
  OUT VOID        *DestinationBuffer,
  IN CONST VOID   *SourceBuffer,
  IN UINTN         Length
  )
{
  UINT8        *D = (UINT8 *)DestinationBuffer;
  CONST UINT8  *S = (CONST UINT8 *)SourceBuffer;

  if (D == S || Length == 0) {
    return DestinationBuffer;
  }
  if (D < S) {
    while (Length--) {
      *D++ = *S++;
    }
  } else {
    D += Length;
    S += Length;
    while (Length--) {
      *(--D) = *(--S);
    }
  }
  return DestinationBuffer;
}

VOID *
EFIAPI
ZeroMem (
  OUT VOID   *Buffer,
  IN UINTN    Length
  )
{
  UINT8  *P = (UINT8 *)Buffer;

  while (Length--) {
    *P++ = 0;
  }
  return Buffer;
}

INTN
EFIAPI
CompareMem (
  IN CONST VOID  *DestinationBuffer,
  IN CONST VOID  *SourceBuffer,
  IN UINTN        Length
  )
{
  CONST UINT8  *A = (CONST UINT8 *)DestinationBuffer;
  CONST UINT8  *B = (CONST UINT8 *)SourceBuffer;

  if (A == B || Length == 0) {
    return 0;
  }
  while (Length--) {
    if (*A != *B) {
      return (INTN)*A - (INTN)*B;
    }
    A++;
    B++;
  }
  return 0;
}

//
// Compiler runtime aliases: struct copies make the compiler emit
// memcpy()/memset() calls even with -fno-builtin.
//
void *
memcpy (
  void        *Destination,
  const void  *Source,
  __SIZE_TYPE__ Length
  )
{
  return CopyMem (Destination, Source, (UINTN)Length);
}

void *
memmove (
  void        *Destination,
  const void  *Source,
  __SIZE_TYPE__ Length
  )
{
  return CopyMem (Destination, Source, (UINTN)Length);
}

void *
memset (
  void          *Destination,
  int            Value,
  __SIZE_TYPE__  Length
  )
{
  UINT8  *P = (UINT8 *)Destination;

  while (Length--) {
    *P++ = (UINT8)Value;
  }
  return Destination;
}

int
memcmp (
  const void  *A,
  const void  *B,
  __SIZE_TYPE__ Length
  )
{
  return (int)CompareMem (A, B, (UINTN)Length);
}

BOOLEAN
EFIAPI
CompareGuid (
  IN CONST GUID  *Guid1,
  IN CONST GUID  *Guid2
  )
{
  return (BOOLEAN)(CompareMem (Guid1, Guid2, sizeof (GUID)) == 0);
}

EFI_GUID *
EFIAPI
CopyGuid (
  OUT EFI_GUID       *DestinationGuid,
  IN  CONST EFI_GUID *SourceGuid
  )
{
  if (DestinationGuid != SourceGuid) {
    CopyMem (DestinationGuid, SourceGuid, sizeof (EFI_GUID));
  }
  return DestinationGuid;
}

//
// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------
//
UINTN
EFIAPI
AsciiStrLen (
  IN CONST CHAR8  *String
  )
{
  UINTN  Len = 0;

  while (String[Len] != '\0') {
    Len++;
  }
  return Len;
}

UINTN
EFIAPI
StrLen (
  IN CONST CHAR16  *String
  )
{
  UINTN  Len = 0;

  while (String[Len] != L'\0') {
    Len++;
  }
  return Len;
}

CHAR8 *
EFIAPI
AsciiStrStr (
  IN CONST CHAR8  *String,
  IN CONST CHAR8  *SearchFor
  )
{
  UINTN  SearchLen;

  if (String == NULL || SearchFor == NULL) {
    return NULL;
  }
  SearchLen = AsciiStrLen (SearchFor);
  if (SearchLen == 0) {
    return (CHAR8 *)String;
  }
  for (; *String != '\0'; String++) {
    CONST CHAR8  *A = String;
    CONST CHAR8  *B = SearchFor;
    while (*A != '\0' && *B != '\0' && *A == *B) {
      A++;
      B++;
    }
    if (*B == '\0') {
      return (CHAR8 *)String;
    }
  }
  return NULL;
}

INTN
EFIAPI
AsciiStrCmp (
  IN CONST CHAR8  *FirstString,
  IN CONST CHAR8  *SecondString
  )
{
  while (*FirstString != '\0' && *FirstString == *SecondString) {
    FirstString++;
    SecondString++;
  }
  return (INTN)(UINT8)*FirstString - (INTN)(UINT8)*SecondString;
}

INTN
EFIAPI
StrCmp (
  IN CONST CHAR16  *FirstString,
  IN CONST CHAR16  *SecondString
  )
{
  while (*FirstString != L'\0' && *FirstString == *SecondString) {
    FirstString++;
    SecondString++;
  }
  return (INTN)*FirstString - (INTN)*SecondString;
}

RETURN_STATUS
EFIAPI
StrCatS (
  IN OUT CHAR16        *Destination,
  IN     UINTN          DestMax,
  IN     CONST CHAR16  *Source
  )
{
  UINTN  DestLen = StrLen (Destination);
  UINTN  SrcLen = StrLen (Source);

  if (DestLen + SrcLen + 1 > DestMax) {
    return RETURN_BUFFER_TOO_SMALL;
  }
  CopyMem (Destination + DestLen, Source, (SrcLen + 1) * sizeof (CHAR16));
  return RETURN_SUCCESS;
}

STATIC
INTN
HexCharToValue (
  IN CHAR8  C
  )
{
  if (C >= '0' && C <= '9') {
    return C - '0';
  }
  if (C >= 'a' && C <= 'f') {
    return C - 'a' + 10;
  }
  if (C >= 'A' && C <= 'F') {
    return C - 'A' + 10;
  }
  return -1;
}

UINT64
EFIAPI
AsciiStrHexToUint64 (
  IN CONST CHAR8  *String
  )
{
  UINT64  Value = 0;

  while (*String == ' ' || *String == '0' || *String == 'x' || *String == 'X') {
    if ((*String == 'x' || *String == 'X') && Value == 0) {
      String++;
      break;
    }
    String++;
  }
  for (;;) {
    INTN  V = HexCharToValue (*String);

    if (V < 0) {
      break;
    }
    Value = (Value << 4) | (UINT64)V;
    String++;
  }
  return Value;
}

UINT64
EFIAPI
AsciiStrDecimalToUint64 (
  IN CONST CHAR8  *String
  )
{
  UINT64  Value = 0;

  while (*String == ' ') {
    String++;
  }
  while (*String >= '0' && *String <= '9') {
    Value = Value * 10 + (UINT64)(*String - '0');
    String++;
  }
  return Value;
}

RETURN_STATUS
EFIAPI
AsciiStrHexToBytes (
  IN  CONST CHAR8  *String,
  IN  UINTN         Length,
  OUT UINT8        *Buffer,
  IN  UINTN         MaxBufferSize
  )
{
  UINTN  Index;

  if (String == NULL || Buffer == NULL) {
    return RETURN_INVALID_PARAMETER;
  }
  if (Length > 0xF4240 || (Length & 1) != 0) {
    return (Length & 1) != 0 ? RETURN_UNSUPPORTED : RETURN_INVALID_PARAMETER;
  }
  if (MaxBufferSize < Length / 2) {
    return RETURN_BUFFER_TOO_SMALL;
  }
  //
  // Validate first, convert second - a non-hex digit aborts without output.
  //
  for (Index = 0; Index < Length; Index++) {
    if (HexCharToValue (String[Index]) < 0) {
      return RETURN_UNSUPPORTED;
    }
  }
  for (Index = 0; Index < Length; Index += 2) {
    Buffer[Index / 2] = (UINT8)((HexCharToValue (String[Index]) << 4) |
                                HexCharToValue (String[Index + 1]));
  }
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
StrToGuid (
  IN  CONST CHAR16  *String,
  OUT GUID          *Guid
  )
{
  UINTN   Index;
  UINTN   Pos;
  UINT32  Acc;
  UINT8   Data4[8];

  if (String == NULL || Guid == NULL) {
    return RETURN_INVALID_PARAMETER;
  }
  //
  // Accept XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX (36 characters).
  //
  for (Index = 0; Index < 36; Index++) {
    if (Index == 8 || Index == 13 || Index == 18 || Index == 23) {
      if (String[Index] != L'-') {
        return RETURN_UNSUPPORTED;
      }
    } else if (HexCharToValue ((CHAR8)(String[Index] & 0xFF)) < 0) {
      return RETURN_UNSUPPORTED;
    }
  }
  if (String[36] != L'\0') {
    return RETURN_UNSUPPORTED;
  }

  Acc = 0;
  for (Pos = 0; Pos < 8; Pos++) {
    Acc = (Acc << 4) | (UINT32)HexCharToValue ((CHAR8)(String[Pos] & 0xFF));
  }
  Guid->Data1 = Acc;

  Acc = 0;
  for (Pos = 9; Pos < 13; Pos++) {
    Acc = (Acc << 4) | (UINT32)HexCharToValue ((CHAR8)(String[Pos] & 0xFF));
  }
  Guid->Data2 = (UINT16)Acc;

  Acc = 0;
  for (Pos = 14; Pos < 18; Pos++) {
    Acc = (Acc << 4) | (UINT32)HexCharToValue ((CHAR8)(String[Pos] & 0xFF));
  }
  Guid->Data3 = (UINT16)Acc;

  for (Index = 0; Index < 8; Index++) {
    //
    // Two hex digits per byte, plus the '-' separator before byte 2.
    //
    Pos = 19 + 2 * Index + (Index >= 2 ? 1 : 0);
    Acc = (UINT32)HexCharToValue ((CHAR8)(String[Pos] & 0xFF)) << 4;
    Pos++;
    Acc |= (UINT32)HexCharToValue ((CHAR8)(String[Pos] & 0xFF));
    Data4[Index] = (UINT8)Acc;
  }
  CopyMem (Guid->Data4, Data4, sizeof (Data4));
  return RETURN_SUCCESS;
}

//
// ---------------------------------------------------------------------------
// Memory allocation (loader data pool)
// ---------------------------------------------------------------------------
//
VOID *
EFIAPI
AllocatePool (
  IN UINTN  AllocationSize
  )
{
  VOID       *Buffer = NULL;
  EFI_STATUS  Status;

  Status = gBS->AllocatePool (EfiLoaderData, AllocationSize, &Buffer);
  if (EFI_ERROR (Status)) {
    return NULL;
  }
  return Buffer;
}

VOID *
EFIAPI
AllocateZeroPool (
  IN UINTN  AllocationSize
  )
{
  VOID  *Buffer = AllocatePool (AllocationSize);

  if (Buffer != NULL) {
    ZeroMem (Buffer, AllocationSize);
  }
  return Buffer;
}

VOID
EFIAPI
FreePool (
  IN VOID  *Buffer
  )
{
  if (Buffer != NULL) {
    gBS->FreePool (Buffer);
  }
}

//
// ---------------------------------------------------------------------------
// EFI_STATUS names for %r (same strings as the classic status table)
// ---------------------------------------------------------------------------
//
STATIC CONST CHAR16 *mStatusWarning[] = {
  L"Success",
  L"Warning Unknown Glyph",
  L"Warning Delete Failure",
  L"Warning Write Failure",
  L"Warning Buffer Too Small",
  L"Warning Stale Data",
  L"Warning File System",
  L"Warning Reset Required"
};

STATIC CONST CHAR16 *mStatusError[] = {
  L"Load Error",
  L"Invalid Parameter",
  L"Unsupported",
  L"Bad Buffer Size",
  L"Buffer Too Small",
  L"Not Ready",
  L"Device Error",
  L"Write Protected",
  L"Out of Resources",
  L"Volume Corrupt",
  L"Volume Full",
  L"No Media",
  L"Media changed",
  L"Not Found",
  L"Access Denied",
  L"No Response",
  L"No mapping",
  L"Time out",
  L"Not started",
  L"Already started",
  L"Aborted",
  L"ICMP Error",
  L"TFTP Error",
  L"Protocol Error",
  L"Incompatible Version",
  L"Security Violation",
  L"CRC Error",
  L"End of Media",
  L"Reserved (29)",
  L"Reserved (30)",
  L"End of File",
  L"Invalid Language",
  L"Compromised Data",
  L"IP Address Conflict",
  L"HTTP Error"
};

//
// ---------------------------------------------------------------------------
// Minimal VSPrint: supports %d %i %u %x %X %p %s %a %c %r %g %%
// with optional zero-padding width (e.g. %02x).  %s follows the format
// string width: CHAR16* in wide formats, CHAR8* in ASCII formats.
// ---------------------------------------------------------------------------
//
typedef struct {
  CHAR16  *W;      // wide output (NULL = ASCII mode)
  CHAR8   *A;      // ASCII output
  UINTN    Max;    // buffer size in bytes
  UINTN    Count;  // units produced
} PRINT_CONTEXT;

STATIC
VOID
PutChar (
  IN OUT PRINT_CONTEXT  *Ctx,
  IN     CHAR16          Ch
  )
{
  UINTN  MaxUnits = Ctx->Max / (Ctx->W != NULL ? sizeof (CHAR16) : sizeof (CHAR8));

  if (Ctx->Count + 1 >= MaxUnits) {
    return;
  }
  if (Ctx->W != NULL) {
    Ctx->W[Ctx->Count] = Ch;
  } else {
    Ctx->A[Ctx->Count] = (CHAR8)Ch;
  }
  Ctx->Count++;
}

STATIC
VOID
PutStr (
  IN OUT PRINT_CONTEXT  *Ctx,
  IN     CONST CHAR16   *Wide OPTIONAL,
  IN     CONST CHAR8    *Ascii OPTIONAL
  )
{
  if (Wide != NULL) {
    while (*Wide != L'\0') {
      PutChar (Ctx, *Wide++);
    }
  } else if (Ascii != NULL) {
    while (*Ascii != '\0') {
      PutChar (Ctx, (CHAR16)(UINT8)*Ascii++);
    }
  }
}

STATIC
VOID
PutUnsigned (
  IN OUT PRINT_CONTEXT  *Ctx,
  IN     UINT64          Value,
  IN     UINTN           Base,
  IN     BOOLEAN         Upper,
  IN     UINTN           Width
  )
{
  //
  // One extra slot: digits fill from the end of the array and PutStr walks
  // forward to the first NUL - slot 32 is always the terminator.
  //
  CHAR16  Buf[33];
  UINTN   Pos = sizeof (Buf) / sizeof (Buf[0]) - 1;
  UINTN   Digits = 0;
  CONST CHAR8 *Alphabet = Upper ? "0123456789ABCDEF" : "0123456789abcdef";

  ZeroMem (Buf, sizeof (Buf));
  do {
    Buf[--Pos] = (CHAR16)(UINT8)Alphabet[Value % Base];
    Value /= Base;
    Digits++;
  } while (Value != 0 && Pos > 0);

  while (Digits < Width && Pos > 0) {
    Buf[--Pos] = L'0';
    Digits++;
  }
  PutStr (Ctx, &Buf[Pos], NULL);
}

STATIC
UINTN
DoPrint (
  IN BOOLEAN       Wide,
  OUT VOID         *Buffer,
  IN UINTN          BufferSize,
  IN CONST VOID    *Format,
  IN VA_LIST        Marker
  )
{
  PRINT_CONTEXT  Ctx;
  CONST CHAR16  *FmtW = Wide ? (CONST CHAR16 *)Format : NULL;
  CONST CHAR8   *FmtA = Wide ? NULL : (CONST CHAR8 *)Format;

  Ctx.W = Wide ? (CHAR16 *)Buffer : NULL;
  Ctx.A = Wide ? NULL : (CHAR8 *)Buffer;
  Ctx.Max = BufferSize;
  Ctx.Count = 0;

  for (;;) {
    CHAR16  Ch;
    UINTN   Width;
    BOOLEAN ZeroPad;

    if (Wide) {
      Ch = *FmtW++;
      if (Ch == L'\0') {
        break;
      }
    } else {
      Ch = (CHAR16)(UINT8)*FmtA++;
      if (Ch == 0) {
        break;
      }
    }

    if (Ch != L'%') {
      PutChar (&Ctx, Ch);
      continue;
    }

    //
    // %[0][width]conv
    //
    ZeroPad = FALSE;
    Width = 0;
    Ch = Wide ? *FmtW++ : (CHAR16)(UINT8)*FmtA++;
    if (Ch == L'0') {
      ZeroPad = TRUE;
      Ch = Wide ? *FmtW++ : (CHAR16)(UINT8)*FmtA++;
    }
    while (Ch >= L'0' && Ch <= L'9') {
      Width = Width * 10 + (UINTN)(Ch - L'0');
      Ch = Wide ? *FmtW++ : (CHAR16)(UINT8)*FmtA++;
    }

    switch (Ch) {
    case L'%':
      PutChar (&Ctx, L'%');
      break;
    case L'd':
    case L'i':
      {
        INT64  Value = VA_ARG (Marker, INT64);

        if (Value < 0) {
          PutChar (&Ctx, L'-');
          PutUnsigned (&Ctx, (UINT64)(-Value), 10, FALSE, Width);
        } else {
          PutUnsigned (&Ctx, (UINT64)Value, 10, FALSE, Width);
        }
      }
      break;
    case L'u':
      PutUnsigned (&Ctx, VA_ARG (Marker, UINT64), 10, FALSE, Width);
      break;
    case L'x':
      PutUnsigned (&Ctx, VA_ARG (Marker, UINT64), 16, FALSE, ZeroPad ? Width : 0);
      break;
    case L'X':
      PutUnsigned (&Ctx, VA_ARG (Marker, UINT64), 16, TRUE, ZeroPad ? Width : 0);
      break;
    case L'p':
      PutChar (&Ctx, L'0');
      PutChar (&Ctx, L'x');
      PutUnsigned (&Ctx, (UINT64)(UINTN)VA_ARG (Marker, VOID *), 16, FALSE, 0);
      break;
    case L'c':
      PutChar (&Ctx, (CHAR16)VA_ARG (Marker, UINTN));
      break;
    case L's':
      if (Wide) {
        CONST CHAR16  *S = VA_ARG (Marker, CONST CHAR16 *);

        PutStr (&Ctx, S != NULL ? S : L"(null)", NULL);
      } else {
        CONST CHAR8  *S = VA_ARG (Marker, CONST CHAR8 *);

        PutStr (&Ctx, NULL, S != NULL ? S : "(null)");
      }
      break;
    case L'a':
      {
        CONST CHAR8  *S = VA_ARG (Marker, CONST CHAR8 *);

        PutStr (&Ctx, NULL, S != NULL ? S : "(null)");
      }
      break;
    case L'S':
      {
        CONST CHAR16  *S = VA_ARG (Marker, CONST CHAR16 *);

        PutStr (&Ctx, S != NULL ? S : L"(null)", NULL);
      }
      break;
    case L'r':
      {
        EFI_STATUS  Status = VA_ARG (Marker, EFI_STATUS);

        if (Status == 0) {
          PutStr (&Ctx, mStatusWarning[0], NULL);
        } else if (((UINTN)Status & 0x8000000000000000ULL) == 0 &&
                   (UINTN)Status < sizeof (mStatusWarning) / sizeof (mStatusWarning[0])) {
          PutStr (&Ctx, mStatusWarning[Status], NULL);
        } else {
          UINTN  Index = (UINTN)(Status & 0x7FFFFFFFFFFFFFFFULL);

          if (Index >= 1 &&
              Index <= sizeof (mStatusError) / sizeof (mStatusError[0])) {
            PutStr (&Ctx, mStatusError[Index - 1], NULL);
          } else {
            PutChar (&Ctx, L'0');
            PutChar (&Ctx, L'x');
            PutUnsigned (&Ctx, (UINT64)Status, 16, FALSE, 0);
          }
        }
      }
      break;
    case L'g':
      {
        EFI_GUID  *Guid = VA_ARG (Marker, EFI_GUID *);
        UINTN      Len;

        PutUnsigned (&Ctx, Guid->Data1, 16, TRUE, 8);
        PutChar (&Ctx, L'-');
        PutUnsigned (&Ctx, Guid->Data2, 16, TRUE, 4);
        PutChar (&Ctx, L'-');
        PutUnsigned (&Ctx, Guid->Data3, 16, TRUE, 4);
        PutChar (&Ctx, L'-');
        PutUnsigned (&Ctx, Guid->Data4[0], 16, TRUE, 2);
        PutUnsigned (&Ctx, Guid->Data4[1], 16, TRUE, 2);
        PutChar (&Ctx, L'-');
        for (Len = 2; Len < 8; Len++) {
          PutUnsigned (&Ctx, Guid->Data4[Len], 16, TRUE, 2);
        }
      }
      break;
    default:
      PutChar (&Ctx, L'%');
      PutChar (&Ctx, Ch);
      break;
    }
  }

  //
  // Terminate.
  //
  if (Ctx.Count * (Wide ? sizeof (CHAR16) : sizeof (CHAR8)) < Ctx.Max) {
    if (Wide) {
      Ctx.W[Ctx.Count] = L'\0';
    } else {
      Ctx.A[Ctx.Count] = '\0';
    }
  }
  return Ctx.Count;
}

UINTN
EFIAPI
UnicodeVSPrint (
  OUT CHAR16        *StartOfBuffer,
  IN  UINTN          BufferSize,
  IN  CONST CHAR16  *FormatString,
  IN  VA_LIST        Marker
  )
{
  return DoPrint (TRUE, StartOfBuffer, BufferSize, FormatString, Marker);
}

UINTN
EFIAPI
AsciiVSPrint (
  OUT CHAR8        *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR8  *FormatString,
  IN  VA_LIST       Marker
  )
{
  return DoPrint (FALSE, StartOfBuffer, BufferSize, FormatString, Marker);
}

UINTN
EFIAPI
UnicodeSPrint (
  OUT CHAR16        *StartOfBuffer,
  IN  UINTN          BufferSize,
  IN  CONST CHAR16  *FormatString,
  ...
  )
{
  VA_LIST  Marker;
  UINTN    Result;

  VA_START (Marker, FormatString);
  Result = DoPrint (TRUE, StartOfBuffer, BufferSize, FormatString, Marker);
  VA_END (Marker);
  return Result;
}

UINTN
EFIAPI
AsciiSPrint (
  OUT CHAR8        *StartOfBuffer,
  IN  UINTN         BufferSize,
  IN  CONST CHAR8  *FormatString,
  ...
  )
{
  VA_LIST  Marker;
  UINTN    Result;

  VA_START (Marker, FormatString);
  Result = DoPrint (FALSE, StartOfBuffer, BufferSize, FormatString, Marker);
  VA_END (Marker);
  return Result;
}

//
// UefiLib Print: format and write to the console.
//
UINTN
EFIAPI
Print (
  IN CONST CHAR16  *Format,
  ...
  )
{
  CHAR16   Buffer[256];
  CHAR16   Out[512];
  VA_LIST  Marker;
  UINTN    Result;
  UINTN    i;
  UINTN    j = 0;

  VA_START (Marker, Format);
  Result = DoPrint (TRUE, Buffer, sizeof (Buffer), Format, Marker);
  VA_END (Marker);
  if (gST != NULL && gST->ConOut != NULL) {
    //
    // Expand '\n' to CR+LF: a bare line feed keeps the cursor column, so
    // consecutive lines would drift to the right like a staircase.
    //
    for (i = 0; Buffer[i] != L'\0' && j < 510; i++) {
      if (Buffer[i] == L'\n' && (i == 0 || Buffer[i - 1] != L'\r')) {
        Out[j++] = L'\r';
      }
      Out[j++] = Buffer[i];
    }
    Out[j] = L'\0';
    gST->ConOut->OutputString (gST->ConOut, Out);
  }
  return Result;
}

//
// ---------------------------------------------------------------------------
// Device path helper
// ---------------------------------------------------------------------------
//
UINTN
EFIAPI
DevicePathNodeLength (
  IN CONST VOID  *Node
  )
{
  CONST EFI_DEVICE_PATH_PROTOCOL  *Dp = (CONST EFI_DEVICE_PATH_PROTOCOL *)Node;

  return (UINTN)Dp->Length[0] | ((UINTN)Dp->Length[1] << 8);
}

BOOLEAN
EFIAPI
IsDevicePathEnd (
  IN CONST VOID  *Node
  )
{
  CONST EFI_DEVICE_PATH_PROTOCOL  *Dp = (CONST EFI_DEVICE_PATH_PROTOCOL *)Node;

  return (BOOLEAN)(Dp->Type == END_DEVICE_PATH_TYPE &&
                   Dp->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE);
}

UINTN
EFIAPI
GetDevicePathSize (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  CONST EFI_DEVICE_PATH_PROTOCOL  *Node = DevicePath;
  UINTN                            Size = 0;

  while (!IsDevicePathEnd (Node)) {
    UINTN  NodeLen = DevicePathNodeLength (Node);

    Size += NodeLen;
    Node = (CONST EFI_DEVICE_PATH_PROTOCOL *)((CONST UINT8 *)Node + NodeLen);
  }
  return Size + DevicePathNodeLength (Node);   // include the end node
}

EFI_DEVICE_PATH_PROTOCOL *
EFIAPI
FileDevicePath (
  IN EFI_HANDLE        Device OPTIONAL,
  IN CONST CHAR16     *FileName
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath = NULL;
  EFI_DEVICE_PATH_PROTOCOL  *Result;
  EFI_DEVICE_PATH_PROTOCOL  *FileNode;
  UINTN                      DeviceSize = 0;
  UINTN                      NameSize;
  UINTN                      FileNodeSize;
  UINT8                     *P;

  if (Device != NULL) {
    gBS->HandleProtocol (Device, &gEfiDevicePathProtocolGuid, (VOID **)&DevicePath);
    if (DevicePath != NULL) {
      DeviceSize = GetDevicePathSize (DevicePath);
      //
      // Drop the trailing end node - a new one is appended below.
      //
      DeviceSize -= 4;
    }
  }

  NameSize = (StrLen (FileName) + 1) * sizeof (CHAR16);
  FileNodeSize = 4 + NameSize;

  Result = (EFI_DEVICE_PATH_PROTOCOL *)AllocatePool (DeviceSize + FileNodeSize + 4);
  if (Result == NULL) {
    return NULL;
  }

  P = (UINT8 *)Result;
  if (DeviceSize > 0 && DevicePath != NULL) {
    CopyMem (P, DevicePath, DeviceSize);
    P += DeviceSize;
  }

  FileNode = (EFI_DEVICE_PATH_PROTOCOL *)P;
  FileNode->Type = MEDIA_DEVICE_PATH;
  FileNode->SubType = MEDIA_FILEPATH_DP;
  FileNode->Length[0] = (UINT8)(FileNodeSize & 0xFF);
  FileNode->Length[1] = (UINT8)(FileNodeSize >> 8);
  CopyMem (P + 4, FileName, NameSize);
  P += FileNodeSize;

  P[0] = END_DEVICE_PATH_TYPE;
  P[1] = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  P[2] = 4;
  P[3] = 0;

  return Result;
}

//
// ---------------------------------------------------------------------------
// Boot breadcrumb: appends one line to \boot.log on the first writable
// filesystem.  Used for black-screen diagnosis - after a failed boot the
// file shows how far the image actually got.
// ---------------------------------------------------------------------------
//
EFI_STATUS
EFIAPI
Crumb (
  IN CONST CHAR8  *Msg
  )
{
  EFI_HANDLE                       *Handles = NULL;
  UINTN                            Count = 0;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Sfs;
  EFI_FILE                         *Root;
  EFI_FILE                         *File;
  UINTN                            Len;

  if (gBS == NULL) {
    return EFI_NOT_READY;
  }
  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                           NULL, &Count, &Handles))) {
    for (Index = 0; Index < Count; Index++) {
      if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid,
                                          (VOID **)&Sfs))) {
        continue;
      }
      if (EFI_ERROR (Sfs->OpenVolume (Sfs, &Root))) {
        continue;
      }
      if (EFI_ERROR (Root->Open (Root, &File, L"boot.log",
                                 EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ | EFI_FILE_MODE_CREATE, 0))) {
        continue;
      }
      File->SetPosition (File, 0xFFFFFFFFFFFFFFFFULL);   // append at end
      Len = 0;
      while (Msg[Len] != '\0') {
        Len++;
      }
      File->Write (File, &Len, (VOID *)Msg);
      File->Flush (File);
      File->Close (File);
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

//
// ---------------------------------------------------------------------------
// Entry point shim
// ---------------------------------------------------------------------------
//
EFI_STATUS
EFIAPI
YuzuEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  );

EFI_STATUS
EFIAPI
efi_main (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  gImageHandle = ImageHandle;
  gST = SystemTable;
  gBS = SystemTable->BootServices;
  return YuzuEntry (ImageHandle, SystemTable);
}
