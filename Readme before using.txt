Converts Keil MDK-ARM 4 project (generated by STMCube) to ARM GCC makefile.
Supports ARM Cortex MCUs (M0, M3, M4) by ST and NXP.
Tested with:
  NXP LCP11C24
  ST  STM32F010  
  ST  STM32F103
  ST  STM32F207
  ST  STM32F429

Usage: keil2gcc uv4_project_file [-soft] [-scanlibs] [makefile_name]

Options:
    -soft            Force using software emulated FPU
    -scanlibs        Scans subdirectories for not included source codes and headers
    makefile_name    Optional, default value is '.\makefile'

Example:

Keil2GCC.exe d:\YourKeilProject\YourProject.uvproj
Keil2GCC.exe d:\YourSTMCubeProject\MDK-ARM\TestF103.uvproj ..\makefile
