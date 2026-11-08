/** @file
  Utility helpers.
**/

#pragma once
#include "Yuzu.h"

//
// Return the UI name of a loaded image by reading the FV file device path
// node and the EFI_SECTION_USER_INTERFACE section.
//
CHAR16 *
FindLoadedImageFileName (
  IN EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage
  );

//
// Convert an ASCII GUID string to EFI_GUID.
//
EFI_STATUS
YuzuStrToGuid (
  IN  CHAR8    *Str,
  OUT EFI_GUID *Guid
  );

//
// Wildcard / regex matcher used by "Op Patch" (Pattern mode).
//
// The bytes at Data[0..Len) are hex-encoded into a CHAR16 string and the
// pattern text (e.g. "000000FFFF00....0A821206......000F0F") is used as a
// regular expression against it: every '.' matches one hex digit, so '..'
// matches one arbitrary byte.  Matching is performed by the
// EFI_REGULAR_EXPRESSION_PROTOCOL provided by Oniguruma.efi.
//
EFI_STATUS
MatchPatternAt (
  IN  UINT8                          *Data,
  IN  CHAR8                          *PatternText,
  IN  UINT16                          Len,
  IN  EFI_REGULAR_EXPRESSION_PROTOCOL *Regex,
  OUT BOOLEAN                        *Found
  );

UINTN
YuzuAsciiStrLen (
  IN CONST CHAR8  *String
  );
