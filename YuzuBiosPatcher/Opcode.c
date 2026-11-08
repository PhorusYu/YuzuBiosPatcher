/** @file
  Command implementations.
**/

#include "Opcode.h"
#include "Utility.h"

EFI_STATUS
LoadFS (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *FileName,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  EFI_HANDLE                  *AppImageHandle
  )
{
  UINTN                          NumHandles;
  UINTN                          Index;
  EFI_HANDLE                    *SFS_Handles;
  EFI_STATUS                     Status;
  EFI_BLOCK_IO_PROTOCOL         *BlkIo;
  EFI_DEVICE_PATH_PROTOCOL      *FilePath;
  CHAR16                         FileName16[255];

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &NumHandles,
                  &SFS_Handles
                  );
  if (Status != EFI_SUCCESS) {
    YuzuMsg (L"Could not find any handle on EfiSimpleFileSystemProtocol\n");
    return Status;
  }
  YuzuMsg (L"No of Handles - %d\n", NumHandles);

  for (Index = 0; Index < NumHandles; Index++) {
    Status = gBS->OpenProtocol (
                    SFS_Handles[Index],
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID **)&BlkIo,
                    ImageHandle,
                    NULL,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (Status != EFI_SUCCESS) {
      YuzuMsg (L"Protocol is not supported - %r\n", Status);
      return Status;
    }

    UnicodeSPrint (FileName16, sizeof (FileName16), L"%a", FileName);
    FilePath = FileDevicePath (SFS_Handles[Index], FileName16);
    Status = gBS->LoadImage (FALSE, ImageHandle, FilePath, NULL, 0, AppImageHandle);
    if (Status != EFI_SUCCESS) {
      YuzuMsg (L"Could not load the image on Handle %d - %r\n", Index, Status);
      continue;
    }
    YuzuMsg (L"Loaded the image with success on Handle %d\n", Index);
    Status = gBS->OpenProtocol (
                    *AppImageHandle,
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **)ImageInfo,
                    ImageHandle,
                    NULL,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    return Status;
  }
  return Status;
}

EFI_STATUS
LoadFV (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *FileName,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  EFI_HANDLE                  *AppImageHandle,
  EFI_GUID                    *CompatGuid
  )
{
  EFI_STATUS  Status;
  CHAR16      FileName16[255];
  UINT8      *Buffer;
  UINTN       BufferSize;

  UnicodeSPrint (FileName16, sizeof (FileName16), L"%a", FileName);
  Buffer = NULL;
  Status = LocateAndLoadFvFromName (FileName16, EFI_SECTION_PE32, &Buffer, &BufferSize, CompatGuid);
  if (EFI_ERROR (Status)) {
    YuzuMsg (L"Could not Locate the image from FV: %r\n", Status);
    return Status;
  }

  Status = gBS->LoadImage (FALSE, ImageHandle, NULL, Buffer, BufferSize, AppImageHandle);
  if (Buffer != NULL) {
    FreePool (Buffer);
  }
  if (Status != EFI_SUCCESS) {
    YuzuMsg (L"Could not load the image: %r\n", Status);
  } else {
    YuzuMsg (L"Loaded the image with success from FV\n");
    Status = gBS->OpenProtocol (
                    *AppImageHandle,
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **)ImageInfo,
                    ImageHandle,
                    NULL,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
  }
  return Status;
}

EFI_STATUS
FindLoadedImageFromName (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *FileName,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  EFI_GUID                    *CompatGuid
  )
{
  EFI_STATUS   Status;
  UINTN        HandleSize;
  EFI_HANDLE  *Handles;
  CHAR16       FileName16[255];
  UINTN        Index;

  UnicodeSPrint (FileName16, sizeof (FileName16), L"%a", FileName);

  HandleSize = 0;
  Handles = NULL;
  Status = gBS->LocateHandle (ByProtocol, &gEfiLoadedImageProtocolGuid, NULL, &HandleSize, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Handles = AllocateZeroPool (HandleSize);
    Status = gBS->LocateHandle (ByProtocol, &gEfiLoadedImageProtocolGuid, NULL, &HandleSize, Handles);
    YuzuMsg (L"Retrived %d Handles, with %r\n",
             HandleSize / sizeof (EFI_HANDLE), Status);
  }

  for (Index = 0; Index < HandleSize / sizeof (EFI_HANDLE); Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiLoadedImageProtocolGuid, (VOID **)ImageInfo);
    if (Status == EFI_SUCCESS) {
      CHAR16  *String = FindLoadedImageFileName (*ImageInfo);

      if (String != NULL && StrCmp (FileName16, String) == 0) {
        YuzuMsg (L"Found %s at Address 0x%X\n",
                 String, (*ImageInfo)->ImageBase);
        return EFI_SUCCESS;
      }
    }
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
LocateSectionInFvByGuid (
  EFI_GUID         *FileGuid,
  EFI_SECTION_TYPE  SectionType,
  UINT8           **Buffer,
  UINTN            *BufferSize,
  EFI_GUID         *CompatGuid
  )
{
  EFI_STATUS                     Status;
  EFI_HANDLE                    *HandleBuffer;
  UINTN                          NumberOfHandles;
  UINTN                          Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL *FvInstance;
  EFI_FV_FILETYPE                FileType;
  EFI_FV_FILE_ATTRIBUTES         FileAttributes;
  UINTN                          FileSize;
  EFI_GUID                       NameGuid;
  VOID                          *Keys;
  UINT32                         AuthenticationStatus;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  CompatGuid,
                  NULL,
                  &NumberOfHandles,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  YuzuMsg (L"Found %d Instances\n", NumberOfHandles);

  for (Index = 0; Index < NumberOfHandles; Index++) {
    Status = gBS->HandleProtocol (HandleBuffer[Index], CompatGuid, (VOID **)&FvInstance);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Keys = AllocateZeroPool (FvInstance->KeySize);
    while (TRUE) {
      FileType = EFI_FV_FILETYPE_ALL;
      ZeroMem (&NameGuid, sizeof (EFI_GUID));
      Status = FvInstance->GetNextFile (FvInstance, Keys, &FileType, &NameGuid, &FileAttributes, &FileSize);
      if (Status != EFI_SUCCESS) {
        break;
      }
      if (CompareGuid (&NameGuid, FileGuid)) {
        YuzuMsg (L"Found Guid: %g, FileSize: %d, Type: %d\n",
                 NameGuid, FileSize, FileType);
        Status = FvInstance->ReadSection (
                               FvInstance,
                               &NameGuid,
                               SectionType,
                               0,
                               (VOID **)Buffer,
                               BufferSize,
                               &AuthenticationStatus
                               );
        FreePool (Keys);
        return Status;
      }
    }
    FreePool (Keys);
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
LocateAndLoadFvFromName (
  CHAR16           *Name,
  EFI_SECTION_TYPE  SectionType,
  UINT8           **Buffer,
  UINTN            *BufferSize,
  EFI_GUID         *CompatGuid OPTIONAL
  )
{
  EFI_STATUS                     Status;
  EFI_HANDLE                    *HandleBuffer;
  UINTN                          NumberOfHandles;
  UINTN                          Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL *FvInstance;
  EFI_FV_FILETYPE                FileType;
  EFI_FV_FILE_ATTRIBUTES         FileAttributes;
  UINTN                          FileSize;
  EFI_GUID                       NameGuid;
  VOID                          *Keys;
  VOID                          *String;
  UINTN                          StringSize;
  UINT32                         AuthenticationStatus;

  if (CompatGuid == NULL) {
    CompatGuid = &gEfiFirmwareVolume2ProtocolGuid;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  CompatGuid,
                  NULL,
                  &NumberOfHandles,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  YuzuMsg (L"Found %d Instances\n", NumberOfHandles);

  for (Index = 0; Index < NumberOfHandles; Index++) {
    Status = gBS->HandleProtocol (HandleBuffer[Index], CompatGuid, (VOID **)&FvInstance);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Keys = AllocateZeroPool (FvInstance->KeySize);
    while (TRUE) {
      FileType = EFI_FV_FILETYPE_ALL;
      ZeroMem (&NameGuid, sizeof (EFI_GUID));
      Status = FvInstance->GetNextFile (FvInstance, Keys, &FileType, &NameGuid, &FileAttributes, &FileSize);
      if (Status != EFI_SUCCESS) {
        break;
      }
      String = NULL;
      Status = FvInstance->ReadSection (FvInstance, &NameGuid, EFI_SECTION_USER_INTERFACE, 0,
                                        &String, &StringSize, &AuthenticationStatus);
      if (!EFI_ERROR (Status) && String != NULL && StrCmp (Name, String) == 0) {
        YuzuMsg (L"Guid: %g, FileSize: %d, Name: %s, Type: %d\n",
                 NameGuid, FileSize, String, FileType);
        Status = FvInstance->ReadSection (FvInstance, &NameGuid, SectionType, 0,
                                          (VOID **)Buffer, BufferSize, &AuthenticationStatus);
        YuzuMsg (L"Result %r\n", Status);
        FreePool (String);
        FreePool (Keys);
        return EFI_SUCCESS;
      }
      if (String != NULL) {
        FreePool (String);
      }
    }
    FreePool (Keys);
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
FindImageInRamByGuid (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *Name,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  UINT8                        SectionType,
  EFI_GUID                    *CompatGuid
  )
{
  EFI_STATUS   Status;
  EFI_GUID     FileGuid;
  UINT8       *SectionBuf;
  UINTN        SectionSize;
  UINTN        HandleSize;
  EFI_HANDLE  *Handles;
  UINTN        Index;

  Status = YuzuStrToGuid (Name, &FileGuid);
  if (EFI_ERROR (Status)) {
    YuzuMsg (L"Failed to convert \"%a\" to GUID\n", Name);
    return Status;
  }
  YuzuLog ("GUID search: parsed %g\r\n", &FileGuid);

  //
  // First locate the section inside the firmware volumes to learn its
  // expected size, then look for a matching loaded image in RAM.
  //
  SectionBuf = NULL;
  SectionSize = 0;
  LocateSectionInFvByGuid (&FileGuid, SectionType, &SectionBuf, &SectionSize, CompatGuid);
  YuzuLog ("GUID search: BIOS section size %d\r\n", SectionSize);

  if (SectionType == EFI_SECTION_TE && SectionSize != 0) {
    YuzuMsg (L"Although the section with size: %d was found in BIOS,\n"
             L"Yuzu may not be able to find TE in RAM if Op Compatibility modifier is bad\n",
             SectionSize);
  }

  HandleSize = 0;
  Handles = NULL;
  Status = gBS->LocateHandle (ByProtocol, &gEfiLoadedImageProtocolGuid, NULL, &HandleSize, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Handles = AllocateZeroPool (HandleSize);
    Status = gBS->LocateHandle (ByProtocol, &gEfiLoadedImageProtocolGuid, NULL, &HandleSize, Handles);
    YuzuMsg (L"Retrived %d Handle, with %r\n",
             HandleSize / sizeof (EFI_HANDLE), Status);
  }

  for (Index = 0; Index < HandleSize / sizeof (EFI_HANDLE); Index++) {
    EFI_FIRMWARE_VOLUME2_PROTOCOL      *Fv;
    EFI_LOADED_IMAGE_PROTOCOL          *LoadedImage;
    UINT8                              *RamBuf;
    UINTN                              RamSize;
    UINT32                             AuthenticationStatus;
    MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  *FvFileNode;

    Status = gBS->HandleProtocol (Handles[Index], &gEfiLoadedImageProtocolGuid, (VOID **)ImageInfo);
    if (EFI_ERROR (Status)) {
      continue;
    }
    LoadedImage = *ImageInfo;
    if (LoadedImage == NULL || LoadedImage->FilePath == NULL) {
      continue;
    }
    FvFileNode = (MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)LoadedImage->FilePath;
    if (FvFileNode->Header.Type != MEDIA_DEVICE_PATH ||
        FvFileNode->Header.SubType != MEDIA_PIWG_FW_FILE_DP) {
      continue;
    }
    if (!CompareGuid (&FvFileNode->FvFileName, &FileGuid)) {
      continue;
    }

    //
    // Open the FV-style protocol selected by "Op Compatibility" on this
    // image's device handle and read the section back from the loaded copy.
    //
    Status = gBS->HandleProtocol (LoadedImage->DeviceHandle, CompatGuid, (VOID **)&Fv);
    if (EFI_ERROR (Status)) {
      continue;
    }
    RamBuf = NULL;
    RamSize = 0;
    Status = Fv->ReadSection (
                   Fv,
                   &FvFileNode->FvFileName,
                   SectionType,
                   0,
                   (VOID **)&RamBuf,
                   &RamSize,
                   &AuthenticationStatus
                   );
    if (EFI_ERROR (Status) || RamBuf == NULL) {
      continue;
    }
    if (SectionSize == 0 || RamSize == SectionSize) {
      YuzuMsg (L"Found handle of size %d at Address 0x%X\n",
               RamSize, LoadedImage->ImageBase);
      return EFI_SUCCESS;
    }
    YuzuLog ("GUID search: candidate size %d != BIOS size %d\r\n", RamSize, SectionSize);
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
LoadGuidSavePe (
  EFI_HANDLE                   ImageHandle,
  CHAR8                       *Name,
  EFI_LOADED_IMAGE_PROTOCOL  **ImageInfo,
  EFI_HANDLE                  *AppImageHandle,
  EFI_SYSTEM_TABLE            *SystemTable,
  EFI_GUID                    *CompatGuid
  )
{
  EFI_STATUS  Status;
  EFI_GUID    FileGuid;
  UINT8      *Buffer;
  UINTN       BufferSize;

  Status = YuzuStrToGuid (Name, &FileGuid);
  if (EFI_ERROR (Status)) {
    YuzuMsg (L"Failed to convert \"%a\" to GUID\n", Name);
    return Status;
  }

  Buffer = NULL;
  BufferSize = 0;
  Status = LocateSectionInFvByGuid (&FileGuid, EFI_SECTION_PE32, &Buffer, &BufferSize, CompatGuid);
  YuzuMsg (L"Section search result: %r\n", Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  YuzuMsg (L"Section Base and Size: 0x%x / 0x%x\n", Buffer, BufferSize);

  DumpSectionToFs (SystemTable, Name, Buffer, BufferSize);

  Status = gBS->LoadImage (FALSE, ImageHandle, NULL, Buffer, BufferSize, AppImageHandle);
  if (EFI_ERROR (Status)) {
    YuzuMsg (L"Could not load the image: %r\n", Status);
    FreePool (Buffer);
    return Status;
  }
  Status = gBS->OpenProtocol (
                  *AppImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)ImageInfo,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  return Status;
}

EFI_STATUS
LoadGuidSaveFreeform (
  EFI_HANDLE        ImageHandle,
  CHAR8            *FileGuidStr,
  UINT8           **SectionBase,
  UINTN            *SectionSize,
  CHAR8            *SectionGuidStr,
  EFI_SYSTEM_TABLE *SystemTable,
  EFI_GUID         *CompatGuid
  )
{
  EFI_STATUS                     Status;
  EFI_GUID                       FileGuid;
  EFI_GUID                       SectionGuid;
  EFI_HANDLE                    *HandleBuffer;
  UINTN                          NumberOfHandles;
  UINTN                          Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv;
  EFI_FV_FILETYPE                FileType;
  EFI_FV_FILE_ATTRIBUTES         FileAttributes;
  UINTN                          FileSize;
  EFI_GUID                       NameGuid;
  VOID                          *Keys;
  UINT32                         AuthenticationStatus;

  YuzuMsg (L"Going to search with EfiFirmwareVolume2Protocol\n");

  Status = YuzuStrToGuid (FileGuidStr, &FileGuid);
  if (EFI_ERROR (Status)) {
    YuzuMsg (L"Failed to convert \"%a\" to File GUID\n", FileGuidStr);
    return Status;
  }

  if (SectionGuidStr != NULL) {
    Status = YuzuStrToGuid (SectionGuidStr, &SectionGuid);
    if (EFI_ERROR (Status)) {
      YuzuMsg (L"Failed to convert \"%a\" to Section Guid\n", SectionGuidStr);
      return Status;
    }
  }

  Status = gBS->LocateHandleBuffer (ByProtocol, CompatGuid, NULL, &NumberOfHandles, &HandleBuffer);
  if (EFI_ERROR (Status)) {
    YuzuMsg (L"Could not find any handle\n");
    return Status;
  }
  YuzuMsg (L"Found %d Handle Instances\n", NumberOfHandles);

  for (Index = 0; Index < NumberOfHandles; Index++) {
    Status = gBS->HandleProtocol (HandleBuffer[Index], CompatGuid, (VOID **)&Fv);
    if (EFI_ERROR (Status)) {
      continue;
    }
    YuzuMsg (L"Trying to retrieve FREEFORM section from file\n");
    Keys = AllocateZeroPool (Fv->KeySize);
    while (TRUE) {
      FileType = EFI_FV_FILETYPE_ALL;
      ZeroMem (&NameGuid, sizeof (EFI_GUID));
      Status = Fv->GetNextFile (Fv, Keys, &FileType, &NameGuid, &FileAttributes, &FileSize);
      if (Status != EFI_SUCCESS) {
        break;
      }
      if (!CompareGuid (&NameGuid, &FileGuid)) {
        continue;
      }
      if (SectionGuidStr != NULL) {
        Status = Fv->ReadSection (Fv, &NameGuid, EFI_SECTION_FREEFORM_SUBTYPE_GUID, 0,
                                  (VOID **)SectionBase, SectionSize, &AuthenticationStatus);
      } else {
        //
        // No freeform subtype GUID given - fall back to the RAW section.
        //
        Status = Fv->ReadSection (Fv, &NameGuid, EFI_SECTION_RAW, 0,
                                  (VOID **)SectionBase, SectionSize, &AuthenticationStatus);
      }
      if (!EFI_ERROR (Status)) {
        YuzuMsg (L"Found\n");
        FreePool (Keys);
        DumpSectionToFs (SystemTable, FileGuidStr, *SectionBase, *SectionSize);
        return EFI_SUCCESS;
      }
    }
    FreePool (Keys);
  }
  return EFI_NOT_FOUND;
}

EFI_STATUS
Exec (
  EFI_HANDLE  *AppImageHandle
  )
{
  UINTN       ExitDataSize;
  EFI_STATUS  Status;

  //
  // Give the user three seconds to read the log output first.
  //
  gBS->Stall (3 * 1000 * 1000);
  Status = gBS->StartImage (*AppImageHandle, &ExitDataSize, NULL);
  YuzuMsg (L"Image Retuned - %r %x\n", Status, Status);
  return Status;
}

EFI_STATUS
UninstallProtocol (
  CHAR8  *GuidStr
  )
{
  EFI_STATUS   Status;
  EFI_GUID     Guid;
  UINTN        HandleSize;
  EFI_HANDLE  *Handles;
  UINTN        Index;
  UINTN        Count;

  Status = YuzuStrToGuid (GuidStr, &Guid);
  if (EFI_ERROR (Status)) {
    YuzuMsg (L"Failed to convert \"%a\" to GUID\n", GuidStr);
    return Status;
  }

  HandleSize = 0;
  Handles = NULL;
  Status = gBS->LocateHandle (ByProtocol, &Guid, NULL, &HandleSize, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Handles = AllocateZeroPool (HandleSize);
    Status = gBS->LocateHandle (ByProtocol, &Guid, NULL, &HandleSize, Handles);
    YuzuMsg (L"Retrived %d Handles, with %r\n",
             HandleSize / sizeof (EFI_HANDLE), Status);
  }

  Count = 0;
  for (Index = 0; Index < HandleSize / sizeof (EFI_HANDLE); Index++) {
    VOID  *Interface;

    Status = gBS->HandleProtocol (Handles[Index], &Guid, &Interface);
    if (EFI_ERROR (Status)) {
      YuzuMsg (L"Failed to retrieve handle %d's protocol interface: %r\n", Index, Status);
      continue;
    }
    Status = gBS->UninstallProtocolInterface (Handles[Index], &Guid, Interface);
    if (EFI_ERROR (Status)) {
      YuzuMsg (L"%r - Could not uninstall protocol: %g, from handle %d\n", Status, Guid, Index);
    } else {
      Count++;
    }
  }
  YuzuMsg (L"Protocol uninstalled from %d handles\n", Count);
  return EFI_SUCCESS;
}

EFI_STATUS
DumpSectionToFs (
  EFI_SYSTEM_TABLE  *SystemTable,
  CHAR8             *Name,
  UINT8             *Buffer,
  UINTN              BufferSize
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                      *SFS_Handles;
  UINTN                            NumHandles;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE                        *Root;
  EFI_FILE                        *OutFile;
  CHAR16                           FileName[255];

  YuzuMsg (L"Creating section dump on FS,\n"
           L"this shouldn't take more than 3 secs.\nOtherwise, UEFI system unsupported.\n");

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
    Status = gBS->HandleProtocol (SFS_Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FileSystem);
    if (EFI_ERROR (Status)) {
      continue;
    }
    Status = FileSystem->OpenVolume (FileSystem, &Root);
    if (EFI_ERROR (Status)) {
      continue;
    }
    UnicodeSPrint (FileName, sizeof (FileName), L"%a.bin", Name);
    Status = Root->Open (Root, &OutFile, FileName,
                         EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR (Status)) {
      continue;
    }
    OutFile->Write (OutFile, &BufferSize, Buffer);
    OutFile->Flush (OutFile);
    OutFile->Close (OutFile);
    return EFI_SUCCESS;
  }
  return EFI_NOT_FOUND;
}
