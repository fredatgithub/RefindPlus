/*
 * BootMaster/launch_legacy.c
 * Functions related to Legacy (BIOS) Booting
 *
 * Copyright (c) 2006 Christoph Pfisterer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Modifications copyright (c) 2012-2015 Roderick W. Smith
 *
 * Modifications distributed under the terms of the GNU General Public
 * License (GPL) version 3 (GPLv3), or (at your option) any later version.
 *
 */
/*
 * Modified for RefindPlus
 * Copyright (c) 2020-2022 Dayo Akanji (sf.net/u/dakanji/profile)
 *
 * Modifications distributed under the preceding terms.
 */

#include "global.h"
#include "icns.h"
#include "lib.h"
#include "menu.h"
#include "mystrings.h"
#include "screenmgt.h"
#include "launch_legacy.h"
#include "../include/refit_call_wrapper.h"
#include "../include/syslinux_mbr.h"
#include "../EfiLib/BdsHelper.h"
#include "../EfiLib/legacy.h"
#include "../include/Handle.h"

extern BOOLEAN            IsBoot;
extern REFIT_MENU_SCREEN *MainMenu;


#ifndef __MAKEWITH_GNUEFI
#define LibLocateHandle gBS->LocateHandleBuffer
#define DevicePathProtocol gEfiDevicePathProtocolGuid
#endif

#define MAX_DISCOVERED_PATHS (16)

// early 2006 Core Duo / Core Solo models
static UINT8 LegacyLoaderDevicePath1Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF9, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};

// mid-2006 Mac Pro (and probably other Core 2 models)
static UINT8 LegacyLoaderDevicePath2Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF7, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};

// mid-2007 MBP ("Santa Rosa" based models)
static UINT8 LegacyLoaderDevicePath3Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF8, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};

// early-2008 MBA
static UINT8 LegacyLoaderDevicePath4Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xC0, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xF8, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};

// late-2008 MB/MBP (NVidia chipset)
static UINT8 LegacyLoaderDevicePath5Data[] = {
    0x01, 0x03, 0x18, 0x00, 0x0B, 0x00, 0x00, 0x00,
    0x00, 0x40, 0xCB, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};

static EFI_DEVICE_PATH *LegacyLoaderList[] = {
    (EFI_DEVICE_PATH *) LegacyLoaderDevicePath1Data,
    (EFI_DEVICE_PATH *) LegacyLoaderDevicePath2Data,
    (EFI_DEVICE_PATH *) LegacyLoaderDevicePath3Data,
    (EFI_DEVICE_PATH *) LegacyLoaderDevicePath4Data,
    (EFI_DEVICE_PATH *) LegacyLoaderDevicePath5Data,
    NULL
};

static
UINT8 LegacyLoaderMediaPathData[] = {
    0x04, 0x06, 0x14, 0x00, 0xEB, 0x85, 0x05, 0x2B,
    0xB8, 0xD8, 0xA9, 0x49, 0x8B, 0x8C, 0xE2, 0x1B,
    0x01, 0xAE, 0xF2, 0xB7, 0x7F, 0xFF, 0x04, 0x00,
};

static
EFI_DEVICE_PATH *LegacyLoaderMediaPath = (EFI_DEVICE_PATH *) LegacyLoaderMediaPathData;

static
EFI_GUID AppleVariableVendorID = { 0x7C436110, 0xAB2A, 0x4BBB, \
    { 0xA8, 0x80, 0xFE, 0x41, 0x99, 0x5C, 0x9F, 0x82 } };

BOOLEAN FirstLegacyScan = TRUE;


static
EFI_STATUS ActivateMbrPartition (
    IN EFI_BLOCK_IO *BlockIO,
    IN UINTN         PartitionIndex
) {
    EFI_STATUS           Status;
    UINT8                SectorBuffer[512];
    MBR_PARTITION_INFO  *MbrTable, *EMbrTable;
    UINT32               ExtBase, ExtCurrent, NextExtCurrent;
    UINTN                LogicalPartitionIndex = 4;
    UINTN                i;
    BOOLEAN              HaveBootCode;

    // Read MBR
    Status = REFIT_CALL_5_WRAPPER(
        BlockIO->ReadBlocks, BlockIO,
        BlockIO->Media->MediaId, 0,
        512, SectorBuffer
    );
    if (EFI_ERROR(Status)) {
        return Status;
    }

    if (*((UINT16 *)(SectorBuffer + 510)) != 0xaa55) {
        // Safety measure #1
        return EFI_NOT_FOUND;
    }

    // Add boot code if necessary
    HaveBootCode = FALSE;
    for (i = 0; i < MBR_BOOTCODE_SIZE; i++) {
        if (SectorBuffer[i] != 0) {
            HaveBootCode = TRUE;
            break;
        }
    } // for

    if (!HaveBootCode) {
        // No boot code found in the MBR, add the syslinux MBR code
        SetMem (SectorBuffer, MBR_BOOTCODE_SIZE, 0);
        CopyMem (SectorBuffer, syslinux_mbr, SYSLINUX_MBR_SIZE);
    }

    // Set the partition active
    MbrTable = (MBR_PARTITION_INFO *)(SectorBuffer + 446);
    ExtBase = 0;
    for (i = 0; i < 4; i++) {
        if (MbrTable[i].Flags != 0x00 && MbrTable[i].Flags != 0x80) {
            return EFI_NOT_FOUND;   // safety measure #2
        }

        if (i == PartitionIndex) {
            MbrTable[i].Flags = 0x80;
        }
        else if (PartitionIndex >= 4 && IS_EXTENDED_PART_TYPE(MbrTable[i].Type)) {
            MbrTable[i].Flags = 0x80;
            ExtBase = MbrTable[i].StartLBA;
        }
        else {
            MbrTable[i].Flags = 0x00;
        }
    }

    // Write MBR
    Status = REFIT_CALL_5_WRAPPER(
        BlockIO->WriteBlocks, BlockIO,
        BlockIO->Media->MediaId, 0,
        512, SectorBuffer
    );
    if (EFI_ERROR(Status)) {
        // Early Return
        return Status;
    }

    if (PartitionIndex >= 4) {
        // We have to activate a logical partition ... so walk the EMBR chain

        // NOTE: ExtBase was set above while looking at the MBR table
        for (ExtCurrent = ExtBase; ExtCurrent; ExtCurrent = NextExtCurrent) {
            // Read current EMBR
            Status = REFIT_CALL_5_WRAPPER(
                BlockIO->ReadBlocks, BlockIO,
                BlockIO->Media->MediaId, ExtCurrent,
                512, SectorBuffer
            );
            if (EFI_ERROR(Status)) {
                return Status;
            }

            if (*((UINT16 *)(SectorBuffer + 510)) != 0xaa55) {
                return EFI_NOT_FOUND;  // safety measure #3
            }

            // Scan EMBR, set appropriate partition active
            EMbrTable = (MBR_PARTITION_INFO *)(SectorBuffer + 446);
            NextExtCurrent = 0;
            for (i = 0; i < 4; i++) {
                if (EMbrTable[i].Flags != 0x00 && EMbrTable[i].Flags != 0x80) {
                    return EFI_NOT_FOUND;   // safety measure #4
                }

                if (EMbrTable[i].StartLBA == 0 || EMbrTable[i].Size == 0) {
                    break;
                }

                if (IS_EXTENDED_PART_TYPE(EMbrTable[i].Type)) {
                    // link to next EMBR
                    NextExtCurrent = ExtBase + EMbrTable[i].StartLBA;
                    EMbrTable[i].Flags = (PartitionIndex >= LogicalPartitionIndex) ? 0x80 : 0x00;
                    break;
                }
                else {
                    // logical partition
                    EMbrTable[i].Flags = (PartitionIndex == LogicalPartitionIndex) ? 0x80 : 0x00;
                    LogicalPartitionIndex++;
                }
            }

            // Write current EMBR
            Status = REFIT_CALL_5_WRAPPER(
                BlockIO->WriteBlocks, BlockIO,
                BlockIO->Media->MediaId, ExtCurrent,
                512, SectorBuffer
            );
            if (EFI_ERROR(Status)) {
                return Status;
            }

            if (PartitionIndex < LogicalPartitionIndex) {
                break;  // stop the loop, no need to touch further EMBRs
            }
        } // for
    } // if PartitionIndex

    return EFI_SUCCESS;
} // static EFI_STATUS ActivateMbrPartition()

static
EFI_STATUS WriteBootDiskHint (
    IN EFI_DEVICE_PATH *WholeDiskDevicePath
){
   EFI_STATUS Status;

   Status = EfivarSetRaw (
       &AppleVariableVendorID, L"BootCampHD",
       WholeDiskDevicePath, GetDevicePathSize (WholeDiskDevicePath), TRUE
   );
   if (EFI_ERROR(Status)) {
       return Status;
   }

   return EFI_SUCCESS;
} // EFI_STATUS WriteBootDiskHint

//
// firmware device path discovery
//

static
VOID ExtractLegacyLoaderPaths (
    EFI_DEVICE_PATH **PathList,
    UINTN             MaxPaths,
    EFI_DEVICE_PATH **HardcodedPathList
) {
    EFI_STATUS          Status;
    UINTN               PathIndex;
    UINTN               PathCount   = 0;
    UINTN               HandleCount = 0;
    UINTN               HandleIndex;
    UINTN               HardcodedIndex;
    BOOLEAN             Seen;
    EFI_HANDLE         *Handles;
    EFI_HANDLE          Handle;
    EFI_DEVICE_PATH    *DevicePath;
    EFI_LOADED_IMAGE   *LoadedImage;

    MaxPaths--;  // leave space for the terminating NULL pointer

    // Get all LoadedImage handles
    Status = LibLocateHandle (
        ByProtocol,
        &LoadedImageProtocol, NULL,
        &HandleCount, &Handles
    );

    if (CheckError (Status, L"while listing LoadedImage handles")) {
        if (HardcodedPathList) {
            for (HardcodedIndex = 0; HardcodedPathList[HardcodedIndex] && PathCount < MaxPaths; HardcodedIndex++) {
                PathList[PathCount++] = HardcodedPathList[HardcodedIndex];
            }
        }

        PathList[PathCount] = NULL;

        return;
    }

    for (HandleIndex = 0; HandleIndex < HandleCount && PathCount < MaxPaths; HandleIndex++) {
        Handle = Handles[HandleIndex];

        Status = REFIT_CALL_3_WRAPPER(
            gBS->HandleProtocol, Handle,
            &LoadedImageProtocol, (VOID **) &LoadedImage
        );
        if (EFI_ERROR(Status)) {
            // This can only happen if the firmware scewed up ... ignore it.
            continue;
        }

        Status = REFIT_CALL_3_WRAPPER(
            gBS->HandleProtocol, LoadedImage->DeviceHandle,
            &DevicePathProtocol, (VOID **) &DevicePath
        );
        if (EFI_ERROR(Status)) {
            // This happens ... ignore it.
            continue;
        }

        // Only grab memory range nodes
        if (DevicePathType (DevicePath) != HARDWARE_DEVICE_PATH ||
            DevicePathSubType (DevicePath) != HW_MEMMAP_DP
        ) {
            continue;
        }

        // Check if we have this device path in the list already
        // WARNING: This assumes the first node in the device path is unique!
        Seen = FALSE;
        for (PathIndex = 0; PathIndex < PathCount; PathIndex++) {
            if (DevicePathNodeLength (DevicePath) != DevicePathNodeLength (PathList[PathIndex])) {
                continue;
            }

            if (CompareMem (
                    DevicePath, PathList[PathIndex],
                    DevicePathNodeLength (DevicePath)
                ) == 0
            ) {
                Seen = TRUE;
                break;
            }
        }

        if (Seen) {
            continue;
        }

        PathList[PathCount++] = AppendDevicePath (DevicePath, LegacyLoaderMediaPath);
    }
    MY_FREE_POOL(Handles);

    if (HardcodedPathList) {
        for (HardcodedIndex = 0; HardcodedPathList[HardcodedIndex] && PathCount < MaxPaths; HardcodedIndex++) {
            PathList[PathCount++] = HardcodedPathList[HardcodedIndex];
        }
    }

    PathList[PathCount] = NULL;
} // VOID ExtractLegacyLoaderPaths()

// Launch a BIOS boot loader (Mac mode)
static
EFI_STATUS StartLegacyImageList (
    IN  EFI_DEVICE_PATH **DevicePaths,
    IN  CHAR16           *LoadOptions,
    OUT UINTN            *ErrorInStep
) {
    EFI_STATUS               Status;
    EFI_HANDLE               ChildImageHandle;
    EFI_LOADED_IMAGE        *ChildLoadedImage = NULL;
    UINTN                    DevicePathIndex;
    CHAR16                  *FullLoadOptions = NULL;

    if (ErrorInStep != NULL) {
        *ErrorInStep = 0;
    }

    // Set load options
    if (LoadOptions != NULL) {
        FullLoadOptions = StrDuplicate (LoadOptions);
    }

    Print (
        L"Starting 'Mac-Style' Legacy (BIOS) Loader\nUsing Load Options:- '%s'\n\n",
        FullLoadOptions ? FullLoadOptions : L"NULL"
    );

    // Load the image into memory
    Status = EFI_LOAD_ERROR;  // in case the list is empty
    for (DevicePathIndex = 0; DevicePaths[DevicePathIndex] != NULL; DevicePathIndex++) {
        Status = REFIT_CALL_6_WRAPPER(
            gBS->LoadImage, FALSE,
            SelfImageHandle, DevicePaths[DevicePathIndex],
            NULL, 0, &ChildImageHandle
        );
        if (Status != EFI_NOT_FOUND) {
            break;
        }
    } // for

    if (CheckError (Status, L"While Loading 'Mac-Style' Legacy (BIOS) Loader")) {
        if (ErrorInStep != NULL) {
            *ErrorInStep = 1;
        }

        goto bailout;
    }

    Status = REFIT_CALL_3_WRAPPER(
        gBS->HandleProtocol, ChildImageHandle,
        &LoadedImageProtocol, (VOID **) &ChildLoadedImage
    );
    if (CheckError (Status, L"While Fetching LoadedImageProtocol Handle!!")) {
        if (ErrorInStep != NULL) {
            *ErrorInStep = 2;
        }

        goto bailout_unload;
    }

    ChildLoadedImage->LoadOptions     = (VOID *) FullLoadOptions;
    ChildLoadedImage->LoadOptionsSize = FullLoadOptions
        ? ((UINT32) StrLen (FullLoadOptions) + 1) * sizeof (CHAR16)
        : 0;
    // Turn control over to the image
    // DA-TAG: (optionally) re-enable the EFI watchdog timer!

    // Close open file handles
    #if REFIT_DEBUG > 0
    ALT_LOG(1, LOG_LINE_NORMAL, L"Launching 'Mac-Style' Legacy (BIOS) Loader");
    #endif

    UninitRefitLib();

    #if REFIT_DEBUG > 0
    OUT_TAG();
    #endif
    Status = REFIT_CALL_3_WRAPPER(
        gBS->StartImage, ChildImageHandle,
        NULL, NULL
    );
    // Control returned after child image called 'Exit()'
    if (CheckError (Status, L"Unexpected Return from Loader")) {
        if (ErrorInStep != NULL) {
            *ErrorInStep = 3;
        }
    }
    #if REFIT_DEBUG > 0
    RET_TAG();
    #endif

    // Re-open file handles
    ReinitRefitLib();

bailout_unload:
    // Unload the child image ... We do not care if it works or not
    REFIT_CALL_1_WRAPPER(gBS->UnloadImage, ChildImageHandle);

bailout:
    MY_FREE_POOL(FullLoadOptions);

    return Status;
} // EFI_STATUS StartLegacyImageList()

VOID StartLegacy (
    IN LEGACY_ENTRY *Entry,
    IN CHAR16 *SelectionName
) {
    EFI_STATUS       Status;
    EG_IMAGE        *BootLogoImage;
    UINTN            ErrorInStep = 0;
    EFI_DEVICE_PATH *DiscoveredPathList[MAX_DISCOVERED_PATHS];

    CHAR16 *MsgStrA = NULL;
    CHAR16 *MsgStrB = NULL;

    IsBoot = TRUE;

    #if REFIT_DEBUG > 1
    CHAR16 *FuncTag = L"StartLegacy";
    #endif

    LOG_SEP(L"X");
    LOG_INCREMENT();
    BREAD_CRUMB(L"%s:  1 - START", FuncTag);

    #if REFIT_DEBUG > 0
    ALT_LOG(1, LOG_LINE_NORMAL,
        L"Starting 'Mac-style' Legacy (BIOS) OS:- '%s'",
        SelectionName
    );
    #endif

    BREAD_CRUMB(L"%s:  2", FuncTag);
    BeginExternalScreen (TRUE, L"Booting 'Mac-style' Legacy (BIOS) OS");

    BREAD_CRUMB(L"%s:  3", FuncTag);
    BootLogoImage = LoadOSIcon (Entry->Volume->OSIconName, L"legacy", TRUE);
    BREAD_CRUMB(L"%s:  4", FuncTag);
    if (BootLogoImage != NULL) {
        BREAD_CRUMB(L"%s:  4a 1", FuncTag);
        BltImageAlpha (
            BootLogoImage,
            (ScreenW - BootLogoImage->Width ) >> 1,
            (ScreenH - BootLogoImage->Height) >> 1,
            &StdBackgroundPixel
        );
        BREAD_CRUMB(L"%s:  4a 2", FuncTag);
    }

    BREAD_CRUMB(L"%s:  5", FuncTag);
    if (Entry->Volume->IsMbrPartition) {
        BREAD_CRUMB(L"%s:  5a 1", FuncTag);
        ActivateMbrPartition (
            Entry->Volume->WholeDiskBlockIO,
            Entry->Volume->MbrPartitionIndex
        );
        BREAD_CRUMB(L"%s:  5a 2", FuncTag);
    }

    BREAD_CRUMB(L"%s:  6", FuncTag);
    if (Entry->Volume->DiskKind != DISK_KIND_OPTICAL &&
        Entry->Volume->WholeDiskDevicePath != NULL
    ) {
        BREAD_CRUMB(L"%s:  6a 1", FuncTag);
        WriteBootDiskHint (Entry->Volume->WholeDiskDevicePath);
        BREAD_CRUMB(L"%s:  6a 2", FuncTag);
    }

    BREAD_CRUMB(L"%s:  7", FuncTag);
    ExtractLegacyLoaderPaths (
        DiscoveredPathList,
        MAX_DISCOVERED_PATHS,
        LegacyLoaderList
    );

    BREAD_CRUMB(L"%s:  8", FuncTag);
    StoreLoaderName (SelectionName);

    BREAD_CRUMB(L"%s:  9", FuncTag);
    Status = StartLegacyImageList (
        DiscoveredPathList,
        Entry->LoadOptions,
        &ErrorInStep
    );
    BREAD_CRUMB(L"%s:  10", FuncTag);
    if (Status == EFI_NOT_FOUND) {
        BREAD_CRUMB(L"%s:  10a 1", FuncTag);
        if (ErrorInStep == 1) {
            BREAD_CRUMB(L"%s:  10a 1a 1", FuncTag);
            SwitchToText (FALSE);

            BREAD_CRUMB(L"%s:  10a 1a 2", FuncTag);
            MsgStrA = L"Please make sure you have the latest firmware update installed";
            REFIT_CALL_2_WRAPPER(gST->ConOut->SetAttribute, gST->ConOut, ATTR_ERROR);
            PrintUglyText (MsgStrA, NEXTLINE);
            REFIT_CALL_2_WRAPPER(gST->ConOut->SetAttribute, gST->ConOut, ATTR_BASIC);
            BREAD_CRUMB(L"%s:  10a 1a 3", FuncTag);

            #if REFIT_DEBUG > 0
            LOG_MSG("** WARN: %s", MsgStrA);
            LOG_MSG("\n\n");
            #endif

            BREAD_CRUMB(L"%s:  10a 1a 4", FuncTag);
            PauseForKey();
            BREAD_CRUMB(L"%s:  10a 1a 5", FuncTag);
            SwitchToGraphics();
        }
        else if (ErrorInStep == 3) {
            BREAD_CRUMB(L"%s:  10a 1b 1", FuncTag);
            SwitchToText (FALSE);

            BREAD_CRUMB(L"%s:  10a 1b 2", FuncTag);
            MsgStrA = L"The firmware refused to boot from the selected volume";
            REFIT_CALL_2_WRAPPER(gST->ConOut->SetAttribute, gST->ConOut, ATTR_ERROR);
            PrintUglyText (MsgStrA, NEXTLINE);
            REFIT_CALL_2_WRAPPER(gST->ConOut->SetAttribute, gST->ConOut, ATTR_BASIC);

            #if REFIT_DEBUG > 0
            LOG_MSG("** WARN: %s", MsgStrA);
            LOG_MSG("\n");
            #endif

            BREAD_CRUMB(L"%s:  10a 1b 3", FuncTag);
            MsgStrB = L"NB: External drives are not well-supported by Apple firmware for legacy booting";
            REFIT_CALL_2_WRAPPER(gST->ConOut->SetAttribute, gST->ConOut, ATTR_ERROR);
            PrintUglyText (MsgStrB, NEXTLINE);
            REFIT_CALL_2_WRAPPER(gST->ConOut->SetAttribute, gST->ConOut, ATTR_BASIC);

            #if REFIT_DEBUG > 0
            LOG_MSG("         %s", MsgStrB);
            LOG_MSG("\n\n");
            #endif

            BREAD_CRUMB(L"%s:  10a 1b 4", FuncTag);
            PauseForKey();

            BREAD_CRUMB(L"%s:  10a 1b 5", FuncTag);
            SwitchToGraphics();
        } // if/else ErrorInStep
    } // if Status == EFI_NOT_FOUND

    BREAD_CRUMB(L"%s:  11", FuncTag);
    FinishExternalScreen();

    BREAD_CRUMB(L"%s:  12 - END:- VOID", FuncTag);
    LOG_DECREMENT();
    LOG_SEP(L"X");
} // static VOID StartLegacy()

// Start a device on a non-Mac using the EFI_LEGACY_BIOS_PROTOCOL
VOID StartLegacyUEFI (
    LEGACY_ENTRY *Entry,
    CHAR16       *SelectionName
) {
    CHAR16 *MsgStrA = L"'UEFI-Style' Legacy (BIOS) OS";
    CHAR16 *MsgStrB = PoolPrint (L"Booting %s", MsgStrA);
    CHAR16 *MsgStrC = PoolPrint (L"Failure %s", MsgStrB);

    #if REFIT_DEBUG > 0
    ALT_LOG(1, LOG_LINE_NORMAL,
        L"Launching %s:- '%s'",
        MsgStrA, SelectionName
    );
    #endif

    IsBoot = TRUE;

    BeginExternalScreen (TRUE, MsgStrB);
    StoreLoaderName (SelectionName);

    UninitRefitLib();
    BdsLibConnectDevicePath (Entry->BdsOption->DevicePath);
    BdsLibDoLegacyBoot (Entry->BdsOption);

    // There was a failure if we get here.
    #if REFIT_DEBUG > 0
    RET_TAG();
    #endif
    ReinitRefitLib();

    #if REFIT_DEBUG > 0
    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStrC);
    #endif

    Print(L"%s", MsgStrC);
    PauseForKey();

    MY_FREE_POOL(MsgStrB);
    MY_FREE_POOL(MsgStrC);

    FinishExternalScreen();
} // static VOID StartLegacyUEFI()

static
VOID AddLegacyEntry (
    IN CHAR16         *LoaderTitle,
    IN REFIT_VOLUME   *Volume
) {
    LEGACY_ENTRY      *Entry;
    LEGACY_ENTRY      *SubEntry;
    REFIT_MENU_SCREEN *SubScreen;
    const CHAR16      *VolDesc = NULL;
    CHAR16            *LegacyTitle;
    CHAR16             ShortcutLetter = 0;

    if (Volume == NULL) {
        // Early Return
        return;
    }

    if (Volume->FSType == FS_TYPE_NTFS) {
        if (MyStriCmp (Volume->VolName, L"System Reserved") ||
            MyStriCmp (Volume->VolName, L"Basic Data Partition") ||
            MyStriCmp (Volume->VolName, L"Microsoft Reserved Partition")
        ) {
            // Early Return on Windows Support Volume
            return;
        }
    }

    if (LoaderTitle == NULL) {
        if (!Volume->OSName) {
            LoaderTitle = L"Legacy (BIOS) OS";
        }
        else {
            LoaderTitle = Volume->OSName;
            if (LoaderTitle[0] == 'W' || LoaderTitle[0] == 'L') {
                ShortcutLetter = LoaderTitle[0];
            }
        }
    }

    VolDesc = (Volume->VolName)
        ? Volume->VolName
        : (Volume->DiskKind == DISK_KIND_OPTICAL)
            ? L"CD" : L"HD";

    LegacyTitle = PoolPrint (L"Boot %s from %s", LoaderTitle, VolDesc);

    if (IsInSubstring (LegacyTitle, GlobalConfig.DontScanVolumes)) {
       MY_FREE_POOL(LegacyTitle);

       // Early Return
       return;
    }

    #if REFIT_DEBUG > 0
    UINTN LogLineType = (FirstLegacyScan)
        ? LOG_THREE_STAR_MID
        : LOG_THREE_STAR_SEP;

    ALT_LOG(1, LogLineType,
        L"Adding Legacy Boot Entry for '%s'",
        LegacyTitle
    );
    #endif

    FirstLegacyScan = FALSE;

    // Prepare the menu entry
    Entry = AllocateZeroPool (sizeof (LEGACY_ENTRY));
    if (Entry == NULL) {
        MY_FREE_POOL(LegacyTitle);

        // Early Return
        return;
    }

    Entry->me.Row            = 0;
    Entry->Enabled           = TRUE;
    Entry->me.Tag            = TAG_LEGACY;
    Entry->me.Title          = LegacyTitle;
    Entry->me.SubScreen      = NULL; // Initial Setting
    Entry->me.ShortcutLetter = ShortcutLetter;
    Entry->me.Image          = LoadOSIcon (Volume->OSIconName, L"legacy", FALSE);
    Entry->Volume            = CopyVolume (Volume);
    Entry->me.BadgeImage     = egCopyImage (Volume->VolBadgeImage);
    Entry->LoadOptions       = (Volume->DiskKind == DISK_KIND_OPTICAL)
                               ? L"CD"
                               : ((Volume->DiskKind == DISK_KIND_EXTERNAL) ? L"USB" : L"HD");

    #if REFIT_DEBUG > 0
    LOG_MSG("%s  - Found '%s' on '%s'", OffsetNext, LoaderTitle, VolDesc);
    #endif

    // Create the submenu
    SubScreen = AllocateZeroPool (sizeof (REFIT_MENU_SCREEN));
    if (SubScreen == NULL) {
        FreeMenuEntry ((REFIT_MENU_ENTRY **) Entry);

        // Early Return
        return;
    }

    SubScreen->TitleImage = egCopyImage (Entry->me.Image);
    SubScreen->Title      = PoolPrint (
        L"Boot Options for %s on %s",
        LoaderTitle, VolDesc
    );

    SubScreen->Hint1 = StrDuplicate (SUBSCREEN_HINT1);
    SubScreen->Hint2 = (GlobalConfig.HideUIFlags & HIDEUI_FLAG_EDITOR)
        ? StrDuplicate (SUBSCREEN_HINT2_NO_EDITOR)
        : StrDuplicate (SUBSCREEN_HINT2);

    // Default entry
    SubEntry = AllocateZeroPool (sizeof (LEGACY_ENTRY));
    if (SubEntry == NULL) {
        FreeMenuScreen (&SubScreen);
        FreeMenuEntry ((REFIT_MENU_ENTRY **) Entry);

        // Early Return
        return;
    }

    SubEntry->me.Title    = PoolPrint (L"Boot %s", LoaderTitle);
    SubEntry->me.Tag      = TAG_LEGACY;
    SubEntry->Volume      = CopyVolume (Entry->Volume);
    SubEntry->LoadOptions = StrDuplicate (Entry->LoadOptions);

    AddMenuEntry (SubScreen, (REFIT_MENU_ENTRY *) SubEntry);

    if (!GetReturnMenuEntry (&SubScreen)) {
        FreeMenuScreen (&SubScreen);
    }
    Entry->me.SubScreen = SubScreen;

    AddMenuEntry (MainMenu, (REFIT_MENU_ENTRY *) Entry);
} // static VOID AddLegacyEntry()


/**
    Create a RefindPlus boot option from a Legacy BIOS protocol option.
*/
static
VOID AddLegacyEntryUEFI (
    BDS_COMMON_OPTION *BdsOption,
    IN UINT16          DiskType
) {
    LEGACY_ENTRY      *Entry;
    LEGACY_ENTRY      *SubEntry;
    REFIT_MENU_SCREEN *SubScreen;
    CHAR16             ShortcutLetter = 0;

    if (IsInSubstring (BdsOption->Description, GlobalConfig.DontScanVolumes)) {
        // Early Return
        return;
    }

    // Remove stray spaces, since many EFIs produce descriptions with lots of
    //   extra spaces, especially at the end; this throws off centering of the
    //   description on the screen.
    LimitStringLength (BdsOption->Description, 100);

    // Prepare the menu entry
    Entry = AllocateZeroPool (sizeof (LEGACY_ENTRY));
    if (Entry == NULL) {
        // Early Return
        return;
    }

    Entry->me.Title = PoolPrint (
        L"Boot Legacy (BIOS) OS from %s",
        BdsOption->Description
    );

    #if REFIT_DEBUG > 0
    ALT_LOG(1, LOG_BLANK_LINE_SEP, L"X");
    ALT_LOG(1, LOG_THREE_STAR_MID,
        L"Adding 'UEFI-Style' Legacy Entry for '%s'",
        Entry->me.Title
    );
    #endif

    Entry->me.Row            = 0;
    Entry->me.Tag            = TAG_LEGACY_UEFI;
    Entry->me.SubScreen      = NULL; // Initial Setting
    Entry->me.ShortcutLetter = ShortcutLetter;
    Entry->me.Image          = LoadOSIcon (L"legacy", L"legacy", TRUE);
    Entry->LoadOptions       = (DiskType == BBS_CDROM)
                                ? L"CD"
                                : ((DiskType == BBS_USB)
                                    ? L"USB" : L"HD");
    Entry->me.BadgeImage     = GetDiskBadge (DiskType);
    Entry->BdsOption         = CopyBdsOption (BdsOption);
    Entry->Enabled           = TRUE;

    // Create the submenu
    SubScreen = AllocateZeroPool (sizeof (REFIT_MENU_SCREEN));
    if (SubScreen == NULL) {
        FreeMenuEntry ((REFIT_MENU_ENTRY **) Entry);

        // Early Return
        return;
    }

    SubScreen->TitleImage = egCopyImage (Entry->me.Image);
    SubScreen->Title      = PoolPrint (L"Legacy (BIOS) Options for %s", BdsOption->Description);
    SubScreen->Hint1      = StrDuplicate (SUBSCREEN_HINT1);
    SubScreen->Hint2      = (GlobalConfig.HideUIFlags & HIDEUI_FLAG_EDITOR)
        ? StrDuplicate (SUBSCREEN_HINT2_NO_EDITOR)
        : StrDuplicate (SUBSCREEN_HINT2);

    // Default entry
    SubEntry = AllocateZeroPool (sizeof (LEGACY_ENTRY));
    if (SubEntry == NULL) {
        FreeMenuScreen (&SubScreen);
        FreeMenuEntry ((REFIT_MENU_ENTRY **) Entry);

        // Early Return
        return;
    }

    SubEntry->me.Title  = PoolPrint (L"Boot %s", BdsOption->Description);
    SubEntry->me.Tag    = TAG_LEGACY_UEFI;
    SubEntry->BdsOption = CopyBdsOption (BdsOption);

    AddMenuEntry (SubScreen, (REFIT_MENU_ENTRY *) SubEntry);

    if (!GetReturnMenuEntry (&SubScreen)) {
        FreeMenuScreen (&SubScreen);
    }
    Entry->me.SubScreen = SubScreen;

    AddMenuEntry (MainMenu, (REFIT_MENU_ENTRY *) Entry);

    #if REFIT_DEBUG > 0
    LOG_MSG("%s  - Found 'UEFI-Style' Legacy (BIOS) OS on '%s'", OffsetNext, BdsOption->Description);
    #endif
} // static VOID AddLegacyEntryUEFI()

/**
    Scan for legacy BIOS targets on machines that implement EFI_LEGACY_BIOS_PROTOCOL.
    In testing, protocol has not been implemented on Macs but has been implemented on
      most UEFI PCs. Restricts output to disks of the specified DiskType.
*/
static
VOID ScanLegacyUEFI (
    IN UINTN DiskType
) {
    EFI_STATUS                 Status;
    UINT16                    *BootOrder       = NULL;
    UINTN                      Index           = 0;
    UINTN                      BootOrderSize   = 0;
    CHAR16                     Buffer[20];
    CHAR16                     BootOption[10];
    BOOLEAN                    SearchingForUsb = FALSE;
    LIST_ENTRY                 TempList;
    BDS_COMMON_OPTION         *BdsOption;
    BBS_BBS_DEVICE_PATH       *BbsDevicePath   = NULL;
    EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;

    #if REFIT_DEBUG > 1
    CHAR16 *FuncTag = L"ScanLegacyUEFI";
    #endif

    LOG_SEP(L"X");
    LOG_INCREMENT();
    BREAD_CRUMB(L"%s:  A - START", FuncTag);

    #if REFIT_DEBUG > 0
    UINTN LogLineType = (FirstLegacyScan)
        ? LOG_THREE_STAR_MID
        : LOG_THREE_STAR_SEP;

    /* Exception for LOG_THREE_STAR_SEP */
    ALT_LOG(1, LogLineType, L"Scanning for 'UEFI-Style' Legacy Boot Options");
    #endif

    FirstLegacyScan = FALSE;

    ZeroMem (Buffer, sizeof (Buffer));

    // If LegacyBios protocol is not implemented on this platform, then
    //   we do not support this type of legacy boot on this machine.
    Status = REFIT_CALL_3_WRAPPER(
        gBS->LocateProtocol, &gEfiLegacyBootProtocolGuid,
        NULL, (VOID **) &LegacyBios
    );
    if (EFI_ERROR(Status)) {
        BREAD_CRUMB(L"%s:  B - END:- VOID (LocateProtocol Error)", FuncTag);
        LOG_DECREMENT();
        LOG_SEP(L"X");

        // Early Return
        return;
    }

    // EFI calls USB drives BBS_HARDDRIVE, but we want to distinguish them,
    //   so we set DiskType inappropriately elsewhere in the program and
    //   "translate" it here.
    if (DiskType == BBS_USB) {
       DiskType = BBS_HARDDISK;
       SearchingForUsb = TRUE;
    }

    // Grab the boot order
    BootOrder = BdsLibGetVariableAndSize (
        L"BootOrder",
        &gEfiGlobalVariableGuid,
        &BootOrderSize
    );

    if (BootOrder == NULL) {
        BootOrderSize = 0;
    }

    Index = 0;
    while (Index < BootOrderSize / sizeof (UINT16)) {
        // Grab each boot option variable from the boot order and convert
        //   the variable into a BDS boot option
        SPrint (BootOption, sizeof (BootOption), L"Boot%04x", BootOrder[Index]);
        // We are not building a list of boot options so init the head each time
        InitializeListHead (&TempList);
        BdsOption = BdsLibVariableToOption (&TempList, BootOption);

        if (BdsOption != NULL) {
            BbsDevicePath = (BBS_BBS_DEVICE_PATH *) BdsOption->DevicePath;
            // Only add the entry if it is of a requested type (e.g. USB, HD).
            // Two checks necessary because some systems return EFI boot loaders
            //   with a DeviceType value that would inappropriately include them
            //   as legacy loaders.
            if ((BbsDevicePath->DeviceType == DiskType) &&
                (BdsOption->DevicePath->Type == DEVICE_TYPE_BIOS)
            ) {
                // USB flash drives appear as hard disks with certain media flags set.
                // Look for this, and if present, pass it on with the (technically
                //   incorrect, but internally useful) BBS_TYPE_USB flag set.
                if (DiskType != BBS_HARDDISK) {
                    AddLegacyEntryUEFI (BdsOption, DiskType);
                }
                else if (SearchingForUsb && (BbsDevicePath->StatusFlag &
                    (BBS_MEDIA_PRESENT | BBS_MEDIA_MAYBE_PRESENT))
                ) {
                    AddLegacyEntryUEFI (BdsOption, BBS_USB);
                }
                else if (!SearchingForUsb && !(BbsDevicePath->StatusFlag &
                    (BBS_MEDIA_PRESENT | BBS_MEDIA_MAYBE_PRESENT))
                ) {
                    AddLegacyEntryUEFI (BdsOption, DiskType);
                }
            } // if BbsDevicePath->DeviceType

            FreeBdsOption (&BdsOption);
        } // if BdsOption != NULL

        Index++;
    } // while

    MY_FREE_POOL(BootOrder);

    BREAD_CRUMB(L"%s:  B - END:- VOID", FuncTag);
    LOG_DECREMENT();
    LOG_SEP(L"X");
} // static VOID ScanLegacyUEFI()

static
VOID ScanLegacyVolume (
    REFIT_VOLUME *Volume,
    UINTN         VolumeIndex
) {
    UINTN   VolumeIndex2;
    BOOLEAN ShowVolume, HideIfOthersFound;

    ShowVolume        = FALSE;
    HideIfOthersFound = FALSE;

    #if REFIT_DEBUG > 1
    CHAR16 *FuncTag = L"ScanLegacyVolume";
    #endif

    LOG_SEP(L"X");
    LOG_INCREMENT();
    BREAD_CRUMB(L"%s:  A - START", FuncTag);

    if (Volume->HasBootCode) {
        ShowVolume = TRUE;
        if (Volume->BlockIO == Volume->WholeDiskBlockIO &&
            Volume->BlockIOOffset == 0 &&
            Volume->OSName == NULL
        ) {
            // Whole disk (MBR) entry found ... Hide this volume
            HideIfOthersFound = TRUE;
        }
    }

    if (HideIfOthersFound) {
        // Check for other bootable entries on the same disk
        for (VolumeIndex2 = 0; VolumeIndex2 < VolumesCount; VolumeIndex2++) {
            if (VolumeIndex2 != VolumeIndex && Volumes[VolumeIndex2]->HasBootCode &&
                Volumes[VolumeIndex2]->WholeDiskBlockIO == Volume->WholeDiskBlockIO
            ) {
                ShowVolume = FALSE;
            }
        }
    }

    if (ShowVolume) {
        if ((Volume->VolName == NULL) ||
            (StrLen (Volume->VolName) < 1)
        ) {
            Volume->VolName = GetVolumeName (Volume);
        }

        AddLegacyEntry (NULL, Volume);
    }

    BREAD_CRUMB(L"%s:  B - END:- VOID", FuncTag);
    LOG_DECREMENT();
    LOG_SEP(L"X");
} // static VOID ScanLegacyVolume()


// Scan attached optical discs for legacy (BIOS) boot code
//   and add anything found to the list.
VOID ScanLegacyDisc (VOID) {
    UINTN         VolumeIndex;
    REFIT_VOLUME *Volume;

    #if REFIT_DEBUG > 0
    ALT_LOG(1, LOG_LINE_THIN_SEP,
        L"Scan for Optical Discs with Mode:- 'Legacy (BIOS)'"
    );
    #endif

    #if REFIT_DEBUG > 1
    CHAR16 *FuncTag = L"ScanLegacyDisc";
    #endif

    LOG_SEP(L"X");
    LOG_INCREMENT();
    BREAD_CRUMB(L"%s:  A - START", FuncTag);

    FirstLegacyScan = TRUE;
    if (GlobalConfig.LegacyType == LEGACY_TYPE_UEFI) {
        ScanLegacyUEFI (BBS_CDROM);
    }
    else if (GlobalConfig.LegacyType == LEGACY_TYPE_MAC) {
        for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
            Volume = Volumes[VolumeIndex];
            if (Volume->DiskKind == DISK_KIND_OPTICAL) {
                ScanLegacyVolume (Volume, VolumeIndex);
            }
         } // for
    }
    FirstLegacyScan = FALSE;

    BREAD_CRUMB(L"%s:  B - END:- VOID", FuncTag);
    LOG_DECREMENT();
    LOG_SEP(L"X");
} // VOID ScanLegacyDisc()

// Scan internal hard disks for legacy (BIOS) boot code
//   and add anything found to the list.
VOID ScanLegacyInternal (VOID) {
    UINTN         VolumeIndex;
    REFIT_VOLUME *Volume;

    #if REFIT_DEBUG > 1
    CHAR16 *FuncTag = L"ScanLegacyInternal";
    #endif

    LOG_SEP(L"X");
    LOG_INCREMENT();
    BREAD_CRUMB(L"%s:  A - START", FuncTag);

    #if REFIT_DEBUG > 0
    ALT_LOG(1, LOG_LINE_THIN_SEP,
        L"Scan for Internal Disk Volumes with Mode:- 'Legacy (BIOS)'"
    );
    #endif

    FirstLegacyScan = TRUE;
    if (GlobalConfig.LegacyType == LEGACY_TYPE_UEFI) {
       // DA-TAG: This also picks USB flash drives up.
       //         Try to find a way to differentiate.
       ScanLegacyUEFI (BBS_HARDDISK);
    }
    else if (GlobalConfig.LegacyType == LEGACY_TYPE_MAC) {
        for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
            Volume = Volumes[VolumeIndex];
            if (Volume->DiskKind == DISK_KIND_INTERNAL) {
                ScanLegacyVolume (Volume, VolumeIndex);
            }
        } // for
    }
    FirstLegacyScan = FALSE;

    BREAD_CRUMB(L"%s:  B - END:- VOID", FuncTag);
    LOG_DECREMENT();
    LOG_SEP(L"X");
} // VOID ScanLegacyInternal()

// Scan external disks for legacy (BIOS) boot code
//   and add anything found to the list.
VOID ScanLegacyExternal (VOID) {
    UINTN         VolumeIndex;
    REFIT_VOLUME *Volume;

    #if REFIT_DEBUG > 0
    ALT_LOG(1, LOG_LINE_THIN_SEP,
        L"Scan for External Disk Volumes with Mode:- 'Legacy (BIOS)'"
    );
    #endif

    #if REFIT_DEBUG > 1
    CHAR16 *FuncTag = L"ScanLegacyExternal";
    #endif

    LOG_SEP(L"X");
    LOG_INCREMENT();
    BREAD_CRUMB(L"%s:  A - START", FuncTag);

    FirstLegacyScan = TRUE;
    if (GlobalConfig.LegacyType == LEGACY_TYPE_UEFI) {
        // TODO: This actually does not do anything useful.
        //       Leaving in hope of fixing it later.
        ScanLegacyUEFI (BBS_USB);
    }
    else if (GlobalConfig.LegacyType == LEGACY_TYPE_MAC) {
        for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
            Volume = Volumes[VolumeIndex];
            if (Volume->DiskKind == DISK_KIND_EXTERNAL) {
                ScanLegacyVolume (Volume, VolumeIndex);
            }
        } // for
    }
    FirstLegacyScan = FALSE;

    BREAD_CRUMB(L"%s:  B - END:- VOID", FuncTag);
    LOG_DECREMENT();
    LOG_SEP(L"X");
} // VOID ScanLegacyExternal()

// Determine what (if any) type of legacy (BIOS) boot support is available
VOID FindLegacyBootType (VOID) {
    EFI_STATUS                 Status;
    EFI_LEGACY_BIOS_PROTOCOL  *LegacyBios;

    // Macs have their own system. If the firmware vendor code contains the
    //   string "Apple", assume it is available. Note that this overrides the
    //   UEFI type, and might yield false positives if the vendor string
    //   contains "Apple" as part of something bigger, so this is not perfect.
    if (AppleFirmware) {
        GlobalConfig.LegacyType = LEGACY_TYPE_MAC;

        // Early Return
        return;
    }

    // UEFI-style legacy BIOS support is only available with some EFI implementations
    Status = REFIT_CALL_3_WRAPPER(
        gBS->LocateProtocol, &gEfiLegacyBootProtocolGuid,
        NULL, (VOID **) &LegacyBios
    );
    if (!EFI_ERROR(Status)) {
        GlobalConfig.LegacyType = LEGACY_TYPE_UEFI;

        // Early Return
        return;
    }

    GlobalConfig.LegacyType = LEGACY_TYPE_NONE;
} // VOID FindLegacyBootType()

// Warn user if legacy OS scans are enabled but the firmware does not support them
VOID WarnIfLegacyProblems (VOID) {
    UINTN     i      = 0;
    BOOLEAN   found  = FALSE;
    CHAR16   *MsgStr = NULL;

    #if REFIT_DEBUG > 1
    CHAR16 *FuncTag = L"WarnIfLegacyProblems";
    #endif

    LOG_SEP(L"X");
    LOG_INCREMENT();
    BREAD_CRUMB(L"%s:  A - START", FuncTag);

    if (GlobalConfig.LegacyType == LEGACY_TYPE_NONE) {
        do {
            if (GlobalConfig.ScanFor[i] == 'H' || GlobalConfig.ScanFor[i] == 'h' ||
                GlobalConfig.ScanFor[i] == 'C' || GlobalConfig.ScanFor[i] == 'c' ||
                GlobalConfig.ScanFor[i] == 'B' || GlobalConfig.ScanFor[i] == 'b'
            ) {
                found = TRUE;
            }
            i++;
        } while ((i < NUM_SCAN_OPTIONS) && (!found));

        if (found) {
            #if REFIT_DEBUG > 0
            MsgStr = StrDuplicate (L"Legacy (BIOS) Support Enabled in RefindPlus but Unavailable in EFI");
            ALT_LOG(1, LOG_STAR_SEPARATOR, L"%s!!", MsgStr);
            LOG_MSG("\n\n* ** ** *** *** ***[ %s ]*** *** *** ** ** *", MsgStr);
            LOG_MSG("\n\n");
            MY_FREE_POOL(MsgStr);
            #endif

            MsgStr = StrDuplicate (
                L"Your 'scanfor' config line specifies scanning for one or more legacy     \n"
                L"(BIOS) boot options; however, this is not possible because your computer \n"
                L"lacks the necessary Compatibility Support Module (CSM) support or because\n"
                L"CSM support has been disabled in your firmware.                           "
            );

            if (!GlobalConfig.DirectBoot) {
                CHAR16 *TmpMsgA = L"** WARN: Legacy (BIOS) Boot Issues                                        ";
                CHAR16 *TmpMsgB = L"                                                                          ";

                #if REFIT_DEBUG > 0
                BOOLEAN CheckMute = FALSE;
                MY_MUTELOGGER_SET;
                #endif
                SwitchToText (FALSE);

                PrintUglyText (TmpMsgB, NEXTLINE);
                REFIT_CALL_2_WRAPPER(gST->ConOut->SetAttribute, gST->ConOut, ATTR_ERROR);
                PrintUglyText (TmpMsgA, NEXTLINE);
                REFIT_CALL_2_WRAPPER(gST->ConOut->SetAttribute, gST->ConOut, ATTR_BASIC);
                PrintUglyText (TmpMsgB, NEXTLINE);
                PrintUglyText (MsgStr, NEXTLINE);
                PrintUglyText (TmpMsgB, NEXTLINE);

                PauseForKey();
                #if REFIT_DEBUG > 0
                MY_MUTELOGGER_OFF;
                #endif
            }

            #if REFIT_DEBUG > 0
            ALT_LOG(1, LOG_LINE_NORMAL, MsgStr);
            LOG_SEP(L"X");
            LOG_MSG("%s", MsgStr);
            LOG_MSG("\n\n");
            #endif

            MY_FREE_POOL(MsgStr);
        }
    } // if GlobalConfig.LegacyType

    BREAD_CRUMB(L"%s:  B - END:- VOID", FuncTag);
    LOG_DECREMENT();
    LOG_SEP(L"X");
} // VOID WarnIfLegacyProblems()
