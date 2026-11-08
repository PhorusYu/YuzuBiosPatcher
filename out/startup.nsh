@echo -off
cls

for %i run (0 5)
  fs%i:
  if exist "YuzuBiosPatcher.efi" then
    YuzuBiosPatcher.efi
  endif
endfor

echo.
echo YuzuBiosPatcher startup script finished. The shell stays open.
echo If nothing printed above, the .efi was not found on fs0:-fs5:.
pause
