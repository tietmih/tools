; Bitstream Inspector - NSIS installer
; Per-user install, no admin rights. Built from Linux with makensis 3.10.
;
; Inputs (all optional, passed by build_installer.sh):
;   APP_VERSION  e.g. 0.2.0
;   OUT_FILE     absolute path of the installer .exe
;   APP_ICON     absolute path to res/app.ico
;   STAGE_DIR    absolute path to the staged files (exe, DLLs, licenses)

!ifndef APP_VERSION
  !define APP_VERSION "0.2.0"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "..\dist\BitstreamInspector-${APP_VERSION}-win64-setup.exe"
!endif
!ifndef APP_ICON
  !define APP_ICON "..\res\app.ico"
!endif
!ifndef STAGE_DIR
  !define STAGE_DIR "staging"
!endif

!define APP_NAME "Bitstream Inspector"
!define APP_PUBLISHER "tietmih"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\BitstreamInspector"

Name "${APP_NAME}"
Caption "${APP_NAME} ${APP_VERSION} Setup"
OutFile "${OUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\BitstreamInspector"
InstallDirRegKey HKCU "Software\BitstreamInspector" "InstallLocation"
RequestExecutionLevel user
Unicode true
SetCompressor /SOLID lzma
SetCompressorDictSize 32
BrandingText "${APP_NAME} ${APP_VERSION}"

VIProductVersion "0.2.0.0"
VIAddVersionKey /LANG=1033 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_NAME} installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1033 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (c) 2026 ${APP_PUBLISHER}"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\bsinspector.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${APP_NAME}"

!define MUI_LICENSEPAGE_TEXT_TOP "Bitstream Inspector is MIT licensed. The optional media layer uses FFmpeg under the LGPL v2.1."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "license_page.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "${APP_NAME} requires 64-bit Windows."
    Abort
  ${EndIf}
  SetShellVarContext current
FunctionEnd

Function un.onInit
  SetShellVarContext current
FunctionEnd

Section "Bitstream Inspector (GUI)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${STAGE_DIR}/bsinspector.exe"
  File "${STAGE_DIR}/avcodec-61.dll"
  File "${STAGE_DIR}/avformat-61.dll"
  File "${STAGE_DIR}/avutil-59.dll"
  File "${STAGE_DIR}/swscale-8.dll"
  File "${STAGE_DIR}/README.md"

  SetOutPath "$INSTDIR\LICENSES"
  File "${STAGE_DIR}/LICENSES/MIT.txt"
  File "${STAGE_DIR}/LICENSES/FFmpeg-LGPLv2.1.txt"
  File "${STAGE_DIR}/LICENSES/FFmpeg-BUILD-INFO.txt"

  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\Bitstream Inspector"
  CreateShortCut "$SMPROGRAMS\Bitstream Inspector\Bitstream Inspector.lnk" \
                 "$INSTDIR\bsinspector.exe" "" "$INSTDIR\bsinspector.exe" 0
  CreateShortCut "$SMPROGRAMS\Bitstream Inspector\Uninstall.lnk" \
                 "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\BitstreamInspector" "InstallLocation" "$INSTDIR"

  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\bsinspector.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" 6800

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\bsinspector.exe" "" "$INSTDIR\bsinspector.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\bsinspector.exe" "Path" "$INSTDIR"
SectionEnd

Section "Command-line tools (bsinfo)" SecCLI
  SetOutPath "$INSTDIR\tools"
  File "${STAGE_DIR}/bsinfo.exe"
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortCut "$DESKTOP\Bitstream Inspector.lnk" \
                 "$INSTDIR\bsinspector.exe" "" "$INSTDIR\bsinspector.exe" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "The analyzer GUI, FFmpeg media layer and license files."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCLI} "Command-line parser: bsinfo.exe (no FFmpeg DLLs required)."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create a shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  ; The main executable may still be mapped by a running instance. Ask the
  ; user to close it and retry; silent uninstalls get one deferred retry.
  IfSilent uninstall_silent
  uninstall_retry:
    ClearErrors
    Delete "$INSTDIR\bsinspector.exe"
    IfFileExists "$INSTDIR\bsinspector.exe" 0 uninstall_continue
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
               "Bitstream Inspector is running. Close it and click Retry." \
               IDRETRY uninstall_retry
    Abort
  uninstall_silent:
    Delete "$INSTDIR\bsinspector.exe"
    IfFileExists "$INSTDIR\bsinspector.exe" 0 uninstall_continue
    Sleep 1500
    Delete "$INSTDIR\bsinspector.exe"
  uninstall_continue:

  Delete "$INSTDIR\avcodec-61.dll"
  Delete "$INSTDIR\avformat-61.dll"
  Delete "$INSTDIR\avutil-59.dll"
  Delete "$INSTDIR\swscale-8.dll"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR\LICENSES"

  Delete "$INSTDIR\tools\bsinfo.exe"
  RMDir "$INSTDIR\tools"

  Delete "$SMPROGRAMS\Bitstream Inspector\Bitstream Inspector.lnk"
  Delete "$SMPROGRAMS\Bitstream Inspector\Uninstall.lnk"
  RMDir "$SMPROGRAMS\Bitstream Inspector"
  Delete "$DESKTOP\Bitstream Inspector.lnk"

  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\bsinspector.exe"
  DeleteRegKey HKCU "Software\BitstreamInspector"

  IfSilent keep_settings
  MessageBox MB_YESNO|MB_ICONQUESTION "Remove your settings and window layout as well?" IDNO keep_settings
  RMDir /r "$APPDATA\BitstreamInspector"
  keep_settings:
  RMDir "$INSTDIR"
SectionEnd
