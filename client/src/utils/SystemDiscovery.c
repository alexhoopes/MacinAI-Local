/*------------------------------------------------------------------------------
    SystemDiscovery.c - Hardware Detection for MacinAI Local

    Detects hardware capabilities at startup using Gestalt.
    Results stored in gApp.hardware for engine configuration.

    For CodeWarrior Pro 5 on System 7.5.3+

    Written by Alex Hoopes
    Copyright (c) 2026 OldAppleStuff / Alex Hoopes

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
------------------------------------------------------------------------------*/

#include "SystemDiscovery.h"
#include "SafeString.h"
#include <Gestalt.h>
#include <Memory.h>
#include <string.h>
#include <stdio.h>

#pragma segment Main

extern AppGlobals gApp;

/*------------------------------------------------------------------------------
    CPU type name lookup
------------------------------------------------------------------------------*/
static const char* GetCPUName(long cpuType)
{
    switch (cpuType)
    {
        /* 68K CPUs (from gestaltProcessorType) */
        case gestaltCPU68000: return "68000";
        case gestaltCPU68010: return "68010";
        case gestaltCPU68020: return "68020";
        case gestaltCPU68030: return "68030";
        case gestaltCPU68040: return "68040";
        /* PowerPC CPUs (from gestaltNativeCPUtype) */
        case 0x0100: return "PowerPC";
        case 0x0101: return "PowerPC 601";
        case 0x0103: return "PowerPC 603";
        case 0x0104: return "PowerPC 604";
        case 0x0106: return "PowerPC 603e";
        case 0x0107: return "PowerPC 603ev";
        case 0x0108: return "PowerPC G3";
        case 0x0109: return "PowerPC 604e";
        case 0x010A: return "PowerPC 604ev";
        case 0x010C: return "PowerPC G4";
        case 0x0139: return "PowerPC G4+";
        case 0x013C: return "PowerPC G5";
        default:     return "Unknown CPU";
    }
}

/*------------------------------------------------------------------------------
    FPU type name lookup
------------------------------------------------------------------------------*/
static const char* GetFPUName(short fpuType, Boolean isPPC)
{
    if (isPPC)
        return "Built-in";
    switch (fpuType)
    {
        case gestaltNoFPU:    return "None";
        case gestalt68881:    return "68881";
        case gestalt68882:    return "68882";
        case gestalt68040FPU: return "68040 built-in";
        default:              return "Unknown";
    }
}

/*------------------------------------------------------------------------------
    SystemDiscovery_DetectHardware
------------------------------------------------------------------------------*/
OSErr SystemDiscovery_DetectHardware(void)
{
    long gestaltResult;
    OSErr err;
    Size growBytes;
    short major, minor, patch;

    /* Clear hardware info */
    memset(&gApp.hardware, 0, sizeof(HardwareInfo));

    /* CPU Type - check architecture first */
    {
        long sysArch;
        long nativeCPU;

        sysArch = 0;
        err = Gestalt(0x73797361L, &sysArch);  /* gestaltSysArchitecture = 'sysa' */
        if (err == noErr && sysArch == 2)  /* gestaltPowerPC = 2 */
        {
            /* PowerPC Mac */
            gApp.hardware.hasFPU = true;  /* All PPC CPUs have FPU */
            nativeCPU = 0;
            err = Gestalt(0x63707574L, &nativeCPU);  /* gestaltNativeCPUtype = 'cput' */
            if (err == noErr)
                gApp.hardware.cpuType = (short)nativeCPU;
            else
                gApp.hardware.cpuType = 0x0100;  /* Generic PPC */
        }
        else
        {
            /* 68K Mac */
            err = Gestalt(gestaltProcessorType, &gestaltResult);
            if (err == noErr)
                gApp.hardware.cpuType = (short)gestaltResult;
        }
    }

    /* FPU Type (68K only - PPC FPU already set above) */
    if (!gApp.hardware.hasFPU)
    {
        err = Gestalt(gestaltFPUType, &gestaltResult);
        if (err == noErr)
        {
            gApp.hardware.fpuType = (short)gestaltResult;
            gApp.hardware.hasFPU = (gApp.hardware.fpuType != gestaltNoFPU);
        }
    }


    /* Physical RAM */
    err = Gestalt(gestaltPhysicalRAMSize, &gestaltResult);
    if (err == noErr)
        gApp.hardware.physicalRAM = gestaltResult;

    /* Available RAM (largest contiguous free block) */
    gApp.hardware.availableRAM = TempMaxMem(&growBytes);

    /* Machine Name */
    err = Gestalt(gestaltMachineType, &gestaltResult);
    if (err == noErr) {
        /* Map machine type to name */
        switch (gestaltResult)
        {
            case gestaltClassic:        SafeStringCopy(gApp.hardware.machineName, "Macintosh Classic", 64); break;
            case gestaltMacSE:          SafeStringCopy(gApp.hardware.machineName, "Macintosh SE", 64); break;
            case gestaltMacSE030:       SafeStringCopy(gApp.hardware.machineName, "Macintosh SE/30", 64); break;
            case gestaltMacPlus:        SafeStringCopy(gApp.hardware.machineName, "Macintosh Plus", 64); break;
            case gestaltMacII:          SafeStringCopy(gApp.hardware.machineName, "Macintosh II", 64); break;
            case gestaltMacIIx:         SafeStringCopy(gApp.hardware.machineName, "Macintosh IIx", 64); break;
            case gestaltMacIIcx:        SafeStringCopy(gApp.hardware.machineName, "Macintosh IIcx", 64); break;
            case gestaltMacIIci:        SafeStringCopy(gApp.hardware.machineName, "Macintosh IIci", 64); break;
            case gestaltMacIIsi:        SafeStringCopy(gApp.hardware.machineName, "Macintosh IIsi", 64); break;
            case gestaltMacIIfx:        SafeStringCopy(gApp.hardware.machineName, "Macintosh IIfx", 64); break;
            case gestaltMacLC:          SafeStringCopy(gApp.hardware.machineName, "Macintosh LC", 64); break;
            case gestaltMacLCII:        SafeStringCopy(gApp.hardware.machineName, "Macintosh LC II", 64); break;
            case gestaltMacLCIII:       SafeStringCopy(gApp.hardware.machineName, "Macintosh LC III", 64); break;
            case gestaltMacQuadra700:   SafeStringCopy(gApp.hardware.machineName, "Quadra 700", 64); break;
            case gestaltMacQuadra900:   SafeStringCopy(gApp.hardware.machineName, "Quadra 900", 64); break;
            case gestaltMacQuadra950:   SafeStringCopy(gApp.hardware.machineName, "Quadra 950", 64); break;
            case gestaltPowerBook170:   SafeStringCopy(gApp.hardware.machineName, "PowerBook 170", 64); break;
            case gestaltPowerBook180:   SafeStringCopy(gApp.hardware.machineName, "PowerBook 180", 64); break;
            default:
                if (gApp.hardware.cpuType >= 0x0100)
                    sprintf(gApp.hardware.machineName, "Power Macintosh (type %ld)", gestaltResult);
                else
                    sprintf(gApp.hardware.machineName, "Macintosh (type %ld)", gestaltResult);
                break;
        }
    } else {
        SafeStringCopy(gApp.hardware.machineName, "Unknown Mac", 64);
    }

    /* System Version */
    err = Gestalt(gestaltSystemVersion, &gestaltResult);
    if (err == noErr) {
        major = (gestaltResult >> 8) & 0xF;
        minor = (gestaltResult >> 4) & 0xF;
        patch = gestaltResult & 0xF;
        sprintf(gApp.hardware.systemVersion, "%d.%d.%d", major, minor, patch);
    } else {
        SafeStringCopy(gApp.hardware.systemVersion, "Unknown", 32);
    }

    return noErr;
}

/*------------------------------------------------------------------------------
    SystemDiscovery_GetSummary
------------------------------------------------------------------------------*/
void SystemDiscovery_GetSummary(char *buffer, short maxLen)
{
    Size growBytes;
    long currentFree;

    (void)maxLen;

    /* Get current free temp memory (reflects post-arena state) */
    currentFree = (long)TempMaxMem(&growBytes);

    sprintf(buffer, "%s | %s - FPU: %s | %ldMB RAM (%ldMB free) | System %s",
            gApp.hardware.machineName,
            GetCPUName((long)gApp.hardware.cpuType),
            gApp.hardware.hasFPU ? GetFPUName(gApp.hardware.fpuType, gApp.hardware.cpuType >= 0x0100) : "None",
            gApp.hardware.physicalRAM / (1024L * 1024L),
            currentFree / (1024L * 1024L),
            gApp.hardware.systemVersion);
}

/*------------------------------------------------------------------------------
    SystemDiscovery_GetSystemVersion
------------------------------------------------------------------------------*/
void SystemDiscovery_GetSystemVersion(char *buffer, short maxLen)
{
    SafeStringCopy(buffer, gApp.hardware.systemVersion, maxLen);
}

/*------------------------------------------------------------------------------
    SystemDiscovery_GetMachineName
------------------------------------------------------------------------------*/
void SystemDiscovery_GetMachineName(char *buffer, short maxLen)
{
    SafeStringCopy(buffer, gApp.hardware.machineName, maxLen);
}
