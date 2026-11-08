/** @file
  Hello.c - minimal diagnostic image built with the same toolchain.

  If this file starts and prints text on your machine, the toolchain and
  PE profile are fine and the issue is elsewhere (config / files on USB).
  It replaces the YuzuEntry symbol that MiniUefi.c's entry shim calls.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

EFI_STATUS
EFIAPI
Crumb (
  IN CONST CHAR8  *Msg
  );

EFI_STATUS
EFIAPI
YuzuEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  Crumb ("[2] hello: YuzuEntry entered\r\n");
  Print (L"\n");
  Print (L"  Hello from YuzuBiosPatcher toolchain!\n");
  Print (L"  If you can read this, EFI images built here load and run correctly.\n");
  Print (L"  Staying on screen for 5 seconds...\n");
  Crumb ("[3] hello: prints done\r\n");
  gBS->Stall (5 * 1000 * 1000);
  Crumb ("[4] hello: done, returning\r\n");
  return EFI_SUCCESS;
}
