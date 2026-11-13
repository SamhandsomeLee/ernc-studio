; Build: makensis /DStageDir="..." /DOutFile="..." ernc-studio.nsi

!include "MUI2.nsh"

!ifndef StageDir
  !error "Define StageDir via /DStageDir=..."
!endif
!ifndef OutFile
  !define OutFile "ERNC-Studio-setup.exe"
!endif

Name "ERNC Studio"
OutFile "${OutFile}"
InstallDir "$PROGRAMFILES64\ERNC Studio"
RequestExecutionLevel user
Unicode true

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Section "ERNC Studio" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${StageDir}\*"
  CreateShortcut "$DESKTOP\ERNC Studio.lnk" "$INSTDIR\ernc-studio.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\ERNC Studio.lnk"
  RMDir /r "$INSTDIR"
SectionEnd
