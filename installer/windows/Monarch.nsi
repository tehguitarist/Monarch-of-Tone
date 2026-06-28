; Monarch of Tone VST3 installer (NSIS). Windows has no AU format, so there's no plugin-type
; choice here — VST3 only, installed to the shared system VST3 folder.
;
; Build with (from repo root, after building Monarch_VST3):
;   makensis /DVERSION=0.8.0 /DARTEFACTS_DIR=build\Monarch_artefacts\Release\VST3 installer\windows\Monarch.nsi
;
; ARTEFACTS_DIR should point at the directory CONTAINING "Monarch of Tone.vst3" (i.e. the VST3
; release output folder), not the .vst3 bundle itself.

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef ARTEFACTS_DIR
  !define ARTEFACTS_DIR "..\..\build\Monarch_artefacts\Release\VST3"
!endif

Name "Monarch of Tone"
OutFile "Monarch-of-Tone-${VERSION}-Windows-Installer.exe"
InstallDir "$COMMONFILES64\VST3"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Monarch of Tone VST3 Plugin" SecVST3
    SetOutPath "$INSTDIR\Monarch of Tone.vst3"
    File /r "${ARTEFACTS_DIR}\Monarch of Tone.vst3\*.*"

    WriteUninstaller "$INSTDIR\Monarch of Tone.vst3\Uninstall-MonarchOfTone.exe"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonarchOfTone" \
        "DisplayName" "Monarch of Tone VST3"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonarchOfTone" \
        "UninstallString" "$INSTDIR\Monarch of Tone.vst3\Uninstall-MonarchOfTone.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonarchOfTone" \
        "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonarchOfTone" \
        "Publisher" "Leigh Pierce"
SectionEnd

Section "Uninstall"
    RMDir /r "$INSTDIR\Monarch of Tone.vst3"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonarchOfTone"
SectionEnd
