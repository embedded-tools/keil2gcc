It converts Keil uVision project file to GCC makefile.

This utility goes through Keil project file and creates:<br />
  -makefile with list of all source files included into project
  -it sets proper C/C++ compiler flags that fit to selected MCU
  -it inserts clean command + install command to makefile
  -it generates JLink script for installing compiled firmware to MCU (see makefile install command)
  -it generates *.ld file (if you don't have any), but is always better to find original LD file from your MCU manufacturer
  -it generates *.s  file (if you don't have any), but is always better to find original startup file from your MCU manufacturer

This utility is also demostrating how easy is to do such things with PersistantLibrary (see https://github.com/embedded-tools/PersistenceLibrary )

  
