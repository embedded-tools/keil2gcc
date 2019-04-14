#include "KeilToARMGCC.h"
#include "TFilePath.h"
#include "StringUtils.h"
#include "TTextFile.h"
#include "TXmlDoc.h"
#include "TXmlTagDynamicPool.h"
#include "TRandom.h"
#include <windows.h>
#include <stdio.h>

KeilToARMGCC::KeilToARMGCC()
{
    m_counter = 0;
    m_stackSize = 0;
    m_heapSize = 0;
    m_useFPU = false;
    m_fpuVersion = 0;
    m_fileHandle = 0;
    m_state = kcsVectors;	
}

void KeilToARMGCC::SetState (KeilConversionState state)
{
    m_state = state;
}

void KeilToARMGCC::ScanLibs(const char* makefilePath, const char* relativePath)
{
    WIN32_FIND_DATAA findData;
    HANDLE hFindHandle;
    char filter [512];
    char subpath[512];

    sprintf(filter, "%s%s*.*", makefilePath, relativePath+2);  

    hFindHandle = FindFirstFileA(filter, &findData);
    if (hFindHandle==INVALID_HANDLE_VALUE)
    {
        return;
    }

    TFilePath filename;
    TFilePath fileExt;
    TFilePath filedir;
    while(true)
    {            

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (findData.cFileName[0]!='.')
            {   
                if (relativePath==NULL)
                {
                    sprintf(subpath, "%s\\", findData.cFileName );
                } else {
                    sprintf(subpath, "%s%s\\", relativePath, findData.cFileName );
                }                
                printf("  %s\r\n", subpath);

                ScanLibs(makefilePath, subpath);
            }
        } else 
        {
            filename  = relativePath;
            filename += findData.cFileName;
            filename.ChangeSeparator('/');
            filedir   = filename.ExtractFileDirectory();
            fileExt   = filename.ExtractFileExt();
            if (filedir.LastChar()=='/')
            {
                filedir.SetLength(filedir.Length()-1);
            }
            if ((fileExt==".c") || (fileExt==".cpp"))
            {                
                printf("    %s\r\n", filename);
                if (m_srcList.IndexOf(filename)==-1)
                {
                    m_srcList.Add(filename);
                }
            }            
            if (fileExt==".h")
            {
                printf("    %s\r\n", filename);
                if (m_incList.IndexOf(filedir)==-1)
                {
                    m_incList.Add(filedir);
                }
            }            
        }        
        if (!FindNextFileA(hFindHandle, &findData)) break;        
    }
    FindClose(hFindHandle);
}

void KeilToARMGCC::DoConversion(const char*  uv4ProjectFile,
                                int         keilTargetIndex,
                                const char* targetDirectory, 
                                bool        forceSoftFPU,
                                bool        scanlibs)
{
    bool res;
    bool noErrors = true;

    TFilePath projectFile, projectPath;
    projectFile.SetLength(512);
    GetFullPathNameA(uv4ProjectFile, 512, (LPSTR)projectFile.ToPChar(), NULL);
    projectFile.SetLength(strlen(projectFile.ToPChar()));

    projectPath = projectFile;
    projectPath.ChangeFileName("");

    TFilePath targetFile = projectPath + targetDirectory;
    TFilePath targetPath = targetFile.ExtractFileDirectory();

    res = ParseKeilProjectSettings(projectFile, keilTargetIndex, targetDirectory, forceSoftFPU);
    if (!res) 
    {
        printf("\r\nKeil project parsing failed\r\n");
        return;	
    }
    if (m_stackSize==0) m_stackSize = 512;
    if (m_heapSize==0) m_heapSize = 512;

    if (scanlibs)
    {
        printf("\r\nSearching for source codes in directories:\r\n");
        printf("  .\\\r\n");
        ScanLibs(targetPath, ".\\");
        printf("All files added to makefile file list.\r\n");
    }
    m_incList.Sort();
    m_srcList.Sort();

    TString deviceName = m_cpuType;
    deviceName.LowerCase();
    for(int i = 0; i<deviceName.Length(); i++)
    {
        if ((deviceName[i]>='a')&&(deviceName[i]<='z')) continue;
        if ((deviceName[i]>='A')&&(deviceName[i]<='Z')) continue;
        if ((deviceName[i]>='0')&&(deviceName[i]<='9')) continue;
        deviceName[i]='_';
    }

    m_startupFile = "gcc_startupfile_";
    m_startupFile += deviceName;	
    m_startupFile += ".s";

    res = CreateStartupFile(targetPath + m_startupFile);
    if (!res)
    {
        printf("\r\nStartup file creating failed\r\n");
        noErrors = false;
    }

    m_ldScriptFile  = "gcc_linkerfile_";
    m_ldScriptFile += deviceName;
    m_ldScriptFile += ".ld";

    res = CreateLDScript(targetPath + m_ldScriptFile);
    if (!res)
    {
        printf("\r\nLD file creating failed\r\n");
        noErrors = false;
    }

    res = CreateJLinkFile(targetPath + "flash.jlink");
    if (!res)
    {
        printf("\r\nJLink file not created\r\n");
        noErrors = false;
    }

    res = CreateMakeFile(targetPath + "makefile");
    if (!res)
    {
        printf("\r\nMakefile not created\r\n");
        noErrors = false;
    }
    if (noErrors)
    {
        printf("\r\nARMGCC makefile created successfully.\r\n");
    } else {
        printf("\r\nARMGCC makefile created with errors, errors need to be fixed manually.\r\n");
    }	
}


bool KeilToARMGCC::ParseKeilProjectSettings(const char* keilProjectFile, int keilTargetIndex, const char* targetFile, bool forceSoftFPU)
{	
    int i1, i2, i3;

    TTextFile textfile;
    TString   line;

    TXMLTagDynamicPool xmlPool;
    TXMLDoc keilProjectXml;
    keilProjectXml.SetPool(&xmlPool);

    TFilePath targetPath = targetFile;   
    targetPath = targetPath.ExtractFileDirectory();
    targetPath.ChangeSeparator('/');

    bool res = keilProjectXml.LoadFromFile(keilProjectFile);
    if (!res) 
    {
        printf("Project file can not be loaded.\r\n");
        return false;
    }

    TXMLTag* header = keilProjectXml.Header();


    TXMLTag* root = keilProjectXml.Root();
    if (root==NULL) 
    {
        printf("Project file has no xml root.\r\n");
        return false;
    }

    TXMLTag* targets = root->SelectNode("Targets");
    if (targets==NULL)
    {
        printf("Embedded target not found\r\n");
        return false;
    }

    TXMLTagList* targetList = targets->SelectNodes("Target");

    TXMLTag* target = targetList->First();
    if (target==NULL)
    {
        printf("Embedded target not found\r\n");
        return false;
    }

    TXMLTag*  targetNameTag = target->SelectNode("TargetName");
    if (targetNameTag)
    {
        m_targetName = targetNameTag->GetValue();
    } else {
        m_targetName = "NotNamed";
    }

    for(int i = 0; i<m_targetName.Length(); i++)
    {
        if ((m_targetName[i]>='a') && (m_targetName[i]<='z')) continue;
        if ((m_targetName[i]>='A') && (m_targetName[i]<='Z')) continue;
        if ((m_targetName[i]>='0') && (m_targetName[i]<='9')) continue;
        m_targetName[i] = '_';
    }

    //include paths
    TXMLTag* variousControlsTag =  target->SelectNode("TargetOption/TargetArmAds/Cads/VariousControls");
    if (variousControlsTag)
    {
        int separatorPosition = 0;
        TXMLTag* defineTag = variousControlsTag->SelectNode("Define");
        if (defineTag)
        {			
            TString defines = defineTag->GetValue();
            TString define;
            while(true)
            {
                separatorPosition = defines.IndexOf(',');
                if (separatorPosition==-1)
                {
                    separatorPosition = defines.IndexOf(';');
                    if (separatorPosition==-1)
                    {
                        separatorPosition = defines.IndexOf(' ');
                    }
                }
                if (separatorPosition>=0)
                {
                    define.CopyFrom(defines.ToPChar(), separatorPosition);
                    define.Trim();
                    m_defineList.Add(define);
                    defines.Delete(0, separatorPosition+1);
                } else break;
            }	
            if (defines.Length()>0)
            {
                defines.Trim();
                m_defineList.Add(defines);
            }
        }		
        separatorPosition = 0;
        TXMLTag* includeTag = variousControlsTag->SelectNode("IncludePath");

        TFilePath target = targetFile;	
        target.ChangeSeparator('/');
        if (includeTag)
        {
            TString    includes = includeTag->GetValue();
            TString    include;
            TString    tmp;
            while(true)
            {	
                separatorPosition = includes.IndexOf(';');
                if (separatorPosition>=0)
                {
                    include.CopyFrom(includes.ToPChar(), separatorPosition);
                    include.Trim();
                    tmp = targetPath + include;
                    if ((tmp.LastChar()=='\\') || (tmp.LastChar()=='/'))
                    {
                        tmp.SetLength(tmp.Length()-1);
                    }
                    int n = tmp.IndexOf("portable/RVDS");
                    if (n>0)
                    {
                        for(int i = n+10; i<tmp.Length(); i++)
                        {
                            tmp[i-1]=tmp[i];
                        }
                        tmp.SetLength(tmp.Length()-1);
                        tmp[n+9]  = 'G';
                        tmp[n+10] = 'C';
                        tmp[n+11] = 'C';
                    }
                    m_incList.Add(tmp);
                    includes.Delete(0, separatorPosition+1);
                } else break;
            }
            if (includes.Length()>0)
            {
                includes.Trim();
                m_incList.Add(targetPath + includes);
            }
        }		
    }	

    //cpu name
    TXMLTag*  deviceTag = target->SelectNode("TargetOption/TargetCommonOption/Device");
    if (deviceTag)
    {
        m_cpuType = deviceTag->GetValue();
    } else {
        printf("CPU Type not found\r\n");
    }

    TXMLTag*  vendorTag = target->SelectNode("TargetOption/TargetCommonOption/Vendor");
    if (vendorTag)
    {
        m_cpuVendor = vendorTag->GetValue();
    } else {
        printf("CPU Vendor not found\r\n");
    }

    //fpu present    
    TXMLTag*  cpuTag = target->SelectNode("TargetOption/TargetCommonOption/Cpu");
    if (!cpuTag) 
    {  
        printf("CPU info not found\r\n");
        return false;
    }

    TString   cpuValue = cpuTag->GetValue();


    //cpu type
    i1 = cpuValue.IndexOf("CPUTYPE"); 
    i1 = cpuValue.IndexOf('"', i1); 
    i2 = cpuValue.IndexOf(")",i1);
    if ((i1>0) && (i2>i1))
    {
        m_cpuPlatform.CopyFrom(cpuValue.ToPChar()+i1+1, i2-i1-2);
        m_cpuPlatform.LowerCase();
    }

    m_fpuPresent = false;
    m_fpuVersion = 0;
    if (m_cpuPlatform.Contains("cortex-m4"))
    {
        m_fpuPresent = true;
        m_fpuVersion = 4;
    }
    if (m_cpuPlatform.Contains("cortex-m7"))
    {
        m_fpuPresent = true;
        m_fpuVersion = 5;
    }
    if (m_cpuPlatform.Contains("cortex-h7"))
    {
        m_fpuPresent = true;
        m_fpuVersion = 5;
    }
    if (forceSoftFPU)
    {
        m_fpuPresent = false;
    }

    MemoryFragment memFragment;
    memset(&memFragment, 0, sizeof(memFragment));

    TXMLTag*  armAds = target->SelectNode("TargetOption/TargetArmAds/ArmAdsMisc");

    //rom
    i1 = cpuValue.IndexOf("IROM("); 
    i2 = cpuValue.IndexOf("-",i1+5);
    i3 = cpuValue.IndexOf(")",i1+5);
    if ((i2==-1) || (i2>i3))
    {
        i2 = cpuValue.IndexOf(',', i1+5);
    }
    if ((i1>0) && (i2>i1) && (i3>i2))
    {            
        i1+=5;
        memFragment.beginAddress = HexToULongInt(cpuValue.ToPChar()+i1,   i2-i1);
        memFragment.endAddress   = HexToULongInt(cpuValue.ToPChar()+i2+1, i3-i2-1);            
        m_romFragments.Add(memFragment);
    } else {
        //alternative way how to find ROM address and size (MDK v5)
        if (armAds)
        {
            if (FindMdk5IRom(armAds, memFragment.beginAddress, memFragment.endAddress))
            {
                m_romFragments.Add(memFragment);
            }
        }
        if (m_romFragments.Count()==0)
        {
            memFragment.beginAddress = 0x8100000;
            memFragment.endAddress   = memFragment.beginAddress  + 0x8000;
            m_romFragments.Add(memFragment);
            printf("ROM address not found, inserted address 0x8100000 and size 0x8000 instead\r\n");	
        }        
    }

    //ram
    i1 = cpuValue.IndexOf("IRAM(");
    i1 = cpuValue.IndexOf("(",i1);
    i2 = cpuValue.IndexOf("-",i1);
    i3 = cpuValue.IndexOf(")",i1);          
    if ((i2==-1) || (i2>i3))
    {
        i2 = cpuValue.IndexOf(',', i1);
    }
    if ((i1>0) && (i2>i1) && (i3>i2))
    {        
        i1++;
        memFragment.beginAddress = HexToULongInt(cpuValue.ToPChar()+i1,   i2-i1);
        memFragment.endAddress   = HexToULongInt(cpuValue.ToPChar()+i2+1, i3-i2-1);
        m_ramFragments.Add(memFragment);
    } else {
        if (armAds)
        {
            if (FindMdk5IRam1(armAds, memFragment.beginAddress, memFragment.endAddress))
            {
                m_ramFragments.Add(memFragment);
            }
        }
        if (m_ramFragments.Count()==0)
        {
            memFragment.beginAddress = 0x8200000;
            memFragment.endAddress   = memFragment.beginAddress + 0x4000;
            m_ramFragments.Add(memFragment);
            printf("RAM address not found, inserted address 0x8200000 and size 0x4000 instead\r\n");	
        }
    }

    //ccm ram
    i1 = cpuValue.IndexOf("IRAM2");
    i1 = cpuValue.IndexOf("(",i1);
    i2 = cpuValue.IndexOf("-",i1);
    i3 = cpuValue.IndexOf(")",i1);          
    if ((i2==-1) || (i2>i3))
    {
        i2 = cpuValue.IndexOf(',', i1);
    }
    if ((i1>0) && (i2>i1) && (i3>i2))
    {            
        i1++;
        memFragment.beginAddress = HexToULongInt(cpuValue.ToPChar()+i1,   i2-i1);
        memFragment.endAddress   = HexToULongInt(cpuValue.ToPChar()+i2+1, i3-i2-1);
        m_ramFragments.Add(memFragment);
    } else {
        if (armAds)
        {
            if (FindMdk5IRam2(armAds, memFragment.beginAddress, memFragment.endAddress))
            {
                m_ramFragments.Add(memFragment);
            }
        }
    }          

    //cpu clock
    i1 = cpuValue.IndexOf("CLOCK");
    i1 = cpuValue.IndexOf("(",i1);
    i2 = cpuValue.IndexOf(")",i1);
    if ((i1>0) && (i2>i1))
    {
        i1++;
        m_cpuClock = StrToULongInt(cpuValue.ToPChar()+i1, (unsigned short)(i2-i1));
    } else{
        printf("Clock frequency not found\r\n");
    }

    TXMLTag* groupsTag = target->SelectNode("Groups");
    TXMLTagList* groupTagList = groupsTag->SelectNodes("Group");
    TFilePath filename;   
    TString   fileExt;

    for(TXMLTag* groupTag = groupTagList->First(); groupTag!=NULL; groupTag=groupTagList->Next())
    {
        TXMLTag* filesTag = groupTag->SelectNode("Files");
        if (filesTag == NULL) continue;

        TXMLTagList* fileTagList = filesTag->SelectNodes("File");
        if (fileTagList==NULL) continue;

        for(TXMLTag* fileTag = fileTagList->First(); fileTag!=NULL; fileTag=fileTagList->Next())
        {
            TXMLTag* fileNameTag = fileTag->SelectNode("FileName");
            TXMLTag* filePathTag = fileTag->SelectNode("FilePath");
            if (fileNameTag && filePathTag)
            {
                filename = filePathTag->GetValue();	
                filename.DeleteDoubleSlash();
                filename.Trim();
                if (filename.Contains("startup_"))
                {
                    m_startupFile = filename;
                } else {
                    fileExt = filename.ExtractFileExt();
                    fileExt.LowerCase();
                    if ((fileExt==".c") || (fileExt==".cpp"))
                    { 
                        TFilePath file = targetPath + filename;
                        int n = file.IndexOf("portable/RVDS");
                        if (n>0)
                        {
                            for(int i = n+10; i<file.Length(); i++)
                            {
                                file[i-1]=file[i];
                            }
                            file.SetLength(file.Length()-1);
                            file[n+9] = 'G';
                            file[n+10] = 'C';
                            file[n+11] = 'C';
                            printf("\r\nWarning: RVDS version needs to be replaced by GCC version:\r\n");
                            printf(file);
                            printf("\r\n");

                        }
                        if (m_srcList.IndexOf(file)==-1)
                        {
                            m_srcList.Add(file);
                        }
                    }
                }
                if (filename.Contains("system_"))
                {
                    m_systemFile = filename;
                }
            }
        }		
    }

    TFilePath absStartupPath = keilProjectFile;
    absStartupPath.ChangeFileName("");
    absStartupPath += m_startupFile ;

    m_stackSize = -1;
    m_heapSize  = -1;

    m_state = kcsHeader;
    textfile.Open(absStartupPath);
    while(true)
    {
        if (textfile.ReadLine(line))
        {
            ParseLine(line);
        } else break;        
    }
    textfile.Close();
    if (m_stackSize==-1)
    {
        m_stackSize = 0x0200;
        printf("Minimal stack size not defined, value 0x0200 used\r\n");
    }
    if (m_heapSize == -1)
    {
        m_heapSize = 0x0800;
        printf("Minimal heap size not defined, value 0x0800 used\r\n");
    }
    if (m_irqList.Count()==0)
    {
        printf("ISR vectors not found, is not possible to generate startup file\r\n");

    }
    if (m_state!=kcsEnd)
    {
        printf("End of ISR vector table not found\r\n");
    }
    return true;
};

bool KeilToARMGCC::FindMdk5IRom(TXMLTag* xmlTag, unsigned long& startAddress, unsigned long& endAddress)
{
    TXMLTag* romEnabledTag = NULL;
    TXMLTag* romAddressTag = NULL;

    bool result = false;

    romEnabledTag = xmlTag->SelectNode("Ro1Chk");
    romAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT1");
    result = ReadMemoryBlock(romEnabledTag, romAddressTag, startAddress, endAddress);
    if (!result)
    {
        romEnabledTag = xmlTag->SelectNode("Ro2Chk");
        romAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT2");
        result = ReadMemoryBlock(romEnabledTag, romAddressTag, startAddress, endAddress);
    }
    if (!result)
    {
        romEnabledTag = xmlTag->SelectNode("Ro3Chk");
        romAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT3");
        result = ReadMemoryBlock(romEnabledTag, romAddressTag, startAddress, endAddress);
    }
    if (!result)
    {
        romEnabledTag = xmlTag->SelectNode("Ir1Chk");
        romAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT4");
        result = ReadMemoryBlock(romEnabledTag, romAddressTag, startAddress, endAddress);
    }
    if (!result)
    {
        romEnabledTag = xmlTag->SelectNode("Ir2Chk");
        romAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT5");
        result = ReadMemoryBlock(romEnabledTag, romAddressTag, startAddress, endAddress);
    }
    return result;
}

bool KeilToARMGCC::FindMdk5IRam1(TXMLTag* xmlTag, unsigned long &startAddress, unsigned long& endAddress)
{
    return FindMdk5IRam(xmlTag, 0, startAddress, endAddress);
}

bool KeilToARMGCC::FindMdk5IRam2(TXMLTag* xmlTag, unsigned long &startAddress, unsigned long& endAddress)
{
    return FindMdk5IRam(xmlTag, 1, startAddress, endAddress);
}

bool KeilToARMGCC::FindMdk5IRam(TXMLTag* xmlTag, unsigned long memoryIndex, unsigned long& startAddress, unsigned long& endAddress)
{
    TXMLTag* ramEnabledTag = NULL;
    TXMLTag* ramAddressTag = NULL;

    for(int i = 0; i<5; i++)
    {
        switch(i)
        {
        case 0:
            {
                ramEnabledTag = xmlTag->SelectNode("Ra1Chk");
                ramAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT6");
            }
            break;

        case 1:
            {
                ramEnabledTag = xmlTag->SelectNode("Ra2Chk");
                ramAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT7");
            }
            break;

        case 2:
            {
                ramEnabledTag = xmlTag->SelectNode("Ra3Chk");
                ramAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT8");
            }
            break;

        case 3:
            {
                ramEnabledTag = xmlTag->SelectNode("Im1Chk");
                ramAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT9");
            }
            break;

        case 4:
            {
                ramEnabledTag = xmlTag->SelectNode("Im2Chk");
                ramAddressTag = xmlTag->SelectNode("OnChipMemories/OCR_RVCT10");
            }
            break;						
        }
        if ((ramEnabledTag) && (ramAddressTag))
        {
            if (ReadMemoryBlock(ramEnabledTag, ramAddressTag, startAddress, endAddress))
            {
                if (memoryIndex==0) return true;
                memoryIndex--;
            }
        }
    }
    return false;
}

bool KeilToARMGCC::ReadMemoryBlock(TXMLTag* ramEnabledTag, TXMLTag* ramAddressTag, unsigned long& startAddress, unsigned long& endAddress)
{
    startAddress = -1;
    endAddress = 0;
    int blockLength  = -1;
    const char* enabled = ramEnabledTag->GetValue();
    if (enabled)
    {
        if (enabled[0]!='0') 
        {
            TXMLTag* startAddressTag = ramAddressTag->SelectNode("StartAddress");
            if (startAddressTag)
            {
                startAddress = HexToULongInt(startAddressTag->GetValue());
            }				
            TXMLTag* sizeTag = ramAddressTag->SelectNode("Size");
            if (sizeTag)
            {
                blockLength = HexToULongInt(sizeTag->GetValue());
            }
            if ((startAddress>=0) && (blockLength>0))
            {
                endAddress = startAddress + blockLength;
                return true;	
            }			
        }
    }
    return false;
}


bool KeilToARMGCC::CreateStartupFile(const char* startUpFileName)
{
    int i;
    TString irq;

    m_fileHandle = fopen(startUpFileName, "wb");
    if (m_fileHandle==NULL)
    {
        return false;
    }

    WriteLine("/** Header file generated by Keil2GCC converter.\r\n");	
    WriteLine(" * \r\n");
    WriteLine(" * GENERATED STARTUP FILE WITH ISR VECTORS IS PROVIDED \"AS IS\" AND ANY EXPRESS\r\n");
    WriteLine(" * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\r\n");
    WriteLine(" * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\r\n");
    WriteLine(" * ARE DISCLAIMED.\r\n");
    WriteLine("*/\r\n");
    WriteLine("\r\n");
    WriteLine("  .syntax unified\r\n");
    WriteLine("  .cpu "); WriteLine(m_cpuPlatform.ToPChar()); WriteLine("\r\n");
    WriteLine("  .fpu softvfp\r\n");
    WriteLine("  .thumb\r\n");
    WriteLine("\r\n");
    WriteLine(".global g_pfnVectors\r\n"\
        ".global Default_Handler\r\n"\
        "\r\n"\
        "/* start address for the initialization values of the .data section.\r\n"\
        "defined in linker script */\r\n"\
        ".word _sidata\r\n"\
        "/* start address for the .data section. defined in linker script */\r\n"\
        ".word _sdata\r\n"\
        "/* end address for the .data section. defined in linker script */\r\n"\
        ".word _edata\r\n"\
        "/* start address for the .bss section. defined in linker script */\r\n"\
        ".word _sbss\r\n"\
        "/* end address for the .bss section. defined in linker script */\r\n"\
        ".word _ebss\r\n"\
        "/* stack used for SystemInit_ExtMemCtl; always internal RAM used */\r\n"\
        "\r\n"\
        "/**\r\n"\
        " * @brief  This is the code that gets called when the processor first\r\n"\
        " *          starts execution following a reset event. Only the absolutely\r\n"\
        " *          necessary set is performed, after which the application\r\n"\
        " *          supplied main() routine is called.\r\n"\
        " * @param  None\r\n"\
        " * @retval : None\r\n"\
        "*/\r\n"\
        "\r\n"
        );

    WriteLine("  .section .text.Reset_Handler\r\n");
    WriteLine("  .weak Reset_Handler\r\n");
    WriteLine("  .type Reset_Handler, %function\r\n");
    WriteLine("Reset_Handler:\r\n");
    WriteLine("/* Disable all interrupts.*/\r\n");
    WriteLine("  cpsid i\r\n");
    WriteLine("\r\n");
    WriteLine("/* Set stack pointer */\r\n");
    if (m_cpuPlatform=="cortex-m0")
    {
        WriteLine("  ldr   r0, =_estack\r\n");
        WriteLine("  mov   sp, r0\r\n");
    } else {
        WriteLine("  ldr   sp, =_estack\r\n");
    }
    WriteLine("\r\n");
    WriteLine("/* Copy the data segment initializers from flash to SRAM */\r\n");
    WriteLine("  movs r1, #0\r\n");
    WriteLine("  b LoopCopyDataInit\r\n");
    WriteLine("\r\n");
    WriteLine("CopyDataInit:\r\n");
    WriteLine("  ldr r3, =_sidata\r\n");
    WriteLine("  ldr r3, [r3, r1]\r\n");
    WriteLine("  str r3, [r0, r1]\r\n");
    WriteLine("  adds r1, r1, #4\r\n");
    WriteLine("\r\n");
    WriteLine("LoopCopyDataInit:\r\n");
    WriteLine("  ldr r0, =_sdata\r\n");
    WriteLine("  ldr r3, =_edata\r\n");
    WriteLine("  adds r2, r0, r1\r\n");
    WriteLine("  cmp r2, r3\r\n");
    WriteLine("  bcc CopyDataInit\r\n");
    WriteLine("  ldr r2, =_sbss\r\n");
    WriteLine("  b LoopFillZerobss\r\n");
    WriteLine("/* Zero fill the bss segment. */\r\n");
    WriteLine("FillZerobss:\r\n");
    WriteLine("  movs r3, #0\r\n");
    if (m_cpuPlatform=="cortex-m0")
    {
        WriteLine("  str  r3,[r2]\r\n");
        WriteLine("  adds r2, r2, #4\r\n");
    } else {
        WriteLine("  str r3, [r2], #4\r\n");
    }	
    WriteLine("\r\n");
    WriteLine("LoopFillZerobss:\r\n");
    WriteLine("  ldr r3, = _ebss\r\n");
    WriteLine("  cmp r2, r3\r\n");
    WriteLine("  bcc FillZerobss\r\n");
    WriteLine("\r\n");
    WriteLine("/* Call the clock system intitialization function.*/\r\n");
    WriteLine("  bl  SystemInit\r\n");
    WriteLine("/* Call static constructors */\r\n");
    WriteLine("  bl __libc_init_array\r\n");
    WriteLine("/* Enable all interrupts.*/\r\n");
    WriteLine("  cpsie i\r\n");
    WriteLine("/* Call the application's entry point.*/\r\n");    
    WriteLine("  bl main\r\n");
    WriteLine("  bx lr\r\n");
    WriteLine("  .size Reset_Handler, .-Reset_Handler\r\n");
    WriteLine("\r\n");
    WriteLine("/**\r\n");
    WriteLine("* @brief  This is the code that gets called when the processor receives an\r\n");
    WriteLine("*         unexpected interrupt.  This simply enters an infinite loop, preserving\r\n");
    WriteLine("*         the system state for examination by a debugger.\r\n");
    WriteLine("*\r\n");
    WriteLine("* @param  None\r\n");
    WriteLine("* @retval : None\r\n");
    WriteLine("*/\r\n");
    WriteLine("  .section .text.Default_Handler,\"ax\",%progbits\r\n");
    WriteLine("Default_Handler:\r\n");
    WriteLine("Infinite_Loop:\r\n");
    WriteLine("  b Infinite_Loop\r\n");
    WriteLine("  .size Default_Handler, .-Default_Handler\r\n");
    WriteLine("/******************************************************************************\r\n");
    WriteLine("*\r\n");
    WriteLine("* The minimal vector table for a ");
    WriteLine(m_cpuPlatform);
    WriteLine(".  Note that the proper constructs\r\n");
    WriteLine("* must be placed on this to ensure that it ends up at physical address\r\n");
    WriteLine("* 0x0000.0000.\r\n");
    WriteLine("*\r\n");
    WriteLine("******************************************************************************/\r\n");
    WriteLine("  .section .isr_vector,\"a\",%progbits\r\n");
    WriteLine("  .type g_pfnVectors, %object\r\n");
    WriteLine("  .size g_pfnVectors, .-g_pfnVectors\r\n");
    WriteLine("\r\n");
    WriteLine("g_pfnVectors:\r\n");
    WriteLine("  .word \t_estack\r\n");
    for(i = 0; i<m_irqList.Count(); i++)
    {		
        WriteLine("  .word \t");
        WriteLine(m_irqList[i]);
        WriteLine("\r\n");
    }
    WriteLine("\r\n");
    WriteLine("/*******************************************************************************\r\n");
    WriteLine("*\r\n");
    WriteLine("* Provide weak aliases for each Exception handler to the Default_Handler.\r\n");
    WriteLine("* As they are weak aliases, any function with the same name will override\r\n");
    WriteLine("* this definition.\r\n");
    WriteLine("*\r\n");
    WriteLine("*******************************************************************************/\r\n");
    for(i = 0; i<m_irqList.Count(); i++)
    {
        irq = m_irqList[i];
        if (irq.Length()==0) continue;
        if (irq[0]=='_') continue;
        if (irq[0]=='0') continue;
        if (irq=="Reset_Handler") continue;

        WriteLine("  .weak      "); WriteLine(irq.ToPChar()); WriteLine("\r\n");
        WriteLine("  .thumb_set "); WriteLine(irq.ToPChar()); WriteLine(",Default_Handler\r\n");
        WriteLine("\r\n");		
    }  
    WriteLine("\r\n");
    //WriteLine("/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/\r\n");

    fclose(m_fileHandle);
    m_fileHandle = NULL;
    return true;
}

bool KeilToARMGCC::CreateMakeFile(const char* makeFileName)
{
    int i = 0;

    m_fileHandle = fopen(makeFileName, "wb");
    if (m_fileHandle==NULL)
    {
        return false;
    }
    WriteLine("#   Makefile generated by Keil2GCC converter (c)2018 Ondrej Sterba.\r\n");	
    WriteLine("# \r\n");
    WriteLine("# GENERATED MAKEFILE IS PROVIDED \"AS IS\" AND ANY EXPRESS\r\n");
    WriteLine("# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\r\n");
    WriteLine("# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\r\n");
    WriteLine("# ARE DISCLAIMED.\r\n");
    WriteLine("#/\r\n");
    WriteLine("\r\n");
    WriteLine("PROJECT = "); WriteLine(m_targetName.ToPChar()); WriteLine("\r\n");
    WriteLine("\r\n");
    WriteLine("CC	= arm-none-eabi-gcc\r\n");
    WriteLine("CPP	= arm-none-eabi-g++\r\n");
    WriteLine("AS	= arm-none-eabi-as\r\n");
    WriteLine("LD	= arm-none-eabi-g++\r\n");
    WriteLine("CP	= arm-none-eabi-objcopy\r\n");
    WriteLine("OS	= arm-none-eabi-size\r\n");
    WriteLine("OD	= arm-none-eabi-objdump\r\n");
    WriteLine("\r\n");
    WriteLine("VECTOR	= ./"); WriteLine(m_startupFile); WriteLine("\r\n");
    WriteLine("\r\n");
    WriteLine("LDSCRIPT = ./"); WriteLine(m_ldScriptFile); WriteLine("\r\n");
    WriteLine("LDFLAGS  = -mcpu=");
    WriteLine(m_cpuPlatform);
    WriteLine(" -mthumb ");
    if (m_fpuPresent)
    {
        WriteLine("-mfloat-abi=hard -mfpu=fpv");
        WriteInt(m_fpuVersion);
        WriteLine("-sp-d16 ");
    } else {
        WriteLine("-mfloat-abi=soft ");
    }
    WriteLine("-T$(LDSCRIPT) -lnosys -Wl,--gc-sections -Wl,-Map,$(PROJECT).map\r\n");
    WriteLine("\r\n");

    bool first = true;
    for(i = 0; i<m_incList.Count(); i++)
    {
        if (first)
        {
            WriteLine("INC = -I");
        }
        else 
        {
            WriteLine("      -I");
        }
        WriteLine(m_incList[i]);
        WriteLine("\\\r\n");

        first = false;
    }
    WriteLine("\r\n");
    WriteLine("SRC = $(VECTOR) \\\r\n");
    for(i = 0; i<m_srcList.Count(); i++)
    {
        WriteLine("      ");
        WriteLine(m_srcList[i]);
        WriteLine("\\\r\n");
    }
    WriteLine("\r\n");

    WriteLine("#  C++ source files\r\n");
    WriteLine("CPPFILES = $(filter %.cpp, $(SRC))\r\n");
    WriteLine("#  C source files\r\n");
    WriteLine("CFILES = $(filter %.c, $(SRC))\r\n");
    WriteLine("#  Assembly source files\r\n");
    WriteLine("ASMFILES = $(filter %.s, $(SRC))\r\n");
    WriteLine("\r\n");
    WriteLine("# Object files\r\n");
    WriteLine("CPPOBJ = $(CPPFILES:.cpp=.o)\r\n");
    WriteLine("COBJ = $(CFILES:.c=.o)\r\n");
    WriteLine("SOBJ = $(ASMFILES:.s=.o)\r\n");
    WriteLine("OBJ  = $(CPPOBJ) $(COBJ) $(SOBJ)\r\n");
    WriteLine("\r\n");

    WriteLine("# Compile thumb for ");
    WriteLine(m_cpuPlatform);
    WriteLine(" with debug info\r\n");
    WriteLine("CPPFLAGS  = -g -mthumb ");
    for(i = 0; i<m_defineList.Count(); i++)
    {
        WriteLine("-D");
        WriteLine(m_defineList[i]);
        WriteLine(" ");
    }
    WriteLine("-mcpu=");WriteLine(m_cpuPlatform); WriteLine(" ");
    if (m_fpuPresent)
    {
        if (m_fpuPresent)
        {
            WriteLine("-mfloat-abi=hard -mfpu=fpv");
            WriteInt(m_fpuVersion);
            WriteLine("-sp-d16 ");
        } else {
            WriteLine("-mfloat-abi=soft ");
        }
    } else {
        WriteLine("-mfloat-abi=soft ");
    }
    WriteLine("-Og -fpack-struct -fdata-sections -ffunction-sections -fno-exceptions -fno-rtti -std=c++11\r\n");
    WriteLine("CFLAGS  = -g -mthumb ");
    for(i = 0; i<m_defineList.Count(); i++)
    {
        WriteLine("-D");
        WriteLine(m_defineList[i]);
        WriteLine(" ");
    }
    WriteLine("-mcpu=");WriteLine(m_cpuPlatform); WriteLine(" ");
    if (m_fpuPresent)
    {
        WriteLine("-mfloat-abi=hard -mfpu=fpv4-sp-d16 ");
    } else {
        WriteLine("-mfloat-abi=soft ");
    }	
    WriteLine("-Og -fpack-struct -fdata-sections -ffunction-sections -std=c99\r\n");
    WriteLine("ASFLAGS = -g -mthumb -mcpu="); WriteLine(m_cpuPlatform); WriteLine("\r\n");
    WriteLine("\r\n");
    WriteLine("\r\n");
    WriteLine("all: $(SRC) $(PROJECT).elf $(PROJECT).hex $(PROJECT).bin\r\n");
    WriteLine("\r\n");
    WriteLine("$(PROJECT).bin: $(PROJECT).elf\r\n");
    WriteLine("	@$(CP) -O binary $(PROJECT).elf $@\r\n");
    WriteLine("\r\n");
    WriteLine("$(PROJECT).hex: $(PROJECT).elf\r\n");
    WriteLine("	@$(CP) -O ihex $(PROJECT).elf $@\r\n");
    WriteLine("\r\n");
    WriteLine("$(PROJECT).elf: $(OBJ)\r\n");
    WriteLine("	@echo Linking\r\n");
    WriteLine("	@$(LD) $(LDFLAGS) $(OBJ) -o $@\r\n");
    WriteLine("	@$(OS) -t $(PROJECT).elf\r\n");
    WriteLine("\r\n");
    WriteLine("\r\n");
    WriteLine("$(CPPOBJ): %.o: %.cpp\r\n");
    WriteLine("	@echo $<\r\n");
    WriteLine("	@$(CPP) -c $(INC) $(CPPFLAGS) $< -o $@\r\n");
    WriteLine("\r\n");
    WriteLine("$(COBJ): %.o: %.c\r\n");
    WriteLine("	@echo $<\r\n");
    WriteLine("	@$(CC) -c $(INC) $(CFLAGS) $< -o $@\r\n");
    WriteLine("\r\n");
    WriteLine("$(SOBJ): %.o: %.s\r\n");
    WriteLine("	@echo $<\r\n");
    WriteLine("	@$(AS) -c $(ASFLAGS) $< -o $@\r\n");
    WriteLine("\r\n");
    WriteLine("clean:\r\n");
    WriteLine("\t@rm -f $(PROJECT).elf $(PROJECT).bin $(PROJECT).map $(PROJECT).hex $(PROJECT).lss $(OBJ)\r\n");
    WriteLine("\r\n");
    WriteLine("install: $(PROJECT).bin\r\n");
    WriteLine("\tJLink.exe -device ");
    WriteLine(m_cpuType); 
    WriteLine(" -CommanderScript ./flash.jlink");
    WriteLine("\r\n");
    //WriteLine(".PHONY: clean\r\n");
    //WriteLine("\r\n");
    fclose(m_fileHandle);
    m_fileHandle = NULL;

    return true;
}

bool KeilToARMGCC::CreateLDScript(const char* ldScriptFileName)
{
    int i = 0;

    m_fileHandle = fopen(ldScriptFileName, "wb");
    if (m_fileHandle==NULL)
    {
        return false;
    }
    WriteLine("/** Linker file generated by Keil2GCC converter.\r\n");	
    WriteLine(" * \r\n");
    WriteLine(" * GENERATED LINKER FILE IS PROVIDED \"AS IS\" AND ANY EXPRESS\r\n");
    WriteLine(" * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\r\n");
    WriteLine(" * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\r\n");
    WriteLine(" * ARE DISCLAIMED.\r\n");
    WriteLine("*/\r\n");
    WriteLine("\r\n");
    WriteLine("/* Entry Point */\r\n");
    WriteLine("ENTRY(Reset_Handler)\r\n");
    WriteLine("\r\n");
    WriteLine("/* Highest address of the user mode stack */\r\n");
    if (m_ramFragments.Count()>0)
    {
        WriteLine("_estack = 0x");
        WriteHex(m_ramFragments[0].endAddress+1);
        WriteLine(";\t/* end of RAM */\r\n");
    }	
    WriteLine("/* Generate a link error if heap and stack don't fit into RAM */\r\n");
    WriteLine("_Min_Heap_Size = 0x");  WriteHex(m_heapSize);  WriteLine("; /* required amount of heap  */\r\n");
    WriteLine("_Min_Stack_Size = 0x"); WriteHex(m_stackSize); WriteLine("; /* required amount of stack */ \r\n");
    WriteLine("\r\n");
    WriteLine("/* Specify the memory areas */\r\n");
    WriteLine("MEMORY\r\n");
    WriteLine("{\r\n");
    if(m_romFragments.Count()>0)
    {
        WriteLine("FLASH (rx)\t:ORIGIN = 0x");
        WriteHex(m_romFragments[0].beginAddress);
        WriteLine(", LENGTH = ");
        WriteInt((m_romFragments[0].endAddress-m_romFragments[0].beginAddress+1)/1024);
        WriteLine("K\r\n");
    }	
    if(m_ramFragments.Count()>0)
    {
        WriteLine("RAM (xrw)\t:ORIGIN = 0x");
        WriteHex(m_ramFragments[0].beginAddress);
        WriteLine(", LENGTH = ");
        WriteInt((m_ramFragments[0].endAddress - m_ramFragments[0].beginAddress+1)/1024);
        WriteLine("K\r\n");
    }
    for(int i = 1; i<m_ramFragments.Count(); i++)
    {
        WriteLine("CCMRAM (rw)\t:ORIGIN = 0x");
        WriteHex(m_ramFragments[i].beginAddress);
        WriteLine(", LENGTH = ");
        WriteInt((m_ramFragments[i].endAddress-m_ramFragments[i].beginAddress+1)/1024);
        WriteLine("K\r\n");
    }
    WriteLine("}\r\n");
    WriteLine("\r\n");	
    WriteLine("/* Define output sections */\r\n");
    WriteLine("SECTIONS\r\n");
    WriteLine("{\r\n");
    WriteLine("  /* The startup code goes first into FLASH */\r\n");
    WriteLine("  .isr_vector :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    . = ALIGN(4);\r\n");
    WriteLine("    KEEP(*(.isr_vector)) /* Startup code */\r\n");
    WriteLine("    . = ALIGN(4);\r\n");
    WriteLine("  } >FLASH\r\n");
    WriteLine("\r\n");
    WriteLine("  .descriptor :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    . = ALIGN(128);\r\n");
    WriteLine("    KEEP(*(.descriptor)) /* Startup code */\r\n");
    WriteLine("  } >FLASH\r\n");
    WriteLine("\r\n");
    WriteLine("  /* The program code and other data goes into FLASH */\r\n");
    WriteLine("  .text :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    . = ALIGN(4);\r\n");
    WriteLine("    *(.text)           /* .text sections (code) */\r\n");
    WriteLine("    *(.text*)          /* .text* sections (code) */\r\n");
    WriteLine("    *(.glue_7)         /* glue arm to thumb code */\r\n");
    WriteLine("    *(.glue_7t)        /* glue thumb to arm code */\r\n");
    WriteLine("    *(.eh_frame)\r\n");
    WriteLine("\r\n");
    WriteLine("    KEEP (*(.init))\r\n");
    WriteLine("    KEEP (*(.fini))\r\n");
    WriteLine("\r\n");
    WriteLine("    . = ALIGN(4);\r\n");
    WriteLine("    _etext = .;        /* define a global symbols at end of code */\r\n");
    WriteLine("  } >FLASH\r\n");
    WriteLine("\r\n");
    WriteLine("  /* Constant data goes into FLASH */\r\n");
    WriteLine("  .rodata :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    . = ALIGN(4);\r\n");
    WriteLine("    *(.rodata)         /* .rodata sections (constants, strings, etc.) */\r\n");
    WriteLine("    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */\r\n");
    WriteLine("    . = ALIGN(4);\r\n");
    WriteLine("  } >FLASH\r\n");
    WriteLine("\r\n");
    WriteLine("  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH\r\n");
    WriteLine("  .ARM : {\r\n");
    WriteLine("    __exidx_start = .;\r\n");
    WriteLine("    *(.ARM.exidx*)\r\n");
    WriteLine("    __exidx_end = .;\r\n");
    WriteLine("  } >FLASH\r\n");
    WriteLine("\r\n");
    WriteLine("  .preinit_array     :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    PROVIDE_HIDDEN (__preinit_array_start = .);\r\n");
    WriteLine("    KEEP (*(.preinit_array*))\r\n");
    WriteLine("    PROVIDE_HIDDEN (__preinit_array_end = .);\r\n");
    WriteLine("  } >FLASH\r\n");
    WriteLine("  .init_array :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    PROVIDE_HIDDEN (__init_array_start = .);\r\n");
    WriteLine("    KEEP (*(SORT(.init_array.*)))\r\n");
    WriteLine("    KEEP (*(.init_array*))\r\n");
    WriteLine("    PROVIDE_HIDDEN (__init_array_end = .);\r\n");
    WriteLine("  } >FLASH\r\n");
    WriteLine("  .fini_array :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    PROVIDE_HIDDEN (__fini_array_start = .);\r\n");
    WriteLine("    KEEP (*(SORT(.fini_array.*)))\r\n");
    WriteLine("    KEEP (*(.fini_array*))\r\n");
    WriteLine("    PROVIDE_HIDDEN (__fini_array_end = .);\r\n");
    WriteLine("  } >FLASH\r\n");
    WriteLine("\r\n");
    WriteLine("  /* used by the startup to initialize data */\r\n");
    WriteLine("  _sidata = LOADADDR(.data);\r\n");
    WriteLine("\r\n");
    WriteLine("  /* Initialized data sections goes into RAM, load LMA copy after code */\r\n");
    WriteLine("  .data :\r\n"); 
    WriteLine("  {\r\n");
    WriteLine("    . = ALIGN(4);\r\n");
    WriteLine("    _sdata = .;        /* create a global symbol at data start */\r\n");
    WriteLine("    *(.data)           /* .data sections */\r\n");
    WriteLine("    *(.data*)          /* .data* sections */\r\n");
    WriteLine("\r\n");
    WriteLine("    . = ALIGN(4);\r\n");
    WriteLine("    _edata = .;        /* define a global symbol at data end */\r\n");
    WriteLine("  } >RAM AT> FLASH\r\n");
    WriteLine("\r\n");
    if (m_ramFragments.Count()>1)
    {
        WriteLine("  _siccmram = LOADADDR(.ccmram);\r\n");
        WriteLine("\r\n");
        WriteLine("  /* CCM-RAM section \r\n");
        WriteLine("  * \r\n");
        WriteLine("  * IMPORTANT NOTE! \r\n");
        WriteLine("  * If initialized variables will be placed in this section,\r\n");
        WriteLine("  * the startup code needs to be modified to copy the init-values.\r\n");
        WriteLine("  */\r\n");
        WriteLine("  .ccmram :\r\n");
        WriteLine("  {\r\n");
        WriteLine("    . = ALIGN(4);\r\n");
        WriteLine("    _sccmram = .;       /* create a global symbol at ccmram start */\r\n");
        WriteLine("    *(.ccmram)\r\n");
        WriteLine("    *(.ccmram*)\r\n");
        WriteLine("\r\n");
        WriteLine("    . = ALIGN(4);\r\n");
        WriteLine("    _eccmram = .;       /* create a global symbol at ccmram end */\r\n");
        WriteLine("  } >CCMRAM AT> FLASH\r\n");
        WriteLine("\r\n");
    }
    WriteLine("\r\n");
    WriteLine("  /* Uninitialized data section */\r\n");
    WriteLine("  . = ALIGN(4);\r\n");
    WriteLine("  .bss :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    /* This is used by the startup in order to initialize the .bss secion */\r\n");
    WriteLine("    _sbss = .;         /* define a global symbol at bss start */\r\n");
    WriteLine("    __bss_start__ = _sbss;\r\n");
    WriteLine("    *(.bss)\r\n");
    WriteLine("    *(.bss*)\r\n");
    WriteLine("    *(COMMON)\r\n");
    WriteLine("\r\n");
    WriteLine("    . = ALIGN(4);\r\n");
    WriteLine("    _ebss = .;         /* define a global symbol at bss end */\r\n");
    WriteLine("    __bss_end__ = _ebss;\r\n");
    WriteLine("  } >RAM\r\n");
    WriteLine("\r\n");
    WriteLine("  /* User_heap_stack section, used to check that there is enough RAM left */\r\n");
    WriteLine("  ._user_heap_stack :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    . = ALIGN(8);\r\n");
    WriteLine("    PROVIDE ( end = . );\r\n");
    WriteLine("    PROVIDE ( _end = . );\r\n");
    WriteLine("    . = . + _Min_Heap_Size;\r\n");
    WriteLine("    . = . + _Min_Stack_Size;\r\n");
    WriteLine("    . = ALIGN(8);\r\n");
    WriteLine("  } >RAM\r\n");
    WriteLine("\r\n");
    WriteLine("\r\n");
    WriteLine("  /* Remove information from the standard libraries */\r\n");
    WriteLine("  /DISCARD/ :\r\n");
    WriteLine("  {\r\n");
    WriteLine("    libnosys.a ( * )\r\n");
    WriteLine("    libc.a ( * )\r\n");
    WriteLine("    libm.a ( * )\r\n");
    WriteLine("    libgcc.a ( * )\r\n");
    WriteLine("  }\r\n");
    WriteLine("\r\n");
    WriteLine("  .ARM.attributes 0 : { *(.ARM.attributes) }\r\n");
    WriteLine("}\r\n");
    fclose(m_fileHandle);
    m_fileHandle = NULL;

    return true;
}

bool KeilToARMGCC::CreateJLinkFile(const char* jlinkFileName)
{
    m_fileHandle = fopen(jlinkFileName, "wb");
    if (m_fileHandle==NULL)
    {
        return false;
    }

    WriteLine("si 1\r\n");
    WriteLine("speed 2000\r\n");
    WriteLine("r\r\n");
    WriteLine("h\r\n");
    WriteLine("loadbin ");
    WriteLine(m_targetName);
    WriteLine(".bin,0x");
    if (m_romFragments.Count()==0)
    {
        return false;
    }
    WriteHex( m_romFragments[0].beginAddress);		
    WriteLine("\r\n");
    WriteLine("r\r\n");
    WriteLine("exit\r\n");

    return true;
}


bool KeilToARMGCC::CreateProjectFile(const char* projectFileName)
{
    int i = 0;	
    m_fileHandle = fopen(projectFileName, "wr");
    if (m_fileHandle==NULL)
    {
        return false;
    }

    WriteLine("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n");
    WriteLine("<projectDescription>\r\n");
    WriteLine("    <name>"); WriteLine(m_targetName); WriteLine("</name>\r\n");
    WriteLine("    <comment></comment>\r\n");
    WriteLine("    <projects>\r\n");
    WriteLine("    </projects>\r\n");
    WriteLine("    <buildSpec>\r\n");
    WriteLine("        <buildCommand>\r\n");
    WriteLine("            <name>org.eclipse.cdt.managedbuilder.core.genmakebuilder</name>\r\n");
    WriteLine("            <triggers>clean,full,incremental,</triggers>\r\n");
    WriteLine("            <arguments>\r\n");
    WriteLine("            </arguments>\r\n");
    WriteLine("        </buildCommand>\r\n");
    WriteLine("        <buildCommand>\r\n");
    WriteLine("             <name>org.eclipse.cdt.managedbuilder.core.ScannerConfigBuilder</name>\r\n");
    WriteLine("             <triggers>full,incremental,</triggers>\r\n");
    WriteLine("             <arguments>\r\n");
    WriteLine("             </arguments>\r\n");
    WriteLine("        </buildCommand>\r\n");
    WriteLine("    </buildSpec>\r\n");
    WriteLine("    <natures>\r\n");
    WriteLine("        <nature>org.eclipse.cdt.core.cnature</nature>\r\n");
    WriteLine("        <nature>org.eclipse.cdt.core.ccnature</nature>\r\n");
    WriteLine("        <nature>org.eclipse.cdt.managedbuilder.core.managedBuildNature</nature>\r\n");
    WriteLine("        <nature>org.eclipse.cdt.managedbuilder.core.ScannerConfigNature</nature>\r\n");
    WriteLine("    </natures>\r\n");
    WriteLine("</projectDescription>\r\n");			
    fclose(m_fileHandle);
    m_fileHandle = NULL;

    return true;
}

bool KeilToARMGCC::CreateCProjectFile(const char* cProjectFileName)
{
    int i = 0;	
    m_fileHandle = fopen(cProjectFileName, "wr");
    if (m_fileHandle==NULL)
    {
        return false;
    }

    unsigned long configId = 872598617;
    unsigned long toolChainId = 345574508;

    ReplaceUIDAndWriteLine("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\r\n");
    ReplaceUIDAndWriteLine("<?fileVersion 4.0.0?>\r\n");
    ReplaceUIDAndWriteLine("<cproject storage_type_id=\"org.eclipse.cdt.core.XmlProjectDescriptionStorage\">\r\n");
    ReplaceUIDAndWriteLine("    <storageModule moduleId=\"org.eclipse.cdt.core.settings\">\r\n");
    ReplaceUIDAndWriteLine("        <cconfiguration id=\"0.");    
    WriteInt(configId);
    ReplaceUIDAndWriteLine("\">\r\n");
    ReplaceUIDAndWriteLine("            <storageModule buildSystemId=\"org.eclipse.cdt.managedbuilder.core.configurationDataProvider\" id=\"0.");
    WriteInt(configId);    
    ReplaceUIDAndWriteLine("\" moduleId=\"org.eclipse.cdt.core.settings\" name=\"Default\">\r\n");
    ReplaceUIDAndWriteLine("                <externalSettings/>\r\n");
    ReplaceUIDAndWriteLine("                <extensions>\r\n");
    ReplaceUIDAndWriteLine("                    <extension id=\"org.eclipse.cdt.core.GASErrorParser\" point=\"org.eclipse.cdt.core.ErrorParser\"/>\r\n");
    ReplaceUIDAndWriteLine("                    <extension id=\"org.eclipse.cdt.core.GmakeErrorParser\" point=\"org.eclipse.cdt.core.ErrorParser\"/>\r\n");
    ReplaceUIDAndWriteLine("                    <extension id=\"org.eclipse.cdt.core.GLDErrorParser\" point=\"org.eclipse.cdt.core.ErrorParser\"/>\r\n");
    ReplaceUIDAndWriteLine("                    <extension id=\"org.eclipse.cdt.core.VCErrorParser\" point=\"org.eclipse.cdt.core.ErrorParser\"/>\r\n");
    ReplaceUIDAndWriteLine("                    <extension id=\"org.eclipse.cdt.core.CWDLocator\" point=\"org.eclipse.cdt.core.ErrorParser\"/>\r\n");
    ReplaceUIDAndWriteLine("                    <extension id=\"org.eclipse.cdt.core.GCCErrorParser\" point=\"org.eclipse.cdt.core.ErrorParser\"/>\r\n");
    ReplaceUIDAndWriteLine("                </extensions>\r\n");
    ReplaceUIDAndWriteLine("            </storageModule>\r\n");
    ReplaceUIDAndWriteLine("            <storageModule moduleId=\"cdtBuildSystem\" version=\"4.0.0\">\r\n");
    ReplaceUIDAndWriteLine("                <configuration artifactName=\"${ProjName}\" buildProperties=\"\" description=\"\" id=\"0.");
    WriteInt(configId);
    ReplaceUIDAndWriteLine("\" name=\"Default\" parent=\"org.eclipse.cdt.build.core.prefbase.cfg\">\r\n");
    ReplaceUIDAndWriteLine("                    <folderInfo id=\"0.");
    WriteInt(configId);
    ReplaceUIDAndWriteLine(".\" name=\"/\" resourcePath=\"\">\r\n");
    ReplaceUIDAndWriteLine("                        <toolChain id=\"org.eclipse.cdt.build.core.prefbase.toolchain.");
    WriteInt(toolChainId);
    ReplaceUIDAndWriteLine("\" name=\"No ToolChain\" resourceTypeBasedDiscovery=\"false\" superClass=\"org.eclipse.cdt.build.core.prefbase.toolchain\">\r\n");
    ReplaceUIDAndWriteLine("                            <targetPlatform id=\"org.eclipse.cdt.build.core.prefbase.toolchain.");
    WriteInt(toolChainId);
    ReplaceUIDAndWriteLine(".1484797154\" name=\"\"/>\r\n");
    ReplaceUIDAndWriteLine("                            <builder id=\"org.eclipse.cdt.build.core.settings.default.builder.%%NEWUID%%\" keepEnvironmentInBuildfile=\"false\" managedBuildOn=\"false\" name=\"Gnu Make Builder\" superClass=\"org.eclipse.cdt.build.core.settings.default.builder\"/>\r\n");
    ReplaceUIDAndWriteLine("                            <tool id=\"org.eclipse.cdt.build.core.settings.holder.libs.%%NEWUID%%\" name=\"holder for library settings\" superClass=\"org.eclipse.cdt.build.core.settings.holder.libs\"/>\r\n");
    ReplaceUIDAndWriteLine("                            <tool id=\"org.eclipse.cdt.build.core.settings.holder.%%NEWUID%%\" name=\"Assembly\" superClass=\"org.eclipse.cdt.build.core.settings.holder\">\r\n");
    ReplaceUIDAndWriteLine("                                <inputType id=\"org.eclipse.cdt.build.core.settings.holder.inType.%%NEWUID%%\" languageId=\"org.eclipse.cdt.core.assembly\" languageName=\"Assembly\" sourceContentType=\"org.eclipse.cdt.core.asmSource\" superClass=\"org.eclipse.cdt.build.core.settings.holder.inType\"/>\r\n");
    ReplaceUIDAndWriteLine("                            </tool>\r\n");
    ReplaceUIDAndWriteLine("                            <tool id=\"org.eclipse.cdt.build.core.settings.holder.%%NEWUID%%\" name=\"GNU C++\" superClass=\"org.eclipse.cdt.build.core.settings.holder\">\r\n");
    ReplaceUIDAndWriteLine("                                <inputType id=\"org.eclipse.cdt.build.core.settings.holder.inType.%%NEWUID%%\" languageId=\"org.eclipse.cdt.core.g++\" languageName=\"GNU C++\" sourceContentType=\"org.eclipse.cdt.core.cxxSource,org.eclipse.cdt.core.cxxHeader\" superClass=\"org.eclipse.cdt.build.core.settings.holder.inType\"/>\r\n");
    ReplaceUIDAndWriteLine("                            </tool>\");\r\n");
    ReplaceUIDAndWriteLine("                            <tool id=\"org.eclipse.cdt.build.core.settings.holder.%%NEWUID%%\" name=\"GNU C\" superClass=\"org.eclipse.cdt.build.core.settings.holder\">\r\n");
    ReplaceUIDAndWriteLine("                                <option id=\"org.eclipse.cdt.build.core.settings.holder.incpaths.%%NEWUID%%\" name=\"Include Paths\" superClass=\"org.eclipse.cdt.build.core.settings.holder.incpaths\" valueType=\"includePath\">\r\n");
    /*
    ReplaceUIDAndWriteLine("                                    <listOptionValue builtIn=\"false\" value=\"&quot;${workspace_loc:/btb175/Inc}&quot;\"/>\r\n");
    ReplaceUIDAndWriteLine("                                    <listOptionValue builtIn=\"false\" value=\"&quot;C:\armgcc\arm-none-eabi\include&quot;\"/>\r\n");
    ReplaceUIDAndWriteLine("                                    <listOptionValue builtIn=\"false\" value=\"&quot;C:\armgcc\lib\gcc\arm-none-eabi\4.8.4\include&quot;\"/>\r\n");
    */
    ReplaceUIDAndWriteLine("                                </option>\r\n");
    ReplaceUIDAndWriteLine("                                <option id=\"org.eclipse.cdt.build.core.settings.holder.symbols.%%NEWUID%%\" name=\"Symbols\" superClass=\"org.eclipse.cdt.build.core.settings.holder.symbols\" valueType=\"definedSymbols\">\r\n");
    ReplaceUIDAndWriteLine("                                     <listOptionValue builtIn=\"false\" value=\"STM32F427xx\"/>\r\n");
    ReplaceUIDAndWriteLine("                                </option>\r\n");
    ReplaceUIDAndWriteLine("                                <inputType id=\"org.eclipse.cdt.build.core.settings.holder.inType.%%NEWUID%%\" languageId=\"org.eclipse.cdt.core.gcc\" languageName=\"GNU C\" sourceContentType=\"org.eclipse.cdt.core.cSource,org.eclipse.cdt.core.cHeader\" superClass=\"org.eclipse.cdt.build.core.settings.holder.inType\"/>\r\n");
    ReplaceUIDAndWriteLine("                            </tool>\r\n");
    ReplaceUIDAndWriteLine("                        </toolChain>\r\n");
    ReplaceUIDAndWriteLine("                    </folderInfo>\r\n");
    ReplaceUIDAndWriteLine("                </configuration>\r\n");
    ReplaceUIDAndWriteLine("            </storageModule>\r\n");
    ReplaceUIDAndWriteLine("            <storageModule moduleId=\"org.eclipse.cdt.core.externalSettings\"/>\r\n");
    ReplaceUIDAndWriteLine("        </cconfiguration>\r\n");
    ReplaceUIDAndWriteLine("    </storageModule>\r\n");
    ReplaceUIDAndWriteLine("    <storageModule moduleId=\"cdtBuildSystem\" version=\"4.0.0\">\r\n");
    ReplaceUIDAndWriteLine("        <project id=\"btb175.null.%%NEWUID%%\" name=\"btb175\"/>\r\n");
    ReplaceUIDAndWriteLine("    </storageModule>\r\n");
    ReplaceUIDAndWriteLine("    <storageModule moduleId=\"scannerConfiguration\">\r\n");
    ReplaceUIDAndWriteLine("        <autodiscovery enabled=\"true\" problemReportingEnabled=\"true\" selectedProfileId=\"\"/>\r\n");
    ReplaceUIDAndWriteLine("        <scannerConfigBuildInfo instanceId=\"0.");
    WriteInt(configId);
    ReplaceUIDAndWriteLine("\">\r\n");
    ReplaceUIDAndWriteLine("            <autodiscovery enabled=\"true\" problemReportingEnabled=\"true\" selectedProfileId=\"\"/>\r\n");
    ReplaceUIDAndWriteLine("        </scannerConfigBuildInfo>\r\n");
    ReplaceUIDAndWriteLine("    </storageModule>\r\n");
    ReplaceUIDAndWriteLine("    <storageModule moduleId=\"org.eclipse.cdt.core.LanguageSettingsProviders\"/>\r\n");
    ReplaceUIDAndWriteLine("    <storageModule moduleId=\"refreshScope\"/>\r\n");
    ReplaceUIDAndWriteLine("</cproject>\r\n");	

    fclose(m_fileHandle);
    m_fileHandle = NULL;
    return true;
}

void KeilToARMGCC::ReplaceUIDAndWriteLine(const char* text)
{
    TString s = text;
    unsigned long randomId = TRandom::GetRandomNumber();

    int i = s.IndexOf("%%NEWUID%%");
    if (i!=-1)
    {
        sprintf((char*)(s.ToPChar()+i), "%010d", randomId);
    }
}

void KeilToARMGCC::WriteLine(const char* text)
{
    if (text==NULL) return;

    fwrite(text, 1, strlen(text), m_fileHandle);
}

void KeilToARMGCC::WriteInt(int number)
{
    char num[12];
    ULongIntToStr(number, num, 12);
    fwrite(num, 1, strlen(num), m_fileHandle);
}

void KeilToARMGCC::WriteHex(int number)
{
    char hex[12];
    ULongIntToHex(number, hex, 12);
    fwrite(hex, 1, strlen(hex), m_fileHandle);
}

void KeilToARMGCC::ParseLine(TString& line)
{
    switch(m_state)
    {
    case kcsHeader:
        {
            if ( (line.IndexOf("Stack_Size")==0) ||
                (line.IndexOf("Heap_Size")==0) )
            {
                SetState(kcsParams);
                ParseLine(line);
            };
            if ( (line.IndexOf("__Vectors")==0))
            {
                SetState(kcsVectors);
                ParseLine(line);				
            }
        }
        break;

    case kcsParams:
        {
            if (line.IndexOf("__Vectors")==0)
            {
                SetState(kcsVectors);
                ParseLine(line);
            } else {		
                ParseParams(line);
            }
        }
        break;

    case kcsVectors:
        {
            if (line.IndexOf("Reset_Handler")==0)
            {
                SetState(kcsEnd);				
            } else 
            {
                ParseVectors(line);

            }			
        }
        break;

    case kcsEnd:
        {
            //do nothing
        }
        break;
    }
}

void KeilToARMGCC::ParseParams(TString& line)
{
    TString sizeString;
    short i;

    i = line.IndexOf("Stack_Size");
    if (i==0)
    {
        i = line.IndexOf("EQU");		
        if (i>0)
        {
            sizeString.CopyFrom(line.ToPChar()+i+3);
            sizeString.Trim();
            m_stackSize = HexToULongInt(sizeString);
        }
    }
    i = line.IndexOf("Heap_Size");
    if (i==0)
    {
        i = line.IndexOf("EQU");
        TString sizeString;
        if (i>0)
        {
            sizeString.CopyFrom(line.ToPChar()+i+3);
            sizeString.Trim();
            m_heapSize = HexToULongInt(sizeString.ToPChar());
        }
    }
}

void KeilToARMGCC::ParseVectors(TString& line)
{
    TString irqName;

    long i1,i2;

    i1 = line.IndexOf("DCD");
    if (i1==-1) return;

    i2 = line.IndexOf(";");
    if (i2==-1) 
    {
        i2 = line.Length();	
    }
    if (i2<i1) return;

    irqName.CopyFrom(line.ToPChar()+i1+3, (unsigned short)(i2-i1-3));
    irqName.Trim();
    if (irqName.Length()==0)
    {
        return;
    }
    if (irqName[0]!='_')
    {
        m_irqList.Add(irqName);	
    }	
}

