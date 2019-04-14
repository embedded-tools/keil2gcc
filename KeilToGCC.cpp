// Keil2EclipseConvertor.cpp : Defines the entry point for the console application.
//
#include "TStringList.h"
#include "TFilePath.h"
#include "KeilToARMGCC.h"
#include "TString.h"


int main(int argc, char* argv[])
{
	if (argc<=1)
	{
		printf("\r\nKeil MDK to ARM GCC makefile converter v1.06\r\n");
		printf("(c) 2018 Ondrej Sterba, osterba@atlas.cz \r\n");
		printf("\r\n");
		printf("Usage: keil2gcc uv_project_file [-soft] [-scanlibs] [makefile_name]\r\n");
        printf("\r\n");
		printf("Options:\r\n");		
		printf("    -soft            Force using software emulated FPU\r\n");
        printf("    -scanlibs        Scans subdirectories for source codes not included in project file\r\n");
		printf("    makefile_name    Optional, default value is '.\\makefile'\r\n");
		printf("\r\n");
		return -1;
	}

	bool forceSoftFPU = false;
    bool scanLibs = false;

	TString uv4ProjectFile = argv[1];
	TFilePath targetFile = "./";
	if (argc>=3) 
	{
		TString lastArg = argv[argc-1];
		if (lastArg.Length()>0)
		{
			if (lastArg[0]!='-')
			{
				targetFile = lastArg;
			}
		}
	}
	targetFile.ChangeSeparator('/');
	for(int i = 1; i<argc; i++)
	{
		if (strcmp(argv[i], "-soft")==0)
		{
			forceSoftFPU = true;
		}
        if (strcmp(argv[i], "-scanlibs")==0)
        {
            scanLibs = true;
        }
	}
    
    KeilToARMGCC conv;
    conv.DoConversion(uv4ProjectFile, 0, targetFile, forceSoftFPU, scanLibs);

	return 0;
}
