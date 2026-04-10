/*----------------------------------------------------------------------
    MacSpecsTable.c - Hardware Specifications Lookup Table

    Static table of Macintosh hardware specifications (1984-2001).
    Data sourced from EveryMac.com.

    Total static data: ~50KB (fits easily on any Mac)

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
----------------------------------------------------------------------*/

#pragma segment MacSpecs

#include "MacSpecsTable.h"
#include <string.h>
#include <stdio.h>

/* Forward declarations */
static void ToLowerStr(const char *src, char *dst, short maxLen);
static short FindSubstring(const char *haystack, const char *needle);

/*----------------------------------------------------------------------
    SPECIFICATIONS TABLE

    Each entry is one Macintosh model. Organized chronologically.
    RAM values in KB (1024 KB = 1 MB, 1048576 KB = 1 GB).
----------------------------------------------------------------------*/

static const MacSpec sSpecsTable[] = {

    /* ============= Original / Compact Macs (1984-1990) ============= */

    {
        "Macintosh 128K", "Motorola 68000", 8,
        128, 128,
        "January 24, 1984", "$2,495",
        "compact", "9-inch monochrome 512x342",
        "single 400K floppy drive",
        "none", 0,
        "2 serial (RS-422)",
        "none",
        "Original Macintosh. No SCSI, no expansion, no hard drive."
    },
    {
        "Macintosh 512K", "Motorola 68000", 8,
        512, 512,
        "September 10, 1984", "$3,195",
        "compact", "9-inch monochrome 512x342",
        "single 400K floppy drive",
        "none", 0,
        "2 serial (RS-422)",
        "none",
        "Fat Mac. Same as 128K but with 4x the RAM."
    },
    {
        "Macintosh 512Ke", "Motorola 68000", 8,
        512, 512,
        "April 14, 1986", "$1,999",
        "compact", "9-inch monochrome 512x342",
        "single 800K floppy drive",
        "none", 0,
        "2 serial (RS-422)",
        "none",
        "Enhanced 512K with 800K drive and 128K ROMs."
    },
    {
        "Macintosh Plus", "Motorola 68000", 8,
        1024, 4096,
        "January 16, 1986", "$2,599",
        "compact", "9-inch monochrome 512x342",
        "800K floppy, optional external SCSI HD",
        "none", 0,
        "SCSI (DB-25), 2 serial (Mini DIN-8)",
        "none",
        "First Mac with SCSI and 800K drive. 4 SIMM slots (30-pin, 150ns)."
    },
    {
        "Macintosh SE", "Motorola 68000", 8,
        1024, 4096,
        "March 2, 1987", "$2,898",
        "compact", "9-inch monochrome 512x342",
        "800K or 1.44MB SuperDrive + optional 20/40MB HD",
        "SE PDS", 1,
        "SCSI (DB-25), 2 serial, ADB",
        "none (optional 68881)",
        "First compact Mac with expansion slot (SE PDS) and internal HD."
    },
    {
        "Macintosh SE/30", "Motorola 68030", 16,
        1024, 131072,
        "January 19, 1989", "$4,369",
        "compact", "9-inch monochrome 512x342",
        "1.44MB SuperDrive + 40/80MB SCSI HD",
        "SE/30 PDS (120-pin)", 1,
        "SCSI (DB-25), 2 serial, ADB",
        "Motorola 68882",
        "Most expandable compact Mac. 8 SIMM slots (30-pin). "
        "Supports 68030 accelerator cards only (NOT 68040). Can run A/UX."
    },
    {
        "Macintosh Classic", "Motorola 68000", 8,
        1024, 4096,
        "October 15, 1990", "$999",
        "compact", "9-inch monochrome 512x342",
        "1.44MB SuperDrive + optional 40MB HD",
        "none", 0,
        "SCSI (DB-25), 2 serial, ADB",
        "none",
        "Cheapest Mac ever at launch. Built-in ROM disk boots System 6.0.3."
    },
    {
        "Macintosh Classic II", "Motorola 68030", 16,
        2048, 10240,
        "October 21, 1991", "$1,899",
        "compact", "9-inch monochrome 512x342",
        "1.44MB SuperDrive + 40/80MB SCSI HD",
        "none", 0,
        "SCSI (DB-25), 2 serial, ADB",
        "none",
        "Last compact Mac with 9-inch CRT. 2 SIMM slots (30-pin)."
    },
    {
        "Macintosh Color Classic", "Motorola 68030", 16,
        4096, 10240,
        "February 10, 1993", "$1,389",
        "compact", "10-inch color Sony Trinitron 512x384",
        "1.44MB SuperDrive + 40/80MB SCSI HD",
        "LC PDS", 1,
        "SCSI (DB-25), 2 serial, ADB",
        "none",
        "First compact Mac with color display. 2 SIMM slots (30-pin)."
    },

    /* ============= Macintosh II Series (1987-1993) ============= */

    {
        "Macintosh II", "Motorola 68020", 16,
        1024, 68608,
        "March 2, 1987", "$5,498",
        "desktop", "requires NuBus video card",
        "800K floppy + 40/80MB SCSI HD",
        "NuBus", 6,
        "SCSI (DB-25), 2 serial, ADB",
        "Motorola 68881",
        "First modular Mac. 6 NuBus slots. 8 SIMM slots (30-pin)."
    },
    {
        "Macintosh IIx", "Motorola 68030", 16,
        1024, 131072,
        "September 19, 1988", "$7,769",
        "desktop", "requires NuBus video card",
        "1.44MB SuperDrive + 80MB SCSI HD",
        "NuBus", 6,
        "SCSI (DB-25), 2 serial, ADB",
        "Motorola 68882",
        "First Mac with 68030 and 1.44MB SuperDrive. 8 SIMM slots (30-pin)."
    },
    {
        "Macintosh IIcx", "Motorola 68030", 16,
        1024, 131072,
        "March 7, 1989", "$5,369",
        "desktop", "requires NuBus video card",
        "1.44MB SuperDrive + 80MB SCSI HD",
        "NuBus", 3,
        "SCSI (DB-25), 2 serial, ADB",
        "Motorola 68882",
        "Compact version of IIx. 3 NuBus slots. 8 SIMM slots (30-pin)."
    },
    {
        "Macintosh IIci", "Motorola 68030", 25,
        1024, 131072,
        "September 20, 1989", "$6,269",
        "desktop", "built-in video support",
        "1.44MB SuperDrive + 80MB SCSI HD",
        "NuBus", 3,
        "SCSI (DB-25), 2 serial, ADB",
        "Motorola 68882",
        "8 SIMM slots (30-pin). Built-in video. Cache card slot. "
        "One of the most popular Mac II models."
    },
    {
        "Macintosh IIfx", "Motorola 68030", 40,
        4096, 131072,
        "March 19, 1990", "$9,870",
        "desktop", "requires NuBus video card",
        "1.44MB SuperDrive + 80/160MB SCSI HD",
        "NuBus", 6,
        "SCSI (DB-25), 2 serial, ADB",
        "Motorola 68882",
        "Fastest 68K Mac ever. Uses custom Apple IOP chips for I/O. "
        "8 SIMM slots (64-pin, special IIfx-only SIMMs). 6 NuBus slots."
    },
    {
        "Macintosh IIsi", "Motorola 68030", 20,
        1024, 17408,
        "October 15, 1990", "$3,769",
        "desktop", "built-in video support",
        "1.44MB SuperDrive + 40/80MB SCSI HD",
        "SE/30 PDS", 1,
        "SCSI (DB-25), 2 serial, ADB",
        "optional 68882",
        "Compact desktop. 1 SE/30 PDS slot (NuBus adapter available). "
        "4 SIMM slots (30-pin). Built-in video and sound input."
    },
    {
        "Macintosh IIvi", "Motorola 68030", 16,
        4096, 68608,
        "October 19, 1992", "$2,949",
        "desktop", "built-in video support",
        "1.44MB SuperDrive + 80MB SCSI HD",
        "NuBus", 3,
        "SCSI (DB-25), 2 serial, ADB",
        "optional 68882",
        "3 NuBus slots. 4 SIMM slots (72-pin)."
    },
    {
        "Macintosh IIvx", "Motorola 68030", 32,
        4096, 68608,
        "October 19, 1992", "$3,319",
        "desktop", "built-in video support",
        "1.44MB SuperDrive + 80/230MB SCSI HD",
        "NuBus", 3,
        "SCSI (DB-25), 2 serial, ADB",
        "Motorola 68882",
        "3 NuBus slots. 4 SIMM slots (72-pin). Optional CD-ROM."
    },

    /* ============= LC Series (1990-1993) ============= */

    {
        "Macintosh LC", "Motorola 68020", 16,
        2048, 10240,
        "October 15, 1990", "$2,499",
        "desktop (pizza box)", "supports 12-inch RGB 512x384",
        "1.44MB SuperDrive + 40MB SCSI HD",
        "LC PDS", 1,
        "SCSI (DB-25), 2 serial, ADB, video (DB-15)",
        "none (no FPU socket)",
        "Low-Cost color Mac for education. No FPU. "
        "2 SIMM slots (30-pin). Apple IIe Card compatible."
    },
    {
        "Macintosh LC II", "Motorola 68030", 16,
        4096, 10240,
        "March 23, 1992", "$1,699",
        "desktop (pizza box)", "supports 12-inch RGB 512x384",
        "1.44MB SuperDrive + 40/80MB SCSI HD",
        "LC PDS", 1,
        "SCSI (DB-25), 2 serial, ADB, video (DB-15)",
        "none",
        "LC with 68030 upgrade. 2 SIMM slots (30-pin). No FPU socket."
    },
    {
        "Macintosh LC III", "Motorola 68030", 25,
        4096, 36864,
        "February 10, 1993", "$1,349",
        "desktop (pizza box)", "supports 12-inch or 14-inch color",
        "1.44MB SuperDrive + 40/80/160MB SCSI HD",
        "LC PDS", 1,
        "SCSI (DB-25), 2 serial, ADB, video (DB-15)",
        "optional 68882",
        "1 SIMM slot (72-pin). FPU socket available. "
        "Significant speed improvement over LC II."
    },
    {
        "Macintosh LC 475", "Motorola 68LC040", 25,
        4096, 36864,
        "October 21, 1993", "$899",
        "desktop (pizza box)", "supports 12-inch or 14-inch color",
        "1.44MB SuperDrive + 80/160MB SCSI HD",
        "LC PDS", 1,
        "SCSI (DB-25), 2 serial, ADB, video (DB-15)",
        "none (68LC040 lacks FPU)",
        "1 SIMM slot (72-pin). Same as Quadra 605."
    },

    /* ============= Quadra Series (1991-1995) ============= */

    {
        "Macintosh Quadra 700", "Motorola 68040", 25,
        4096, 68608,
        "October 21, 1991", "$5,699",
        "mini-tower", "built-in video (DB-15)",
        "1.44MB SuperDrive + 80/160MB SCSI HD",
        "NuBus", 2,
        "SCSI, 2 serial, ADB, Ethernet (AAUI)",
        "built-in (68040)",
        "2 NuBus slots + 1 PDS slot. First Mac with built-in Ethernet. "
        "4 SIMM slots (30-pin). 68MB max RAM."
    },
    {
        "Macintosh Quadra 900", "Motorola 68040", 25,
        4096, 262144,
        "October 21, 1991", "$7,199",
        "tower", "requires NuBus video card",
        "1.44MB SuperDrive + 160/400MB SCSI HD",
        "NuBus", 5,
        "SCSI, 2 serial, ADB, Ethernet (AAUI)",
        "built-in (68040)",
        "5 NuBus slots. 16 SIMM slots (30-pin). 256MB max RAM."
    },
    {
        "Macintosh Quadra 950", "Motorola 68040", 33,
        8192, 262144,
        "May 18, 1992", "$7,199",
        "tower", "requires NuBus video card",
        "1.44MB SuperDrive + up to 2 internal SCSI HDs",
        "NuBus", 5,
        "SCSI, 2 serial, ADB, Ethernet (AAUI)",
        "built-in (68040)",
        "Top-of-line 68K tower. 5 NuBus slots. "
        "16 SIMM slots (30-pin). 256MB max RAM."
    },
    {
        "Macintosh Quadra 800", "Motorola 68040", 33,
        8192, 136192,
        "February 10, 1993", "$4,679",
        "mini-tower", "built-in video (DB-15)",
        "1.44MB SuperDrive + 230/500MB SCSI HD",
        "NuBus", 3,
        "SCSI, 2 serial, ADB, Ethernet (AAUI)",
        "built-in (68040)",
        "3 NuBus slots + 1 PDS slot. 4 SIMM slots (72-pin)."
    },
    {
        "Macintosh Quadra 840AV", "Motorola 68040", 40,
        8192, 131072,
        "July 29, 1993", "$3,839",
        "mini-tower", "built-in video (DB-15)",
        "1.44MB SuperDrive + 230/500MB SCSI HD",
        "NuBus", 3,
        "SCSI, 2 serial, ADB, Ethernet, GeoPort",
        "built-in (68040)",
        "AV model with DSP chip (AT&T 3210) for speech recognition "
        "and telephony. 4 SIMM slots (72-pin)."
    },
    {
        "Macintosh Quadra 660AV", "Motorola 68040", 25,
        8192, 68608,
        "July 29, 1993", "$2,579",
        "desktop", "built-in video (DB-15)",
        "1.44MB SuperDrive + 230MB SCSI HD",
        "NuBus (via adapter)", 1,
        "SCSI, 2 serial, ADB, Ethernet, GeoPort",
        "built-in (68040)",
        "AV model with DSP chip. 1 PDS slot (NuBus adapter available). "
        "4 SIMM slots (72-pin)."
    },
    {
        "Macintosh Quadra 605", "Motorola 68LC040", 25,
        4096, 36864,
        "October 21, 1993", "$899",
        "desktop (pizza box)", "built-in video (DB-15)",
        "1.44MB SuperDrive + 80/160MB SCSI HD",
        "LC PDS", 1,
        "SCSI (DB-25), 2 serial, ADB",
        "none (68LC040 lacks FPU)",
        "Smallest Quadra. 1 SIMM slot (72-pin). Same as LC 475."
    },
    {
        "Macintosh Quadra 630", "Motorola 68LC040", 33,
        4096, 36864,
        "July 18, 1994", "$1,199",
        "desktop", "built-in video (DB-15)",
        "1.44MB SuperDrive + 250MB IDE HD",
        "LC PDS + Comm slot", 2,
        "SCSI, serial, ADB",
        "none (68LC040 lacks FPU)",
        "First Mac with IDE hard drive. 1 SIMM slot (72-pin). "
        "Comm slot for Ethernet or modem card."
    },

    /* ============= Performa Series (key models) ============= */

    {
        "Macintosh Performa 475", "Motorola 68LC040", 25,
        4096, 36864,
        "October 21, 1993", "$1,199",
        "desktop (pizza box)", "built-in video (DB-15)",
        "1.44MB SuperDrive + 160MB SCSI HD",
        "LC PDS", 1,
        "SCSI (DB-25), 2 serial, ADB",
        "none (68LC040 lacks FPU)",
        "Consumer version of Quadra 605/LC 475."
    },
    {
        "Macintosh Performa 6400", "PowerPC 603ev", 200,
        16384, 136192,
        "August 1, 1996", "$2,199",
        "tower", "built-in video",
        "1.44MB SuperDrive + 1.6/2.4GB IDE HD, CD-ROM",
        "PCI + L2 cache slot", 2,
        "SCSI, serial, ADB, Ethernet, video in/out",
        "built-in (603ev)",
        "2 PCI slots. 2 DIMM slots (168-pin). TV tuner option."
    },

    /* ============= PowerBook Series (1991-1998) ============= */

    {
        "Macintosh Portable", "Motorola 68000", 16,
        1024, 9216,
        "September 20, 1989", "$6,500",
        "portable", "10-inch active matrix 640x400",
        "1.44MB SuperDrive + 40MB SCSI HD",
        "PDS", 1,
        "SCSI (DB-25), 2 serial, ADB, modem",
        "optional 68881",
        "First battery-powered Mac. 16 lbs. Lead-acid battery (6-12 hrs)."
    },
    {
        "PowerBook 100", "Motorola 68000", 16,
        2048, 8192,
        "October 21, 1991", "$2,299",
        "notebook", "9-inch passive matrix 640x400",
        "1.44MB external SuperDrive + 20/40MB SCSI HD",
        "none", 0,
        "serial, ADB, SCSI (HDI-30), modem",
        "none",
        "First PowerBook. Made by Sony. 2 SIMM slots (30-pin)."
    },
    {
        "PowerBook 140", "Motorola 68030", 16,
        2048, 8192,
        "October 21, 1991", "$2,899",
        "notebook", "10-inch passive matrix 640x400",
        "1.44MB SuperDrive + 20/40MB SCSI HD",
        "none", 0,
        "serial, ADB, SCSI (HDI-30), modem",
        "none",
        "First PowerBook with 68030. 2 SIMM slots (30-pin)."
    },
    {
        "PowerBook 170", "Motorola 68030", 25,
        2048, 8192,
        "October 21, 1991", "$4,599",
        "notebook", "10-inch active matrix 640x400",
        "1.44MB SuperDrive + 40/80MB SCSI HD",
        "none", 0,
        "serial, ADB, SCSI (HDI-30), modem",
        "Motorola 68882",
        "First PowerBook with active matrix display and FPU."
    },
    {
        "PowerBook 520", "Motorola 68LC040", 25,
        4096, 36864,
        "May 16, 1994", "$2,270",
        "notebook", "9.5-inch passive matrix grayscale 640x480",
        "1.44MB SuperDrive + 160/240MB IDE HD",
        "PDS", 1,
        "serial, ADB, SCSI (HDI-30), modem",
        "none (68LC040 lacks FPU)",
        "500 series introduced trackpad (first Apple laptop with trackpad)."
    },
    {
        "PowerBook 540c", "Motorola 68LC040", 33,
        4096, 36864,
        "May 16, 1994", "$4,840",
        "notebook", "9.5-inch active matrix color 640x480",
        "1.44MB SuperDrive + 320/500MB IDE HD",
        "PDS", 1,
        "serial, ADB, SCSI (HDI-30), Ethernet, modem",
        "none (68LC040 lacks FPU)",
        "First PowerBook with color active matrix and Ethernet."
    },
    {
        "PowerBook G3 (Wallstreet)", "PowerPC 750 (G3)", 233,
        32768, 524288,
        "May 6, 1998", "$2,299",
        "notebook", "13.3-inch active matrix color 1024x768",
        "1.44MB SuperDrive + 2-8GB IDE HD, CD/DVD",
        "PCI (CardBus)", 2,
        "serial, ADB, SCSI (HDI-30), Ethernet, USB, modem",
        "built-in (G3)",
        "2 CardBus (PC Card) slots. Up to 512MB RAM. 14.1-inch option."
    },

    /* ============= Power Macintosh (1994-2001) ============= */

    {
        "Power Macintosh 6100/60", "PowerPC 601", 60,
        8192, 73728,
        "March 14, 1994", "$1,819",
        "desktop (pizza box)", "built-in video (HDI-45)",
        "1.44MB SuperDrive + 160/250MB SCSI HD",
        "PDS (NuBus adapter)", 1,
        "SCSI, serial, ADB, Ethernet",
        "built-in (601)",
        "First Power Macintosh. LC-style pizza box case. "
        "2 SIMM slots (72-pin). 72MB max RAM."
    },
    {
        "Power Macintosh 7100/66", "PowerPC 601", 66,
        8192, 136192,
        "March 14, 1994", "$2,899",
        "desktop", "built-in video",
        "1.44MB SuperDrive + 250/500MB SCSI HD",
        "NuBus", 3,
        "SCSI, serial, ADB, Ethernet",
        "built-in (601)",
        "3 NuBus slots + 1 PDS slot. 4 SIMM slots (72-pin)."
    },
    {
        "Power Macintosh 8100/80", "PowerPC 601", 80,
        8192, 264192,
        "March 14, 1994", "$4,249",
        "mini-tower", "built-in video",
        "1.44MB SuperDrive + 250/500MB SCSI HD, CD-ROM",
        "NuBus", 3,
        "SCSI, serial, ADB, Ethernet",
        "built-in (601)",
        "3 NuBus slots + 1 PDS slot. 8 SIMM slots (72-pin). 264MB max."
    },
    {
        "Power Macintosh 7200/75", "PowerPC 601", 75,
        8192, 262144,
        "August 8, 1995", "$1,699",
        "desktop", "built-in video",
        "1.44MB SuperDrive + 500MB SCSI HD",
        "PCI", 3,
        "SCSI, serial, ADB, Ethernet (AAUI)",
        "built-in (601)",
        "First PCI Power Mac. 3 PCI slots. 4 DIMM slots (168-pin)."
    },
    {
        "Power Macintosh 7500/100", "PowerPC 601", 100,
        16384, 1048576,
        "August 8, 1995", "$2,699",
        "desktop", "built-in video",
        "1.44MB SuperDrive + 500MB SCSI HD",
        "PCI", 3,
        "SCSI, serial, ADB, Ethernet (AAUI), video",
        "built-in (601)",
        "3 PCI slots. 8 DIMM slots (168-pin). Upgradeable CPU daughter card. "
        "Up to 1GB RAM."
    },
    {
        "Power Macintosh 8500/120", "PowerPC 604", 120,
        16384, 1048576,
        "August 8, 1995", "$3,699",
        "mini-tower", "built-in video",
        "1.44MB SuperDrive + 1GB SCSI HD, CD-ROM",
        "PCI", 3,
        "SCSI, serial, ADB, Ethernet, video in/out",
        "built-in (604)",
        "3 PCI slots. 8 DIMM slots (168-pin). AV features. Up to 1GB RAM."
    },
    {
        "Power Macintosh 9500/120", "PowerPC 604", 120,
        16384, 1572864,
        "May 1, 1995", "$4,999",
        "tower", "requires PCI video card",
        "1.44MB SuperDrive + 1/2GB SCSI HD, CD-ROM",
        "PCI", 6,
        "SCSI, serial, ADB, Ethernet",
        "built-in (604)",
        "6 PCI slots. 12 DIMM slots (168-pin). Up to 1.5GB RAM. "
        "Dual-CPU capable."
    },
    {
        "Power Macintosh G3 (B&W)", "PowerPC 750 (G3)", 350,
        65536, 1048576,
        "January 5, 1999", "$1,599",
        "tower", "built-in ATI Rage 128",
        "1.44MB SuperDrive + 6-12GB IDE HD, CD/DVD",
        "PCI", 4,
        "USB, FireWire, Ethernet, serial, SCSI (optional)",
        "built-in (G3)",
        "Blue and White tower. 3 PCI + 1 AGP slot. 4 DIMM slots (PC100). "
        "First Mac with FireWire and USB."
    },
    {
        "Power Mac G4 (AGP)", "PowerPC 7400 (G4)", 400,
        65536, 2097152,
        "October 13, 1999", "$1,599",
        "tower", "built-in ATI Rage 128 Pro",
        "1.44MB SuperDrive + 10-20GB IDE HD, DVD-ROM",
        "PCI + AGP", 4,
        "USB, FireWire, Ethernet, modem",
        "built-in (G4)",
        "3 PCI + 1 AGP slot. 4 DIMM slots (PC100/133). "
        "Velocity Engine (AltiVec). Up to 2GB RAM."
    },
    {
        "Power Mac G4 Cube", "PowerPC 7400 (G4)", 450,
        65536, 1572864,
        "July 19, 2000", "$1,799",
        "cube", "built-in ATI Rage 128 Pro",
        "20GB IDE HD, slot-loading DVD-ROM",
        "AGP", 1,
        "USB (2), FireWire (2), Ethernet, modem",
        "built-in (G4)",
        "Fanless design in 8-inch acrylic cube. 3 DIMM slots. "
        "No floppy drive."
    },

    /* ============= iMac (1998-2001) ============= */

    {
        "iMac G3 (Bondi Blue)", "PowerPC 750 (G3)", 233,
        32768, 262144,
        "August 15, 1998", "$1,299",
        "all-in-one", "15-inch CRT 1024x768",
        "4GB IDE HD, CD-ROM",
        "mezzanine", 1,
        "USB (2), Ethernet (10/100), modem, IrDA",
        "built-in (G3)",
        "First iMac. No floppy, no SCSI, no ADB, no serial. "
        "2 DIMM slots (SO-DIMM). Translucent case."
    },

    /* ============= Special Models ============= */

    {
        "Macintosh TV", "Motorola 68030", 32,
        5120, 8192,
        "October 25, 1993", "$2,079",
        "desktop", "14-inch Sony Trinitron color CRT 512x384",
        "1.44MB SuperDrive + 160MB SCSI HD",
        "LC PDS", 1,
        "SCSI, serial, ADB, cable TV input (F-connector)",
        "none",
        "Black case. Built-in TV/cable tuner card. Only 10,000 produced."
    },
    {
        "Twentieth Anniversary Mac", "PowerPC 603e", 250,
        32768, 131072,
        "March 20, 1997", "$7,499",
        "all-in-one", "12.1-inch active matrix color LCD 800x600",
        "2GB IDE HD, CD-ROM, TV tuner",
        "PCI", 1,
        "serial, ADB, Ethernet, modem, S-video in, Bose sound",
        "built-in (603e)",
        "20th anniversary limited edition. Leather palm rest. "
        "Bose sound system. Concierge delivery."
    }
};

#define kNumSpecs (sizeof(sSpecsTable) / sizeof(sSpecsTable[0]))

/*----------------------------------------------------------------------
    ALIASES TABLE

    Maps common name variations to indices in sSpecsTable.
----------------------------------------------------------------------*/

static const MacSpecAlias sAliases[] = {
    /* Original / Compact */
    {"128k",               0},
    {"mac 128k",           0},
    {"mac 128",            0},
    {"512k",               1},
    {"fat mac",            1},
    {"mac 512k",           1},
    {"512ke",              2},
    {"mac 512ke",          2},
    {"plus",               3},
    {"mac plus",           3},
    {"macintosh se ",      4},  /* trailing space to avoid matching se/30 */
    {"mac se ",            4},
    {"se/30",              5},
    {"se30",               5},
    {"mac se/30",          5},
    {"classic ii",         7},
    {"classic 2",          7},
    {"mac classic ii",     7},
    {"color classic",      8},
    {"colour classic",     8},
    {"cc",                 8},
    {"mystic",             8},

    /* Mac II series */
    {"macintosh ii ",      9},  /* trailing space */
    {"mac ii ",            9},
    {"iix",                10},
    {"mac iix",            10},
    {"iicx",               11},
    {"mac iicx",           11},
    {"iici",               12},
    {"mac iici",           12},
    {"iifx",               13},
    {"mac iifx",           13},
    {"iisi",               14},
    {"mac iisi",           14},
    {"iivi",               15},
    {"mac iivi",           15},
    {"iivx",               16},
    {"mac iivx",           16},

    /* LC series */
    {"lc ii",              18},
    {"mac lc ii",          18},
    {"lc 2",               18},
    {"lc iii",             19},
    {"mac lc iii",         19},
    {"lc 3",               19},
    {"lc 475",             20},
    {"mac lc 475",         20},

    /* Quadra series */
    {"quadra 700",         21},
    {"q700",               21},
    {"quadra 900",         22},
    {"q900",               22},
    {"quadra 950",         23},
    {"q950",               23},
    {"quadra 800",         24},
    {"q800",               24},
    {"quadra 840av",       25},
    {"840av",              25},
    {"quadra 660av",       26},
    {"660av",              26},
    {"quadra 605",         27},
    {"q605",               27},
    {"quadra 630",         28},
    {"q630",               28},

    /* Performa */
    {"performa 475",       29},
    {"performa 6400",      30},

    /* Portable / PowerBook */
    {"mac portable",       31},
    {"portable",           31},
    {"powerbook 100",      32},
    {"pb 100",             32},
    {"powerbook 140",      33},
    {"pb 140",             33},
    {"powerbook 170",      34},
    {"pb 170",             34},
    {"powerbook 520",      35},
    {"pb 520",             35},
    {"powerbook 540",      36},
    {"pb 540",             36},
    {"powerbook 540c",     36},
    {"wallstreet",         37},
    {"powerbook g3",       37},

    /* Power Macintosh */
    {"6100",               38},
    {"pm 6100",            38},
    {"power mac 6100",     38},
    {"7100",               39},
    {"pm 7100",            39},
    {"power mac 7100",     39},
    {"8100",               40},
    {"pm 8100",            40},
    {"power mac 8100",     40},
    {"7200",               41},
    {"pm 7200",            41},
    {"power mac 7200",     41},
    {"7500",               42},
    {"pm 7500",            42},
    {"power mac 7500",     42},
    {"8500",               43},
    {"pm 8500",            43},
    {"power mac 8500",     43},
    {"9500",               44},
    {"pm 9500",            44},
    {"power mac 9500",     44},
    {"b&w g3",             45},
    {"blue and white",     45},
    {"power mac g3",       45},
    {"g4 agp",             46},
    {"power mac g4",       46},
    {"g4 cube",            47},
    {"cube",               47},

    /* iMac */
    {"bondi blue",         48},
    {"imac g3",            48},
    {"imac",               48},

    /* Special */
    {"macintosh tv",       49},
    {"mac tv",             49},
    {"tam",                50},
    {"twentieth anniversary", 50},
    {"spartacus",          50},
};

#define kNumAliases (sizeof(sAliases) / sizeof(sAliases[0]))

/*----------------------------------------------------------------------
    ToLowerStr - Convert string to lowercase
----------------------------------------------------------------------*/
static void ToLowerStr(const char *src, char *dst, short maxLen)
{
    short i;

    for (i = 0; i < maxLen - 1 && src[i] != '\0'; i++) {
        if (src[i] >= 'A' && src[i] <= 'Z') {
            dst[i] = src[i] + 32;
        } else {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

/*----------------------------------------------------------------------
    FindSubstring - Find needle in haystack, return position or -1
----------------------------------------------------------------------*/
static short FindSubstring(const char *haystack, const char *needle)
{
    short hLen;
    short nLen;
    short i;

    hLen = (short)strlen(haystack);
    nLen = (short)strlen(needle);

    if (nLen == 0 || nLen > hLen) {
        return -1;
    }

    for (i = 0; i <= hLen - nLen; i++) {
        if (strncmp(&haystack[i], needle, nLen) == 0) {
            return i;
        }
    }
    return -1;
}

/*----------------------------------------------------------------------
    MacSpecs_Lookup - Find a Mac model in the specs table
----------------------------------------------------------------------*/
const MacSpec* MacSpecs_Lookup(const char *query)
{
    char lowerQuery[256];
    char lowerName[kSpecNameLen];
    short bestIndex;
    short bestLen;
    short i;
    short nameLen;

    if (query == nil || query[0] == '\0') {
        return nil;
    }

    ToLowerStr(query, lowerQuery, 256);
    bestIndex = -1;
    bestLen = 0;

    /* Check aliases first (longest match wins) */
    for (i = 0; i < kNumAliases; i++) {
        nameLen = (short)strlen(sAliases[i].alias);
        if (nameLen > bestLen && FindSubstring(lowerQuery,
                                               sAliases[i].alias) >= 0) {
            bestLen = nameLen;
            bestIndex = sAliases[i].specIndex;
        }
    }

    /* Check full model names */
    for (i = 0; i < (short)kNumSpecs; i++) {
        ToLowerStr(sSpecsTable[i].name, lowerName, kSpecNameLen);
        nameLen = (short)strlen(lowerName);
        if (nameLen > bestLen && FindSubstring(lowerQuery, lowerName) >= 0) {
            bestLen = nameLen;
            bestIndex = i;
        }
    }

    if (bestIndex >= 0 && bestIndex < (short)kNumSpecs) {
        return &sSpecsTable[bestIndex];
    }
    return nil;
}

/*----------------------------------------------------------------------
    MacSpecs_FormatAnswer - Format specs as natural language answer
----------------------------------------------------------------------*/
short MacSpecs_FormatAnswer(const MacSpec *spec, char *output, short maxLen)
{
    char ramMin[16];
    char ramMax[16];
    short pos;

    if (spec == nil || output == nil || maxLen < 64) {
        return 0;
    }

    /* Format RAM strings */
    if (spec->ramMinKB < 1024) {
        sprintf(ramMin, "%ldKB", spec->ramMinKB);
    } else if (spec->ramMinKB < 1048576) {
        sprintf(ramMin, "%ldMB", spec->ramMinKB / 1024);
    } else {
        sprintf(ramMin, "%ldGB", spec->ramMinKB / 1048576);
    }

    if (spec->ramMaxKB < 1024) {
        sprintf(ramMax, "%ldKB", spec->ramMaxKB);
    } else if (spec->ramMaxKB < 1048576) {
        sprintf(ramMax, "%ldMB", spec->ramMaxKB / 1024);
    } else {
        sprintf(ramMax, "%ldGB", spec->ramMaxKB / 1048576);
    }

    /* Build the answer */
    pos = sprintf(output,
        "The %s has a %s processor at %d MHz",
        spec->name, spec->processor, spec->clockMHz);

    /* FPU */
    if (strcmp(spec->fpu, "none") != 0 &&
        strcmp(spec->fpu, "none (no FPU socket)") != 0 &&
        strcmp(spec->fpu, "none (68LC040 lacks FPU)") != 0) {
        pos += sprintf(output + pos, " with %s", spec->fpu);
    }

    /* RAM */
    if (spec->ramMinKB == spec->ramMaxKB) {
        pos += sprintf(output + pos, ". It has %s of RAM", ramMin);
    } else {
        pos += sprintf(output + pos,
            ". RAM ranges from %s to %s", ramMin, ramMax);
    }

    /* Release */
    pos += sprintf(output + pos,
        ". Released %s at %s", spec->release, spec->price);

    /* Display */
    pos += sprintf(output + pos, ". Display: %s", spec->display);

    /* Storage */
    pos += sprintf(output + pos, ". Storage: %s", spec->storage);

    /* Expansion */
    if (spec->expansionSlots > 0) {
        pos += sprintf(output + pos,
            ". Expansion: %d %s slot%s",
            spec->expansionSlots, spec->bus,
            spec->expansionSlots > 1 ? "s" : "");
    }

    /* Notes */
    if (spec->notes[0] != '\0') {
        pos += sprintf(output + pos, ". %s", spec->notes);
    }

    /* Safety truncation */
    if (pos >= maxLen) {
        output[maxLen - 1] = '\0';
        pos = maxLen - 1;
    }

    return pos;
}

/*----------------------------------------------------------------------
    MacSpecs_GetCount - Return number of models in table
----------------------------------------------------------------------*/
short MacSpecs_GetCount(void)
{
    return (short)kNumSpecs;
}
