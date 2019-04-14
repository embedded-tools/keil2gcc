It converts Keil uVision project file to GCC makefile.<br />
<br />
This utility goes through Keil project file and creates:<br />
  -makefile with list of all source files included into project<br />
  -it sets proper C/C++ compiler flags that fit to selected MCU<br />
  -it inserts clean command + install command to makefile<br />
  -it generates JLink script for installing compiled firmware to MCU (see makefile install command)<br />
  -it generates *.ld file (if you don't have any), but is always better to find original LD file from your MCU manufacturer<br />
  -it generates *.s  file (if you don't have any), but is always better to find original startup file from your MCU manufacturer<br />
<br />

Supported MCU types:

-STM32F0xx
-STM32F1xx
-STM32F3xx
-STM32F4xx
-STM32F7xx
-NXP LCP17x00

Conversion does not work well if another MCU type is used.

This utility is also demostrating how easy is to do such things with PersistantLibrary (see https://github.com/embedded-tools/PersistenceLibrary )<br />

  

