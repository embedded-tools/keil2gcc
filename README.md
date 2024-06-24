It converts Keil uVision project file to GCC makefile. It also can convert STMCube project to GCC makefile - you just need to select target platform (in STM Cube) to Keil MDK 4 and then convert keil project to GCC.<br />
<br />
This utility goes through Keil project file and creates:<br />
  -makefile with list of all source files included into project<br />
  -it sets proper C/C++ compiler flags that fit to selected MCU<br />
  -it inserts clean command + install command to makefile<br />
  -it generates JLink script for installing compiled firmware to MCU (see makefile install command)<br />
  -it generates *.ld file<br />
  -it generates *.s  file<br />
  -*.s file contains some additional instructions to make sure that stack pointer is always set to correct value
  (even after calling entry point by boot loader)
<br />

It has been tested with these MCU types:<br />
<br />
-STM 32F010<br />
-STM 32F103<br />
-STM 32F205<br />
-STM 32F207<br />
-STM 32F429<br />
-NXP LCP11C24<br />
<br />
For these MCUs is generated correct S (startup) file and LD file. For other MCUs can happen that generated S file or LD file is not 100% correct (although I hope it will not happen). In such case try to get original S file or LD file from your MCU manufacter and you can send me the correct file.<br />
<br />
This utility is also demostrating how easy is to do such things with PersistantLibrary (see https://github.com/embedded-tools/PersistenceLibrary )<br />
<br />
Binary files:<br />
<br />
1.03 - not available anymore
1.04 - https://keil2gcc.bbot.cz/file/keil2gcc_1.04.zip  (added -scanlib feature)<br />
1.05 - https://keil2gcc.bbot.cz/file/keil2gcc_1.05.zip  (support for Keil v5 added)<br />
1.06 - https://keil2gcc.bbot.cz/file/keil2gcc_1.06.zip  (minor bug fixes)<br />
1.07 - https://keil2gcc.bbot.cz/file/keil2gcc_1.07.zip  (checks for presence of original gcc startup file by manufacturer - tested with some waveshare dev kits)<br />
  

