; Build: makensis /DStageDir="..." /DOutFile="..." [/DGitShort="..."] ernc-studio.nsi

!include "MUI2.nsh"

!ifndef StageDir
  !error "Define StageDir via /DStageDir=..."
!endif
!ifndef OutFile
  !define OutFile "ERNC-Studio-setup.exe"
!endif
!ifndef GitShort
  !define GitShort "dev"
!endif

Name "ERNC Studio"
OutFile "${OutFile}"
InstallDir "$PROGRAMFILES64\ERNC Studio"
InstallDirRegKey HKCU "Software\ERNC Studio" ""
RequestExecutionLevel admin
Unicode true

VIProductVersion 0.1.0.0
VIAddVersionKey "ProductName" "ERNC Studio"
VIAddVersionKey "FileDescription" "ERNC Studio 安装程序"
VIAddVersionKey "FileVersion" "${GitShort}"
VIAddVersionKey "ProductVersion" "0.1.0-${GitShort}"

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"

Section "主程序" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${StageDir}\*.*"

  CreateDirectory "$SMPROGRAMS\ERNC Studio"
  CreateShortcut "$SMPROGRAMS\ERNC Studio\ERNC Studio.lnk" "$INSTDIR\ernc-studio.exe" "" "$INSTDIR\ernc-studio.exe" 0
  CreateShortcut "$SMPROGRAMS\ERNC Studio\卸载 ERNC Studio.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\ERNC Studio.lnk" "$INSTDIR\ernc-studio.exe" "" "$INSTDIR\ernc-studio.exe" 0

  WriteRegStr HKCU "Software\ERNC Studio" "" $INSTDIR
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ERNCStudio" "DisplayName" "ERNC Studio"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ERNCStudio" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ERNCStudio" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ERNCStudio" "DisplayVersion" "0.1.0-${GitShort}"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ERNCStudio" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ERNCStudio" "NoRepair" 1

  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\ERNC Studio.lnk"
  Delete "$SMPROGRAMS\ERNC Studio\ERNC Studio.lnk"
  Delete "$SMPROGRAMS\ERNC Studio\卸载 ERNC Studio.lnk"
  RMDir "$SMPROGRAMS\ERNC Studio"

  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "Software\ERNC Studio"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ERNCStudio"
SectionEnd
