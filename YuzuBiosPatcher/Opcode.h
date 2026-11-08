/** @file
  Command implementations: image loading, module search, patching targets.
**/

#pragma once
#include "Yuzu.h"

//
// Load an EFI image from a filesystem ("Op LoadFromFS").
//
EFI_STATUS
LoadFS (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *FileName,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  EFI_HANDLE                  *AppImageHandle
  );

//
// Load an EFI image from a firmware volume by UI section name
// ("Op LoadFromFV").  CompatGuid selects the FV-style protocol used to
// read the section (see "Op Compatibility").
//
EFI_STATUS
LoadFV (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *FileName,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  EFI_HANDLE                  *AppImageHandle,
  EFI_GUID                    *CompatGuid
  );

//
// Find an already-loaded image by its FV UI name ("Op Loaded").
//
EFI_STATUS
FindLoadedImageFromName (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *FileName,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  EFI_GUID                    *CompatGuid
  );

//
// Find an already-loaded image by its FV file GUID ("Op NonamePE" /
// "Op NonameTE") - for modules without a UI name.
//
// Candidates are identified by the MEDIA_PIWG_FW_FILE device-path node of
// each loaded image plus a section-size cross-check against the section
// found in the firmware volumes.
//
EFI_STATUS
FindImageInRamByGuid (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *Name,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  UINT8                        SectionType,
  EFI_GUID                    *CompatGuid
  );

//
// Search every FV for a file by UI name and return one section.
// CompatGuid selects the FV-style protocol (NULL = EFI_FIRMWARE_VOLUME2).
//
EFI_STATUS
LocateAndLoadFvFromName (
  CHAR16           *Name,
  EFI_SECTION_TYPE  SectionType,
  UINT8           **Buffer,
  UINTN            *BufferSize,
  EFI_GUID         *CompatGuid OPTIONAL
  );

//
// Search every FV for a file by GUID and return one section.
//
EFI_STATUS
LocateSectionInFvByGuid (
  EFI_GUID         *FileGuid,
  EFI_SECTION_TYPE  SectionType,
  UINT8           **Buffer,
  UINTN            *BufferSize,
  EFI_GUID         *CompatGuid
  );

//
// "Op LoadGUIDandSavePE": load a PE by file GUID and dump the section to
// the filesystem as "<name>.bin".
//
EFI_STATUS
LoadGuidSavePe (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *Name,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  EFI_HANDLE                  *AppImageHandle,
  EFI_SYSTEM_TABLE            *SystemTable,
  EFI_GUID                    *CompatGuid
  );

//
// "Op LoadGUIDandSaveFreeform": extract a FREEFORM (subtype GUID) or RAW
// section by file GUID and report its base/size.
//
EFI_STATUS
LoadGuidSaveFreeform (
  EFI_HANDLE        ImageHandle,
  CHAR8            *FileGuidStr,
  UINT8           **SectionBase,
  UINTN            *SectionSize,
  CHAR8            *SectionGuidStr,
  EFI_SYSTEM_TABLE *SystemTable,
  EFI_GUID         *CompatGuid
  );

//
// "Op Exec": start the previously loaded image.
//
EFI_STATUS
Exec (
  EFI_HANDLE  *AppImageHandle
  );

//
// "Op UninstallProtocol": uninstall a protocol from every handle that has it.
//
EFI_STATUS
UninstallProtocol (
  CHAR8  *GuidStr
  );

//
// Dump a raw section buffer to "<name>.bin" on the first writable FS.
//
EFI_STATUS
DumpSectionToFs (
  EFI_SYSTEM_TABLE  *SystemTable,
  CHAR8             *Name,
  UINT8             *Buffer,
  UINTN              BufferSize
  );
