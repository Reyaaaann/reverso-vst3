; Inno Setup script - builds Reverso-Setup.exe (installs the VST3 for FL Studio and other hosts)
#ifndef VstDir
  #define VstDir "..\build\Reverso_artefacts\Release\VST3\Reverso.vst3"
#endif

[Setup]
AppId={{6D1E4C52-9B7A-4F0C-8A53-2E7B1C9D4A11}
AppName=Reverso (VST3)
AppVersion=1.0.0
AppPublisher=Reverso Audio
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=Reverso-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#VstDir}\*"; DestDir: "{commoncf64}\VST3\Reverso.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[Messages]
FinishedLabel=Reverso was installed. In FL Studio: Options > Manage plugins > Find installed plugins, then add it from a mixer effect slot.
