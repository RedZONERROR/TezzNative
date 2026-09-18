[Defines]
  PLATFORM_NAME           = TezzKernel
  PLATFORM_GUID           = 36C62221-9E22-4B6D-8C4E-2F9E1C7D7B55
  PLATFORM_VERSION        = 0.1
  DSC_SPECIFICATION       = 0x0001001B
  OUTPUT_DIRECTORY        = Build/TezzKernel
  SUPPORTED_ARCHITECTURES = X64
  BUILD_TARGETS           = RELEASE
  SKUID_IDENTIFIER        = DEFAULT

[LibraryClasses]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf

[Components]
  TezzKernel/TezzKernel.inf
