# pefix.ps1 - normalize a PE32+ image to the classic EDK2 EFI profile:
#   * Import Directory      -> 0    (empty import tables upset strict loaders)
#   * PE Checksum           -> 0    (EDK2 convention: 0 = do not verify)
#   * COFF symbol table     -> 0    (debug symbols never appear in EFI images)
#   * trailing symbol data  -> cut  (file ends at the last section's raw data)
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File pefix.ps1 <file.efi>

param([Parameter(Mandatory=$true)][string]$Path)

$b = [IO.File]::ReadAllBytes($Path)

# PE header offsets
$e    = [BitConverter]::ToUInt32($b, 0x3C)   # e_lfanew
$coff = $e + 4
$opt  = $e + 24                              # optional header (PE32+)
$dd   = $opt + 112                           # data directories

# Zero DataDirectory[1] (Import)
for ($i = 0; $i -lt 8; $i++) { $b[$dd + 8 + $i] = 0 }

# Zero CheckSum (optional header +64)
for ($i = 0; $i -lt 4; $i++) { $b[$opt + 64 + $i] = 0 }

# Zero COFF symbol table pointer and count
for ($i = 0; $i -lt 8; $i++) { $b[$coff + 8 + $i] = 0 }

# Truncate at the end of the last section's raw data
$nsec    = [BitConverter]::ToUInt16($b, $coff + 2)
$optsz   = [BitConverter]::ToUInt16($b, $coff + 16)
$sect    = $coff + 20 + $optsz
$end     = 0
for ($i = 0; $i -lt $nsec; $i++) {
  $rawsz = [BitConverter]::ToUInt32($b, $sect + $i * 40 + 16)
  $raw   = [BitConverter]::ToUInt32($b, $sect + $i * 40 + 20)
  if ($raw + $rawsz -gt $end) { $end = $raw + $rawsz }
}
if ($end -gt 0 -and $end -lt $b.Length) {
  $b = $b[0..($end - 1)]
}

[IO.File]::WriteAllBytes($Path, $b)
Write-Host ("pefix: " + $Path + " normalized, final size " + $b.Length)
