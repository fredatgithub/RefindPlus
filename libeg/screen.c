/*
 * libeg/screen.c
 * Screen handling functions
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
 * Modifications copyright (c) 2012-2021 Roderick W. Smith
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

#include "libegint.h"
#include "../BootMaster/screenmgt.h"
#include "../BootMaster/global.h"
#include "../BootMaster/apple.h"
#include "../BootMaster/lib.h"
#include "../BootMaster/mystrings.h"
#include "../include/refit_call_wrapper.h"
#include "libeg.h"
#include "lodepng.h"
#include "../include/Handle.h"

#include <efiUgaDraw.h>
#include <efiConsoleControl.h>

#ifndef __MAKEWITH_GNUEFI
#define LibLocateProtocol EfiLibLocateProtocol
#define LibOpenRoot EfiLibOpenRoot
#else
#include <efilib.h>
#endif

extern UINTN    AppleFramebuffers;
extern BOOLEAN  ForceTextOnly;
extern BOOLEAN  AllowTweakUEFI;
extern BOOLEAN  AcquireErrorGOP;
extern EG_PIXEL MenuBackgroundPixel;


// Console defines and variables
EFI_GUID UgaDrawProtocolGuid        = EFI_UGA_DRAW_PROTOCOL_GUID;
EFI_GUID GOPDrawProtocolGuid        = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
EFI_GUID ConsoleControlProtocolGuid = EFI_CONSOLE_CONTROL_PROTOCOL_GUID;

EFI_UGA_DRAW_PROTOCOL        *UGADraw        = NULL;
EFI_GRAPHICS_OUTPUT_PROTOCOL *GOPDraw        = NULL;
EFI_CONSOLE_CONTROL_PROTOCOL *ConsoleControl = NULL;

BOOLEAN egHasGraphics  = FALSE;
BOOLEAN SetPreferUGA   = FALSE;

UINTN   SelectedGOP    = 0;
UINTN   egScreenWidth  = 800;
UINTN   egScreenHeight = 600;


static
EFI_STATUS EncodeAsPNG (
    IN  VOID     *RawData,
    IN  UINT32    Width,
    IN  UINT32    Height,
    OUT VOID    **Buffer,
    OUT UINTN    *BufferSize
) {
    EFI_STATUS Status = EFI_SUCCESS;
    UINTN      ErrorCode;

    // Should return 0 on success
    ErrorCode = lodepng_encode32 (
        (unsigned char **) Buffer,
        BufferSize,
        RawData,
        Width,
        Height
    );

    if (ErrorCode != 0) {
        Status = EFI_INVALID_PARAMETER;
    }

    return Status;
} // static EFI_STATUS EncodeAsPNG()

static
EFI_STATUS RefitCheckGOP (VOID) {
    EFI_STATUS                     Status;
    UINTN                          i;
    UINTN                          HandleCount;
    EFI_HANDLE                    *HandleBuffer;
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *OrigGop = NULL;

    // Search for GOP on ConsoleOut handle
    Status = REFIT_CALL_3_WRAPPER(
        gBS->HandleProtocol, gST->ConsoleOutHandle,
        &GOPDrawProtocolGuid, (VOID **) &OrigGop
    );
    if (!OrigGop || EFI_ERROR(Status)) {
        // Early Return on Failure ... Proceed to Try to Provide
        // Need to return 'Success' to trigger provision
        return EFI_SUCCESS;
    }

    // Search for avaliable modes on ConsoleOut GOP
    if (OrigGop->Mode->MaxMode > 0) {
        #if REFIT_DEBUG > 0
        LOG_MSG("INFO: Usable GOP Found on ConsoleOut Handle");
        LOG_MSG("\n\n");
        #endif

        GOPDraw = OrigGop;

        // Early Return ... Skip Provision
        return EFI_ALREADY_STARTED;
    }

    #if REFIT_DEBUG > 0
    LOG_MSG("Seek Replacement GOP Candidates:");
    #endif

    // Search for GOP on handle buffer
    Status = REFIT_CALL_5_WRAPPER(
        gBS->LocateHandleBuffer, ByProtocol,
        &GOPDrawProtocolGuid, NULL,
        &HandleCount, &HandleBuffer
    );
    if (EFI_ERROR(Status) || HandleCount == 1) {
        #if REFIT_DEBUG > 0
        LOG_MSG("%s  - Could Not Find GOP Candidates", OffsetNext);
        LOG_MSG("\n\n");
        #endif

        MY_FREE_POOL(HandleBuffer);

        // Early Return ... Skip Provision
        return EFI_NOT_FOUND;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL         *Gop;

    UINTN   SizeOfInfo;
    UINT32  Mode;
    UINT32  Width;
    UINT32  Height;
    BOOLEAN DoneLoop;
    BOOLEAN OurValidGOP = FALSE;

    #if REFIT_DEBUG > 0
    UINTN   IndexGPU = 0;
    #endif

    // Assess GOP instances on handle buffer
    for (i = 0; i < HandleCount; i++) {
        if (HandleBuffer[i] == gST->ConsoleOutHandle) {
            // Skip ConsoleOut GOP
            continue;
        }

        Status = REFIT_CALL_3_WRAPPER(
            gBS->HandleProtocol, HandleBuffer[i],
            &GOPDrawProtocolGuid, (VOID **) &Gop
        );
        if (EFI_ERROR(Status)) {
            // Skip handle on error
            continue;
        }

        #if REFIT_DEBUG > 0
        ++IndexGPU;
        LOG_MSG("%s  - Found Replacement Candidate on GPU %02d", OffsetNext, IndexGPU);
        LOG_MSG("%s    * Evaluate Candidate", OffsetNext);
        #endif

        Width = Height = 0;
        DoneLoop = FALSE;

        for (Mode = 0; Mode < Gop->Mode->MaxMode; Mode++) {
            if (DoneLoop) {
                MY_FREE_POOL(Info);
            }

            Status = Gop->QueryMode (Gop, Mode, &SizeOfInfo, &Info);
            if (EFI_ERROR(Status)) {
                DoneLoop = FALSE;

                // Skip handle
                continue;
            }
            DoneLoop = TRUE;

            if (Width  > Info->HorizontalResolution ||
                Height > Info->VerticalResolution
            ) {
                // Skip handle
                continue;
            }

            if (Width  == Info->HorizontalResolution &&
                Height == Info->VerticalResolution
            ) {
                // Skip handle
                continue;
            }

            Width  = Info->HorizontalResolution;
            Height = Info->VerticalResolution;
        } // for Mode = 0

        MY_FREE_POOL(Info);

        #if REFIT_DEBUG > 0
        LOG_MSG(
            "%s    ** %s Candidate : %5d x %-5d",
            OffsetNext,
            (Width == 0 || Height == 0)
                ? L"Invalid"
                : L"Valid",
            Width, Height
        );
        #endif

        if (Width != 0 && Height != 0) {
            OurValidGOP = TRUE;

            // Found valid candiadte ... Break out of loop
            break;
        }
    } // for i = 0

    MY_FREE_POOL(HandleBuffer);

    #if REFIT_DEBUG > 0
    LOG_MSG("\n\n");
    #endif

    if (!OurValidGOP || EFI_ERROR(Status)) {
        #if REFIT_DEBUG > 0
        LOG_MSG("INFO: Could Not Find Usable Replacement GOP");
        LOG_MSG("\n\n");
        #endif

        // Early Return ... Skip Provision
        return EFI_UNSUPPORTED;
    }

    // Return 'Success' to trigger provision
    return EFI_SUCCESS;
} // static EFI_STATUS RefitCheckGOP()

EFI_STATUS egDumpGOPVideoModes (VOID) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

    EFI_STATUS Status;
    UINT32     Mode;
    UINT32     MaxMode;
    UINT32     LoopCount;
    UINTN      SizeOfInfo;
    BOOLEAN    OurValidGOP = FALSE;

    #if REFIT_DEBUG > 0
    CHAR16 *MsgStr   = NULL;
    CHAR16 *PixelFormatDesc;
    #endif

    if (GOPDraw == NULL) {
        #if REFIT_DEBUG > 0
        MsgStr = StrDuplicate (L"Could Not Find GOP Instance");
        ALT_LOG(1, LOG_STAR_SEPARATOR, L"%s!!", MsgStr);
        LOG_MSG("** WARN: %s", MsgStr);
        LOG_MSG("\n\n");
        MY_FREE_POOL(MsgStr);
        #endif

        return EFI_UNSUPPORTED;
    }

    // Get dump
    MaxMode = GOPDraw->Mode->MaxMode;
    if (MaxMode > 0) {
        #if REFIT_DEBUG > 0
        MsgStr = PoolPrint (L"Analyse GOP Modes on GPU Handle[%02d]", SelectedGOP);
        ALT_LOG(1, LOG_THREE_STAR_MID, L"%s", MsgStr);
        LOG_MSG("%s:", MsgStr);
        MY_FREE_POOL(MsgStr);

        MsgStr = PoolPrint (
            L"%02d GOP Mode%s ... 0x%lx <-> 0x%lx Framebuffer",
            MaxMode,
            (MaxMode != 1) ? L"s" : L"",
            GOPDraw->Mode->FrameBufferBase,
            GOPDraw->Mode->FrameBufferBase + GOPDraw->Mode->FrameBufferSize
        );
        ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
        LOG_MSG("%s  - Summary:- '%s'", OffsetNext, MsgStr);
        MY_FREE_POOL(MsgStr);
        #endif

        LoopCount = -1;
        for (Mode = 0; Mode <= MaxMode; Mode++) {
            LoopCount++;
            if (LoopCount == MaxMode) {
                break;
            }

            #if REFIT_DEBUG > 0
            UINT32 ModeLog;

            // Limit logged value to 99
            ModeLog = (Mode > 99) ? 99 : Mode;

            LOG_MSG("%s    * Mode[%02d]", OffsetNext, ModeLog);
            #endif

            Status = REFIT_CALL_4_WRAPPER(
                GOPDraw->QueryMode, GOPDraw,
                Mode, &SizeOfInfo, &Info
            );
            if (!EFI_ERROR(Status)) {
                OurValidGOP = TRUE;

                #if REFIT_DEBUG > 0
                switch (Info->PixelFormat) {
                    case PixelRedGreenBlueReserved8BitPerColor: PixelFormatDesc = L"8bit RGB";  break;
                    case PixelBlueGreenRedReserved8BitPerColor: PixelFormatDesc = L"8bit BGR";  break;
                    case PixelBitMask:                          PixelFormatDesc = L"BIT MASK";  break;
                    case PixelBltOnly:                          PixelFormatDesc = L"!FBuffer";  break;
                    default:                                    PixelFormatDesc = L"Invalid!";  break;
                } // switch

                LOG_MSG(
                    " @ %5d x %-5d (%5d Pixels Per Scanned Line, %s Pixel Format ) ... %r",
                    Info->HorizontalResolution,
                    Info->VerticalResolution,
                    Info->PixelsPerScanLine,
                    PixelFormatDesc, Status
                );
                if (LoopCount >= (MaxMode - 1)) {
                    LOG_MSG("\n\n");
                }
                #endif
            }
            else {
                #if REFIT_DEBUG > 0
                ALT_LOG(1, LOG_THREE_STAR_MID, L"Mode[%02d]: %r", ModeLog, Status);
                LOG_MSG(" ... %r", Status);

                if (Mode > 99) {
                    LOG_MSG( ". NB: Real Mode = %02d", Mode);
                }

                if (LoopCount >= (MaxMode - 1)) {
                    LOG_MSG("\n\n");
                }
                else {
                    LOG_MSG("\n");
                }
                #endif
            } // if/else !EFI_ERROR(Status)

            MY_FREE_POOL(Info);
        } // for (Mode = 0; Mode <= MaxMode; Mode++)
    } // if MaxMode > 0

    if (!OurValidGOP) {
        #if REFIT_DEBUG > 0
        MsgStr = StrDuplicate (L"Could Not Find Usable GOP");
        ALT_LOG(1, LOG_STAR_SEPARATOR, L"%s!!", MsgStr);
        LOG_MSG("INFO: %s:", MsgStr);
        LOG_MSG("\n\n");
        MY_FREE_POOL(MsgStr);
        #endif

        return EFI_UNSUPPORTED;
    }

    return EFI_SUCCESS;
} // EFI_STATUS egDumpGOPVideoModes()

//
// Sets mode via GOP protocol, and reconnects simple text out drivers
//
static
EFI_STATUS GopSetModeAndReconnectTextOut (
    IN UINT32 ModeNumber
) {
    EFI_STATUS Status;

    #if REFIT_DEBUG > 0
    CHAR16 *MsgStr = NULL;
    #endif

    if (GOPDraw == NULL) {
        return EFI_UNSUPPORTED;
    }

    Status = REFIT_CALL_2_WRAPPER(GOPDraw->SetMode, GOPDraw, ModeNumber);
    #if REFIT_DEBUG > 0
    MsgStr = PoolPrint (L"Switch to GOP Mode[%02d] ... %r", ModeNumber, Status);
    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
    LOG_MSG("%s", MsgStr);
    LOG_MSG("\n\n");
    MY_FREE_POOL(MsgStr);
    #endif

    return Status;
} // static EFI_STATUS GopSetModeAndReconnectTextOut()

static
EFI_STATUS egSetGopMode (
    INT32 Next
) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

    EFI_STATUS   Status;
    UINT32       i, MaxMode;
    UINTN        SizeOfInfo;
    INT32        Mode;

    #if REFIT_DEBUG > 0
    CHAR16 *MsgStr = NULL;

    LOG_MSG("Set GOP Mode:");
    #endif

    if (GOPDraw == NULL) {
        #if REFIT_DEBUG > 0
        MsgStr = StrDuplicate (L"Could Not Set GOP Mode");
        ALT_LOG(1, LOG_LINE_NORMAL, L"%s!!", MsgStr);
        LOG_MSG("\n\n");
        LOG_MSG("** WARN: %s", MsgStr);
        LOG_MSG("\n\n");
        MY_FREE_POOL(MsgStr);
        #endif

        return EFI_UNSUPPORTED;
    }

    MaxMode = GOPDraw->Mode->MaxMode;
    Mode    = GOPDraw->Mode->Mode;
    Status  = EFI_UNSUPPORTED;

    if (MaxMode < 1) {
        #if REFIT_DEBUG > 0
        MsgStr = StrDuplicate (L"Incompatible GPU");
        ALT_LOG(1, LOG_LINE_NORMAL, L"%s!!", MsgStr);
        LOG_MSG("\n\n");
        LOG_MSG("** WARN: %s", MsgStr);
        LOG_MSG("\n\n");
        MY_FREE_POOL(MsgStr);
        #endif
    }
    else {
        i = 0;
        while (EFI_ERROR(Status) && i <= MaxMode) {
            Mode = Mode + Next;
            Mode = (Mode >= (INT32) MaxMode) ? 0 : Mode;
            Mode = (Mode < 0) ? ((INT32) MaxMode - 1) : Mode;

            Status = REFIT_CALL_4_WRAPPER(
                GOPDraw->QueryMode, GOPDraw,
                (UINT32) Mode, &SizeOfInfo, &Info
            );

            #if REFIT_DEBUG > 0
            LOG_MSG("%s    * Mode[%02d]", OffsetNext, Status);
            #endif

            if (!EFI_ERROR(Status)) {
                #if REFIT_DEBUG > 0
                LOG_MSG("\n");
                #endif

                Status = GopSetModeAndReconnectTextOut ((UINT32) Mode);
                if (!EFI_ERROR(Status)) {
                    egScreenWidth  = GOPDraw->Mode->Info->HorizontalResolution;
                    egScreenHeight = GOPDraw->Mode->Info->VerticalResolution;
                }
            }

            i++;
        } // while

        #if REFIT_DEBUG > 0
        if (EFI_ERROR(Status)) {
            LOG_MSG("\n\n");
        }
        #endif
    }

    return Status;
} // static EFI_STATUS egSetGopMode()

// On GOP systems, set the maximum available resolution.
// On UGA systems, just record the current resolution.
static
EFI_STATUS egSetMaxResolution (VOID) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

    EFI_STATUS   Status;
    UINT32       Width    = 0;
    UINT32       Height   = 0;
    UINT32       BestMode = 0;
    UINT32       MaxMode;
    UINT32       Mode;
    UINTN        SizeOfInfo;

    #if REFIT_DEBUG > 0
    CHAR16 *MsgStr = NULL;
    #endif

    if (GOPDraw == NULL) {
        // Cannot do this in text mode or with UGA.
        // So get and set basic data and ignore.
        UINT32 Depth, RefreshRate;

        REFIT_CALL_5_WRAPPER(
            UGADraw->GetMode, UGADraw,
            &Width, &Height,
            &Depth, &RefreshRate
        );

        GlobalConfig.RequestedScreenWidth  = Width;
        GlobalConfig.RequestedScreenHeight = Height;

        return EFI_UNSUPPORTED;
    }

    #if REFIT_DEBUG > 0
    LOG_MSG("Set Screen Resolution:");
    #endif

    MaxMode = GOPDraw->Mode->MaxMode;
    for (Mode = 0; Mode < MaxMode; Mode++) {
        Status = REFIT_CALL_4_WRAPPER(
            GOPDraw->QueryMode, GOPDraw,
            Mode, &SizeOfInfo, &Info
        );
        if (!EFI_ERROR(Status)) {
            if (Width > Info->HorizontalResolution) {
                continue;
            }
            if (Height > Info->VerticalResolution) {
                continue;
            }

            BestMode = Mode;
            Width    = Info->HorizontalResolution;
            Height   = Info->VerticalResolution;
        }

        MY_FREE_POOL(Info);
    } // for

    #if REFIT_DEBUG > 0
    MsgStr = PoolPrint (L"BestMode:- 'GOP Mode[%02d] on GPU Handle[%02d] @ %d x %d'",
        BestMode, SelectedGOP,
        Width, Height
    );
    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
    LOG_MSG("%s  - %s", OffsetNext, MsgStr);
    LOG_MSG("\n");
    MY_FREE_POOL(MsgStr);
    #endif

    // check if requested mode is equal to current mode
    if (BestMode == GOPDraw->Mode->Mode) {
        Status = EFI_SUCCESS;

        #if REFIT_DEBUG > 0
        MsgStr = StrDuplicate (L"Screen Resolution Already Set");
        ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
        LOG_MSG("%s", MsgStr);
        LOG_MSG("\n\n");
        MY_FREE_POOL(MsgStr);
        #endif

        egScreenWidth  = GOPDraw->Mode->Info->HorizontalResolution;
        egScreenHeight = GOPDraw->Mode->Info->VerticalResolution;
    }
    else {
        Status = GopSetModeAndReconnectTextOut (BestMode);
        if (!EFI_ERROR(Status)) {
            egScreenWidth  = Width;
            egScreenHeight = Height;
        }
        else {
            // we cannot set BestMode - search for first one that we can use
            Status = egSetGopMode (1);

            #if REFIT_DEBUG > 0
            MsgStr = StrDuplicate (L"Could Not Set BestMode ... Try First Useable Mode");
            ALT_LOG(1, LOG_LINE_NORMAL, L"%s!!", MsgStr);
            LOG_MSG("** WARN: %s", MsgStr);
            LOG_MSG("\n\n");
            MY_FREE_POOL(MsgStr);
            #endif
        }
    }

    return Status;
} // static EFI_STATUS egSetMaxResolution()


//
// Screen handling
//

// Make the necessary system calls to identify the current graphics mode.
// Stores the results in the file-global variables egScreenWidth,
// egScreenHeight, and egHasGraphics. The first two of these will be
// unchanged if neither GOPDraw nor UGADraw is a valid pointer.
static
VOID egDetermineScreenSize (VOID) {
    EFI_STATUS Status = EFI_SUCCESS;
    UINT32     ScreenW;
    UINT32     ScreenH;
    UINT32     UgaDepth;
    UINT32     UgaRefreshRate;

    // Get screen size
    egHasGraphics = FALSE;
    if (GOPDraw != NULL) {
        egScreenWidth   = GOPDraw->Mode->Info->HorizontalResolution;
        egScreenHeight  = GOPDraw->Mode->Info->VerticalResolution;
        egHasGraphics   = TRUE;
    }
    else if (UGADraw != NULL) {
        Status = REFIT_CALL_5_WRAPPER(
            UGADraw->GetMode, UGADraw,
            &ScreenW, &ScreenH,
            &UgaDepth, &UgaRefreshRate
        );

        if (EFI_ERROR(Status)) {
            UGADraw = NULL;   // graphics not available
        }
        else {
            egScreenWidth  = ScreenW;
            egScreenHeight = ScreenH;
            egHasGraphics  = TRUE;
        }
    }
} // static VOID egDetermineScreenSize()

UINTN egCountAppleFramebuffers (VOID) {
    EFI_STATUS  Status;
    UINTN       HandleCount                       = 0;
    EFI_GUID    AppleFramebufferInfoProtocolGuid  = APPLE_FRAMEBUFFER_INFO_PROTOCOL_GUID;
    EFI_HANDLE *HandleBuffer                      = NULL;
    APPLE_FRAMEBUFFER_INFO_PROTOCOL *FramebufferInfo;

    Status = LibLocateProtocol (&AppleFramebufferInfoProtocolGuid, (VOID *) &FramebufferInfo);
    if (EFI_ERROR(Status)) {
        HandleCount = 0;
    }
    else {
        Status = REFIT_CALL_5_WRAPPER(
            gBS->LocateHandleBuffer, ByProtocol,
            &AppleFramebufferInfoProtocolGuid, NULL,
            &HandleCount, &HandleBuffer
        );
        if (EFI_ERROR(Status)) {
            HandleCount = 0;
        }
    }

    MY_FREE_POOL(HandleBuffer);

    return HandleCount;
} // UINTN egCountAppleFramebuffers()

VOID egGetScreenSize (
    OUT UINTN *ScreenWidth,
    OUT UINTN *ScreenHeight
) {
    egDetermineScreenSize();

    if (ScreenWidth != NULL) {
        *ScreenWidth = egScreenWidth;
    }
    if (ScreenHeight != NULL) {
        *ScreenHeight = egScreenHeight;
    }
} // VOID egGetScreenSize()

static
VOID egInitConsoleControl (VOID) {
    EFI_STATUS                     Status;
    UINTN                          i;
    UINTN                          HandleCount;
    EFI_HANDLE                    *HandleBuffer   = NULL;

    #if REFIT_DEBUG > 0
    LOG_MSG("%s  - Seek Console Control", OffsetNext);
    #endif

    // Check ConsoleOut Handle
    Status = REFIT_CALL_3_WRAPPER(
        gBS->HandleProtocol, gST->ConsoleOutHandle,
        &ConsoleControlProtocolGuid, (VOID **) &ConsoleControl
    );

    #if REFIT_DEBUG > 0
    LOG_MSG("%s    * Seek on ConsoleOut Handle ... %r", OffsetNext, Status);
    #endif

    if (EFI_ERROR(Status)) {
        // Try Locating by Handle
        Status = REFIT_CALL_5_WRAPPER(
            gBS->LocateHandleBuffer, ByProtocol,
            &ConsoleControlProtocolGuid, NULL,
            &HandleCount, &HandleBuffer
        );
        #if REFIT_DEBUG > 0
        LOG_MSG("%s    * Seek on Handle Buffer ... %r", OffsetNext, Status);
        #endif
        if (!EFI_ERROR(Status)) {
            for (i = 0; i < HandleCount; i++) {
                Status = REFIT_CALL_3_WRAPPER(
                    gBS->HandleProtocol, HandleBuffer[i],
                    &ConsoleControlProtocolGuid, (VOID*) &ConsoleControl
                );
                #if REFIT_DEBUG > 0
                LOG_MSG("    ** Evaluate on GPU Handle[%02d] ... %r", i, Status);
                #endif
                if (!EFI_ERROR(Status)) {
                    break;
                }
            }
            MY_FREE_POOL(HandleBuffer);
        }
        if (EFI_ERROR(Status)) {
            DetectedDevices  = FALSE;
        }
    }

    #if REFIT_DEBUG > 0
    CHAR16 *MsgStr = (EFI_ERROR(Status))
        ? L"Assess Console Control ... NOT OK!!"
        : L"Assess Console Control ... ok";
    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
    LOG_MSG("%s  - %s", OffsetNext, MsgStr);
    MY_FREE_POOL(MsgStr);
    #endif
} // static VOID egInitConsoleControl()

BOOLEAN egInitUGADraw (
    BOOLEAN LogOutput
) {
    EFI_STATUS                     Status;
    UINTN                          i;
    UINTN                          HandleCount;
    BOOLEAN                        FoundHandleUGA = FALSE;
    EFI_HANDLE                    *HandleBuffer   = NULL;

    #if REFIT_DEBUG > 0
    BOOLEAN CheckMute = FALSE;
    if (!LogOutput) {
        MY_MUTELOGGER_SET;
    }

    LOG_MSG("\n%s  - Seek Universal Graphics Adapter", OffsetNext);
    #endif

    // Check ConsoleOut Handle
    Status = REFIT_CALL_3_WRAPPER(
        gBS->HandleProtocol, gST->ConsoleOutHandle,
        &UgaDrawProtocolGuid, (VOID **) &UGADraw
    );
    #if REFIT_DEBUG > 0
    LOG_MSG("%s    * Seek on ConsoleOut Handle ... %r", OffsetNext, Status);
    #endif
    if (EFI_ERROR(Status)) {
        // Try Locating by Handle
        Status = REFIT_CALL_5_WRAPPER(
            gBS->LocateHandleBuffer, ByProtocol,
            &UgaDrawProtocolGuid, NULL,
            &HandleCount, &HandleBuffer
        );

        #if REFIT_DEBUG > 0
        LOG_MSG("%s    * Seek on Handle Buffer ... %r", OffsetNext, Status);
        #endif

        if (!EFI_ERROR(Status)) {
            EFI_UGA_DRAW_PROTOCOL *TmpUGA    = NULL;
            UINT32                 UGAWidth  = 0;
            UINT32                 UGAHeight = 0;
            UINT32                 Width;
            UINT32                 Height;
            UINT32                 Depth;
            UINT32                 RefreshRate;

            for (i = 0; i < HandleCount; i++) {
                Status = REFIT_CALL_3_WRAPPER(
                    gBS->HandleProtocol,
                    HandleBuffer[i],
                    &UgaDrawProtocolGuid,
                    (VOID*) &TmpUGA
                );

                #if REFIT_DEBUG > 0
                LOG_MSG("%s    ** Examine GPU Handle[%02d] ... %r", OffsetNext, i, Status);
                #endif

                if (!EFI_ERROR(Status)) {
                    FoundHandleUGA = TRUE;

                    if (HandleCount == 1) {
                        UGADraw = TmpUGA;
                        break;
                    }

                    Status = REFIT_CALL_5_WRAPPER(
                        TmpUGA->GetMode, TmpUGA,
                        &Width, &Height,
                        &Depth, &RefreshRate
                    );
                    if (!EFI_ERROR(Status)) {
                        if (UGAWidth < Width ||
                            UGAHeight < Height
                        ) {
                            UGADraw   = TmpUGA;
                            UGAWidth  = Width;
                            UGAHeight = Height;

                            #if REFIT_DEBUG > 0
                            LOG_MSG("%s    *** Select GPU Handle[%02d] @ %5d x %-5d",
                                OffsetNext, i, Width, Height
                            );
                            #endif
                        }
                        else {
                            #if REFIT_DEBUG > 0
                            LOG_MSG("%s    *** Ignore GPU Handle[%02d] @ %5d x %-5d",
                                OffsetNext, i, Width, Height
                            );
                            #endif
                        }
                    }
                } // if !EFI_ERROR(Status)
            } // for
            MY_FREE_POOL(HandleBuffer);

        } // if !EFI_ERROR(Status)
    } // if EFI_ERROR(Status

    #if REFIT_DEBUG > 0
    CHAR16 *MsgStr = (EFI_ERROR(Status))
        ? L"Assess Universal Graphics Adapter ... NOT OK!!"
        : L"Assess Universal Graphics Adapter ... ok";
    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
    LOG_MSG("%s  - %s", OffsetNext, MsgStr);

    if (!LogOutput) {
        MY_MUTELOGGER_OFF;
    }
    #endif

    return FoundHandleUGA;
} // static BOOLEAN egInitUGADraw()

VOID egInitScreen (VOID) {
    EFI_STATUS                     Status;
    EFI_STATUS                     XFlag;
    UINTN                          i;
    UINTN                          HandleCount;
    BOOLEAN                        FlagUGA        = FALSE;
    BOOLEAN                        thisValidGOP   = FALSE;
    EFI_HANDLE                    *HandleBuffer   = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *OldGop         = NULL;

    #if REFIT_DEBUG > 0
    CHAR16  *MsgStr   = NULL;
    BOOLEAN  PrevFlag = FALSE;

    LOG_MSG("Check for Graphics:");
    #endif

    // Get ConsoleControl Protocol
    egInitConsoleControl();

    // Get UGADraw Protocol
    BOOLEAN FoundHandleUGA = egInitUGADraw(TRUE);

    // Align PreferUGA
    if (GlobalConfig.PreferUGA && FoundHandleUGA && !SetPreferUGA) {
        UINT32     TmpScreenW;
        UINT32     TmpScreenH;
        UINT32     TmpUgaDepth;
        UINT32     TmpUgaRefreshRate;

        Status = REFIT_CALL_5_WRAPPER(
            UGADraw->GetMode, UGADraw,
            &TmpScreenW, &TmpScreenH,
            &TmpUgaDepth, &TmpUgaRefreshRate
        );
        if (EFI_ERROR(Status)) {
            UGADraw =  NULL;
        }
        else {
            SetPreferUGA = TRUE;
        }
    }

    // Get GOPDraw Protocol
    XFlag = EFI_NOT_STARTED;
    if (FoundHandleUGA && (SetPreferUGA || AcquireErrorGOP)) {
        #if REFIT_DEBUG > 0
        LOG_MSG("\n\n");
        LOG_MSG("INFO: Skip GOP Search ... ");
        LOG_MSG("%s", (SetPreferUGA) ? L"Enforced UGA-Only Mode" : L"Apparent UGA-Only Graphics");
        LOG_MSG("\n\n");
        #endif
    }
    else {
        #if REFIT_DEBUG > 0
        LOG_MSG("\n%s  - Seek Graphics Output Protocol", OffsetNext);
        #endif

        // Check ConsoleOut Handle
        Status = REFIT_CALL_3_WRAPPER(
            gBS->HandleProtocol, gST->ConsoleOutHandle,
            &GOPDrawProtocolGuid, (VOID **) &OldGop
        );

        #if REFIT_DEBUG > 0
        LOG_MSG("%s    * Seek on ConsoleOut Handle ... %r", OffsetNext, Status);
        #endif

        if (EFI_ERROR(Status)) {
            // Try Locating by Handle
            Status = REFIT_CALL_5_WRAPPER(
                gBS->LocateHandleBuffer, ByProtocol,
                &GOPDrawProtocolGuid, NULL,
                &HandleCount, &HandleBuffer
            );

            #if REFIT_DEBUG > 0
            LOG_MSG("%s    * Seek on Handle Buffer ... %r", OffsetNext, Status);
            #endif

            if (EFI_ERROR(Status)) {
                // Force to NOT FOUND on error as subsequent code relies on this
                Status = EFI_NOT_FOUND;
            }
            else {
                EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
                EFI_GRAPHICS_OUTPUT_PROTOCOL *TmpGop = NULL;
                UINT32 GopWidth  = 0;
                UINT32 GopHeight = 0;
                UINT32 MaxMode   = 0;
                UINT32 GopMode;
                UINTN  SizeOfInfo;

                for (i = 0; i < HandleCount; i++) {
                    Status = REFIT_CALL_3_WRAPPER(
                        gBS->HandleProtocol, HandleBuffer[i],
                        &GOPDrawProtocolGuid, (VOID*) &TmpGop
                    );
                    #if REFIT_DEBUG > 0
                    LOG_MSG("%s    ** Evaluate on GPU Handle[%02d] ... %r", OffsetNext, i, Status);
                    #endif
                    if (!EFI_ERROR(Status)) {
                        if (HandleCount == 1) {
                            OldGop = TmpGop;
                            break;
                        }

                        MaxMode = TmpGop->Mode->MaxMode;
                        for (GopMode = 0; GopMode < MaxMode; GopMode++) {
                            Status = TmpGop->QueryMode (TmpGop, GopMode, &SizeOfInfo, &Info);
                            if (!EFI_ERROR(Status)) {
                                if (GopWidth < Info->HorizontalResolution ||
                                    GopHeight < Info->VerticalResolution
                                ) {
                                    OldGop      = TmpGop;
                                    GopWidth    = Info->HorizontalResolution;
                                    GopHeight   = Info->VerticalResolution;
                                    SelectedGOP = i;

                                    #if REFIT_DEBUG > 0
                                    LOG_MSG(
                                        "%s    *** Select GPU Handle[%02d][%02d] @ %5d x %-5d",
                                        OffsetNext,
                                        i, GopMode,
                                        GopWidth,
                                        GopHeight
                                    );
                                    #endif
                                }
                                else {
                                    #if REFIT_DEBUG > 0
                                    LOG_MSG(
                                        "%s        Ignore GPU Handle[%02d][%02d] @ %5d x %-5d",
                                        OffsetNext,
                                        i, GopMode,
                                        Info->HorizontalResolution,
                                        Info->VerticalResolution
                                    );
                                    #endif
                                }
                            }

                            MY_FREE_POOL(Info);
                        } // for GopMode = 0
                    } // if !EFI_ERROR(Status)
                } // for

                MY_FREE_POOL(HandleBuffer);
            } // if EFI_ERROR(Status) ... Force to NOT FOUND
        } // if EFI_ERROR(Status)

        XFlag = EFI_UNSUPPORTED;
        if (Status == EFI_NOT_FOUND) {
            XFlag = EFI_NOT_FOUND;

            // Not Found
            #if REFIT_DEBUG > 0
            MsgStr = StrDuplicate (L"Assess Graphics Output Protocol ... NOT FOUND!!");
            ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
            LOG_MSG("%s  - %s", OffsetNext, MsgStr);
            LOG_MSG("\n\n");
            MY_FREE_POOL(MsgStr);
            #endif
        }
        else if (EFI_ERROR(Status) && XFlag == EFI_UNSUPPORTED) {
            XFlag = EFI_NOT_FOUND;

            // Not Found
            #if REFIT_DEBUG > 0
            MsgStr = StrDuplicate (L"Assess Graphics Output Protocol ... NOT SUPPORTED!!");
            ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
            LOG_MSG("%s  - %s", OffsetNext, MsgStr);
            LOG_MSG("\n\n");
            MY_FREE_POOL(MsgStr);
            #endif
        }
        else if (!EFI_ERROR(Status) && XFlag != EFI_ALREADY_STARTED) {
            if (OldGop && (!AllowTweakUEFI || OldGop->Mode->MaxMode > 0)) {
                XFlag        = EFI_SUCCESS;
                thisValidGOP = TRUE;

                // Set GOP to OldGop
                GOPDraw = OldGop;

                #if REFIT_DEBUG > 0
                MsgStr = PoolPrint (
                    L"Assess Graphics Output Protocol ... ok",
                    (!AllowTweakUEFI)
                        ? L" (  A S S U M E D  )"
                        : L""
                );
                ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                LOG_MSG("%s  - %s", OffsetNext, MsgStr);
                LOG_MSG("\n\n");
                MY_FREE_POOL(MsgStr);
                #endif
            }
            else {
                XFlag = EFI_UNSUPPORTED;

                #if REFIT_DEBUG > 0
                MsgStr = StrDuplicate (L"Assess Graphics Output Protocol ... NOT OK!!");
                ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                LOG_MSG("%s  - %s", OffsetNext, MsgStr);
                LOG_MSG("\n\n");
                MY_FREE_POOL(MsgStr);
                #endif

                #ifdef __MAKEWITH_TIANO
                // DA-TAG: Limit to TianoCore
                if (GlobalConfig.ProvideConsoleGOP) {
                    Status = RefitCheckGOP();
                    if (!EFI_ERROR(Status)) {
                        // Run OpenCore Function
                        Status = OcProvideConsoleGop (TRUE);
                        if (!EFI_ERROR(Status)) {
                            Status = REFIT_CALL_3_WRAPPER(
                                gBS->HandleProtocol, gST->ConsoleOutHandle,
                                &GOPDrawProtocolGuid, (VOID **) &GOPDraw
                            );
                        }
                    }
                }
                #endif
            } // if/else !AllowTweakUEFI
        } // if/else Status == EFI_NOT_FOUND

        if (XFlag != EFI_NOT_FOUND && XFlag != EFI_NOT_STARTED && XFlag != EFI_UNSUPPORTED && GlobalConfig.UseDirectGop) {
            if (GOPDraw == NULL) {
                #if REFIT_DEBUG > 0
                MsgStr = StrDuplicate (L"Cannot Implement Direct GOP Renderer");
                ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                LOG_MSG("INFO: %s", MsgStr);
                LOG_MSG("\n\n");
                MY_FREE_POOL(MsgStr);
                #endif
            }
            else {
                if (GOPDraw->Mode->Info->PixelFormat == PixelBltOnly) {
                    Status = EFI_UNSUPPORTED;
                }
                else {
                    Status = EFI_NOT_STARTED;

                    #ifdef __MAKEWITH_TIANO
                    // DA-TAG: Limit to TianoCore
                    if (AllowTweakUEFI) {
                        Status = OcUseDirectGop (-1);
                    }
                    #endif
                }
                if (!EFI_ERROR(Status)) {
                    // Check ConsoleOut Handle
                    Status = REFIT_CALL_3_WRAPPER(
                        gBS->HandleProtocol, gST->ConsoleOutHandle,
                        &GOPDrawProtocolGuid, (VOID **) &OldGop
                    );
                    if (EFI_ERROR(Status)) {
                        OldGop = NULL;
                    }
                    else {
                        if (OldGop && (!AllowTweakUEFI || OldGop->Mode->MaxMode > 0)) {
                            GOPDraw = OldGop;
                            XFlag = EFI_ALREADY_STARTED;
                        }
                    }
                }

                #if REFIT_DEBUG > 0
                CHAR16 *StrX = L"";
                if (!AllowTweakUEFI && !EFI_ERROR(Status)) {
                    StrX = L" (  A S S U M E D  )";
                }
                MsgStr = PoolPrint (
                    L"Implement Direct GOP Renderer ... %r%s",
                    Status, StrX
                );
                ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                LOG_MSG("INFO: %s", MsgStr);
                LOG_MSG("\n\n");
                MY_FREE_POOL(MsgStr);
                #endif
            } // if/else GOPDraw == NULL
        } // if XFlag != EFI_NOT_FOUND etc

        if (XFlag == EFI_NOT_FOUND || XFlag == EFI_LOAD_ERROR) {
            #if REFIT_DEBUG > 0
            MsgStr = PoolPrint (L"Graphics Output Protocol ... %r", XFlag);
            #endif
        }
        else if (XFlag == EFI_UNSUPPORTED) {
            #if REFIT_DEBUG > 0
            MsgStr = PoolPrint (L"Provide GOP on ConsoleOut Handle ... %r", Status);
            #endif

            if (!EFI_ERROR(Status)) {
                thisValidGOP = TRUE;
            }
        }
        #if REFIT_DEBUG > 0
        if (MsgStr) {
            ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
            LOG_MSG("INFO: %s", MsgStr);
            LOG_MSG("\n\n");
            FreePool (MsgStr);
        }
        #endif
    } // if/else FoundHandleUGA && (SetPreferUGA || AcquireErrorGOP)

    // Get Screen Size
    egHasGraphics = FALSE;
    if (GOPDraw != NULL) {
        Status = egDumpGOPVideoModes();
        if (EFI_ERROR(Status)) {
            #if REFIT_DEBUG > 0
            MsgStr = StrDuplicate (L"Invalid GOP Instance");
            ALT_LOG(1, LOG_LINE_NORMAL, L"WARNING: %s!!", MsgStr);
            LOG_MSG("** WARN: %s", MsgStr);
            LOG_MSG("\n\n");
            MY_FREE_POOL(MsgStr);
            #endif

            GOPDraw = NULL;
        }
        else {
            egHasGraphics  = TRUE;

            Status = egSetMaxResolution();
            if (!EFI_ERROR(Status)) {
                egScreenWidth  = GOPDraw->Mode->Info->HorizontalResolution;
                egScreenHeight = GOPDraw->Mode->Info->VerticalResolution;
            }
            else {
                egScreenWidth  = GlobalConfig.RequestedScreenWidth;
                egScreenHeight = GlobalConfig.RequestedScreenHeight;
            }

            #if REFIT_DEBUG > 0
            // Only log this if GOPFix or Direct Renderer attempted
            if (XFlag == EFI_UNSUPPORTED || XFlag == EFI_ALREADY_STARTED) {
                MsgStr = PoolPrint (L"Provide Graphics Output Protocol ... %r", Status);
                ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                LOG_MSG("INFO: %s", MsgStr);
                LOG_MSG("\n\n");
                MY_FREE_POOL(MsgStr);
            }
            #endif
        }
    } // if GOPDraw != NULL

    if (UGADraw != NULL) {
        #if REFIT_DEBUG > 0
        Status = EFI_NOT_STARTED;
        #endif

// DA-TAG: Limit to TianoCore
#ifdef __MAKEWITH_TIANO
        if (GlobalConfig.PassUgaThrough && thisValidGOP) {
            // Run OcProvideUgaPassThrough from OpenCorePkg
            Status = OcProvideUgaPassThrough();
        }
#endif

        #if REFIT_DEBUG > 0
        MsgStr = PoolPrint (L"Implement UGA Pass Through ... %r", Status);
        ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
        LOG_MSG("INFO: %s", MsgStr);
        if ((GOPDraw != NULL) &&
            (GlobalConfig.UseTextRenderer || GlobalConfig.TextOnly)
        ) {
            PrevFlag = TRUE;
        }
        else {
            LOG_MSG("\n\n");
        }
        MY_FREE_POOL(MsgStr);
        #endif

        if (GOPDraw == NULL) {
            UINT32 Width, Height, Depth, RefreshRate;
            Status = REFIT_CALL_5_WRAPPER(
                UGADraw->GetMode, UGADraw,
                &Width, &Height,
                &Depth, &RefreshRate
            );
            if (EFI_ERROR(Status)) {
                // Graphics not available
                UGADraw               = NULL;
                GlobalConfig.TextOnly = ForceTextOnly = TRUE;
            }
            else {
                egHasGraphics  = FlagUGA = TRUE;
                egScreenWidth  = GlobalConfig.RequestedScreenWidth  = Width;
                egScreenHeight = GlobalConfig.RequestedScreenHeight = Height;
            }

            #if REFIT_DEBUG > 0
            MsgStr = StrDuplicate (
                (EFI_ERROR(Status))
                    ? L"Graphics Not Available ... Fall Back on Text Mode"
                    : (FoundHandleUGA && AcquireErrorGOP)
                        ? L"Leveraging Universal Graphics Adapter"
                        : (SetPreferUGA)
                            ? L"Enforcing Universal Graphics Adapter"
                            : L"GOP Not Available ... Fall Back on UGA"
            );
            ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
            LOG_MSG("INFO: %s", MsgStr);
            MY_FREE_POOL(MsgStr);

            PrevFlag = TRUE;
            #endif
        } // if GOPDraw == NULL
    } // if UGADraw != NULL

    #if REFIT_DEBUG > 0
    Status = EFI_NOT_STARTED;
    #endif

// DA-TAG: Limit to TianoCore
BOOLEAN NewAppleFramebuffers = FALSE;
#ifdef __MAKEWITH_TIANO
    if (GOPDraw != NULL) {
        if (GlobalConfig.UseTextRenderer || GlobalConfig.TextOnly) {
            // Implement Text Renderer
            Status = OcUseBuiltinTextOutput (
                (egHasGraphics)
                    ? EfiConsoleControlScreenGraphics
                    : EfiConsoleControlScreenText
            );
        }
    }
    else if (UGADraw != NULL) {
        if (GlobalConfig.SupplyAppleFB && AppleFramebuffers == 0) {
            #if REFIT_DEBUG > 0
            PrevFlag = FALSE;
            LOG_MSG("\n\n");
            #endif

            // Install AppleFramebuffers and Update AppleFramebuffer Count
            RP_AppleFbInfoInstallProtocol (TRUE);
            AppleFramebuffers = egCountAppleFramebuffers();

            if (AppleFramebuffers > 0) {
                NewAppleFramebuffers = TRUE;
            }
        }
    }
#endif

    #if REFIT_DEBUG > 0
    MsgStr = PoolPrint (L"Implement Text Renderer ... %r", Status);
    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
    if (PrevFlag) {
        LOG_MSG("%s      ", OffsetNext);
    }
    else {
        LOG_MSG("INFO: ");
    }
    LOG_MSG("%s", MsgStr);
    MY_FREE_POOL(MsgStr);
    #endif

    if (!egHasGraphics) {
        #if REFIT_DEBUG > 0
        MsgStr = StrDuplicate (L"No");
        #endif
    }
    else if (NewAppleFramebuffers) {
        #if REFIT_DEBUG > 0
        MsgStr = StrDuplicate (L"Possibly via New Framebuffers ... Try 'TextOnly' if Not");
        #endif
    }
    else if (!FlagUGA || !AppleFirmware) {
        #if REFIT_DEBUG > 0
        MsgStr = StrDuplicate (L"Yes");
        #endif
    }
    else {
        // Force Text Mode ... AppleFramebuffers Missing on Mac with UGA
        UGADraw               =  NULL;
        GOPDraw               =  NULL;
        egHasGraphics         = FALSE;
        GlobalConfig.TextOnly = ForceTextOnly = TRUE;

        Status = EFI_ALREADY_STARTED;
        if (!GlobalConfig.UseTextRenderer) {
            // Force Text Renderer
            Status = OcUseBuiltinTextOutput (EfiConsoleControlScreenText);
        }

        #if REFIT_DEBUG > 0
        MsgStr = PoolPrint (
            L"Yes (Without Display ... Forcing Text Mode%s)",
            (Status == EFI_ALREADY_STARTED)
                ? L""
                : L" and Renderer"
        );
        #endif
    }

    #if REFIT_DEBUG > 0
    LOG_MSG("%s      Graphics Available:- '%s'", OffsetNext, MsgStr);
    LOG_MSG("\n\n");
    MY_FREE_POOL(MsgStr);
    #endif
} // VOID egInitScreen()

// Convert a graphics mode (in *ModeWidth) to a width and height (returned in
// *ModeWidth and *Height, respectively).
// Returns TRUE if successful, FALSE if not (invalid mode, typically)
BOOLEAN egGetResFromMode (
    UINTN *ModeWidth,
    UINTN *Height
) {
    UINTN                                  Size;
    EFI_STATUS                             Status;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info = NULL;

    if ((ModeWidth != NULL) && (Height != NULL) && GOPDraw) {
        Status = REFIT_CALL_4_WRAPPER(
            GOPDraw->QueryMode, GOPDraw,
            *ModeWidth, &Size, &Info
        );
        if (!EFI_ERROR(Status) && (Info != NULL)) {
            *ModeWidth = Info->HorizontalResolution;
            *Height    = Info->VerticalResolution;

            MY_FREE_POOL(Info);

            return TRUE;
        }
    }

    return FALSE;
} // BOOLEAN egGetResFromMode()

// Sets the screen resolution to the specified value, if possible. If *ScreenHeight
// is 0 and GOP mode is detected, assume that *ScreenWidth contains a GOP mode
// number rather than a horizontal resolution. If the specified resolution is not
// valid, displays a warning with the valid modes on GOP (UEFI) systems, or silently
// fails on UGA (EFI 1.x) systems. Note that this function attempts to set ANY screen
// resolution, even 1x1 or ridiculously large values.
// Upon success, returns actual screen resolution in *ScreenWidth and *ScreenHeight.
// These values are unchanged upon failure.
// Returns TRUE if successful, FALSE if not.
BOOLEAN egSetScreenSize (
    IN OUT UINTN *ScreenWidth,
    IN OUT UINTN *ScreenHeight
) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;

    EFI_STATUS   Status  = EFI_SUCCESS;
    BOOLEAN      ModeSet = FALSE;
    UINTN        Size;
    UINT32       ModeNum = 0;
    UINT32       CurrentModeNum;
    UINT32       ScreenW;
    UINT32       ScreenH;
    UINT32       UgaDepth;
    UINT32       UgaRefreshRate;
    CHAR16      *MsgStr = NULL;

    #if REFIT_DEBUG > 0
    LOG_MSG("Set Screen Size Manually ... H = %d and W = %d", ScreenHeight, ScreenWidth);
    LOG_MSG("\n");
    #endif

    if ((ScreenWidth == NULL) || (ScreenHeight == NULL)) {
        #if REFIT_DEBUG > 0
        MsgStr = StrDuplicate (
            L"WARN: ScreenWidth or ScreenHeight is 'NULL' in egSetScreenSize!!"
        );
        ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
        LOG_MSG("%s", MsgStr);
        LOG_MSG("\n");
        MY_FREE_POOL(MsgStr);
        #endif

        return FALSE;
    }

    if (GOPDraw != NULL) {
        // GOP mode (UEFI)
        CurrentModeNum = GOPDraw->Mode->Mode;

        #if REFIT_DEBUG > 0
        MsgStr = PoolPrint (
            L"GOPDraw Object Found ... Current Mode = %d",
            CurrentModeNum
        );
        ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
        LOG_MSG("  - %s", MsgStr);
        LOG_MSG("\n");
        MY_FREE_POOL(MsgStr);
        #endif

        if (*ScreenHeight == 0) {
            // User specified a mode number (stored in *ScreenWidth); use it directly
            ModeNum = (UINT32) *ScreenWidth;
            if (ModeNum != CurrentModeNum) {
                #if REFIT_DEBUG > 0
                MsgStr = StrDuplicate (L"GOP Mode Set from Configured Screen Width");
                #endif

                ModeSet = TRUE;
            }
            else if (egGetResFromMode (ScreenWidth, ScreenHeight) &&
                REFIT_CALL_2_WRAPPER(GOPDraw->SetMode, GOPDraw, ModeNum) == EFI_SUCCESS
            ) {
                #if REFIT_DEBUG > 0
                MsgStr = PoolPrint (L"Setting GOP Mode to %d", ModeNum);
                #endif

                ModeSet = TRUE;
            }
            else {
                #if REFIT_DEBUG > 0
                MsgStr = StrDuplicate (L"Could Not Set GOP Mode");
                #endif
            }
            #if REFIT_DEBUG > 0
            ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
            LOG_MSG("  - %s", MsgStr);
            LOG_MSG("\n\n");
            MY_FREE_POOL(MsgStr);
            #endif
        }
        else {
            // Do a loop through the modes to see if the specified one is available.
            // Switch to it if so.
            do {
                Status = REFIT_CALL_4_WRAPPER(
                    GOPDraw->QueryMode, GOPDraw,
                    ModeNum, &Size, &Info
                );
                if ((!EFI_ERROR(Status)) &&
                    (Size >= sizeof (*Info) &&
                    (Info != NULL)) &&
                    (Info->HorizontalResolution == *ScreenWidth) &&
                    (Info->VerticalResolution   == *ScreenHeight) &&
                    (
                        ModeNum == CurrentModeNum ||
                        REFIT_CALL_2_WRAPPER(GOPDraw->SetMode, GOPDraw, ModeNum) == EFI_SUCCESS
                    )
                ) {
                    #if REFIT_DEBUG > 0
                    MsgStr = PoolPrint (L"Setting GOP Mode to %d", ModeNum);
                    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                    LOG_MSG("  - %s", MsgStr);
                    LOG_MSG("\n\n");
                    MY_FREE_POOL(MsgStr);
                    #endif

                    ModeSet = TRUE;
                }
                else {
                    MsgStr = StrDuplicate (L"Could Not Set GOP Mode");

                    #if REFIT_DEBUG > 0
                    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                    LOG_MSG("  - %s", MsgStr);
                    LOG_MSG("\n\n");
                    #endif

                    PrintUglyText (MsgStr, NEXTLINE);
                    MY_FREE_POOL(MsgStr);
                }

                MY_FREE_POOL(Info);
            } while ((++ModeNum < GOPDraw->Mode->MaxMode) && !ModeSet);
        } // if/else *ScreenHeight == 0

        if (ModeSet) {
            egScreenWidth  = *ScreenWidth;
            egScreenHeight = *ScreenHeight;
        }
        else {
            MsgStr = StrDuplicate (
                L"Invalid Resolution Setting Provided ... Trying Default Mode"
            );

            #if REFIT_DEBUG > 0
            ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
            LOG_MSG("%s:", MsgStr);
            LOG_MSG("\n");
            MY_FREE_POOL(MsgStr);
            #endif

            PrintUglyText (MsgStr, NEXTLINE);
            MY_FREE_POOL(MsgStr);

            ModeNum = 0;
            do {

                #if REFIT_DEBUG > 0
                LOG_MSG("\n");
                #endif

                Status = REFIT_CALL_4_WRAPPER(
                    GOPDraw->QueryMode, GOPDraw,
                    ModeNum, &Size, &Info
                );
                if (!EFI_ERROR(Status) && (Info != NULL)) {
                    #if REFIT_DEBUG > 0
                    MsgStr = PoolPrint (
                        L"Available Mode: Mode[%02d][%d x %d]",
                        ModeNum,
                        Info->HorizontalResolution,
                        Info->VerticalResolution
                    );
                    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                    LOG_MSG("  - %s", MsgStr);
                    MY_FREE_POOL(MsgStr);
                    #endif

                    if (ModeNum == CurrentModeNum) {
                        egScreenWidth  = Info->HorizontalResolution;
                        egScreenHeight = Info->VerticalResolution;
                    }
                }
                else {
                    MsgStr = StrDuplicate (L"Error : Could Not Query GOP Mode");

                    #if REFIT_DEBUG > 0
                    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                    LOG_MSG("  - %s", MsgStr);
                    #endif

                    PrintUglyText (MsgStr, NEXTLINE);
                    MY_FREE_POOL(MsgStr);
                }
            } while (++ModeNum < GOPDraw->Mode->MaxMode);

            #if REFIT_DEBUG > 0
            LOG_MSG("\n\n");
            #endif
        } // if GOP mode (UEFI)
    }
    else if ((UGADraw != NULL) && (*ScreenHeight > 0)) {
        // UGA mode (EFI 1.x)
        // Try to use current color depth & refresh rate for new mode. Maybe not the best choice
        // in all cases, but I do not know how to probe for alternatives.
        REFIT_CALL_5_WRAPPER(
            UGADraw->GetMode, UGADraw,
            &ScreenW, &ScreenH,
            &UgaDepth, &UgaRefreshRate
        );
        #if REFIT_DEBUG > 0
        ALT_LOG(1, LOG_LINE_NORMAL, L"Setting UGADraw Mode to %d x %d", *ScreenWidth, *ScreenHeight);
        #endif

        Status = REFIT_CALL_5_WRAPPER(
            UGADraw->SetMode, UGADraw,
            ScreenW, ScreenH,
            UgaDepth, UgaRefreshRate
        );
        *ScreenWidth  = (UINTN) ScreenW;
        *ScreenHeight = (UINTN) ScreenH;
        if (!EFI_ERROR(Status)) {
            ModeSet        = TRUE;
            egScreenWidth  = *ScreenWidth;
            egScreenHeight = *ScreenHeight;
        }
        else {
            // TODO: Find a list of supported modes and display it.
            // NOTE: Below does not actually appear unless we explicitly switch to text mode.
            // This is just a placeholder until something better can be done.
            MsgStr = PoolPrint (
                L"Error Setting %d x %d Resolution ... Unsupported Mode",
                *ScreenWidth,
                *ScreenHeight
            );
            PrintUglyText (MsgStr, NEXTLINE);

            #if REFIT_DEBUG > 0
            ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
            LOG_MSG("%s", MsgStr);
            LOG_MSG("\n");

            #endif

            MY_FREE_POOL(MsgStr);
        }
    } // if/else if UGADraw != NULL

    return (ModeSet);
} // BOOLEAN egSetScreenSize()

// Set a text mode.
// Returns TRUE if the mode actually changed, FALSE otherwise.
// Note that a FALSE return value can mean either an error or no change
// necessary.
BOOLEAN egSetTextMode (
    UINT32 RequestedMode
) {
    EFI_STATUS   Status;
    BOOLEAN      ChangedIt = FALSE;
    UINTN        i = 0;
    UINTN        Width;
    UINTN        Height;
    CHAR16      *MsgStr = NULL;

    if ((RequestedMode != DONT_CHANGE_TEXT_MODE) &&
        (RequestedMode != gST->ConOut->Mode->Mode)
    ) {
        #if REFIT_DEBUG > 0
        ALT_LOG(1, LOG_LINE_NORMAL, L"Setting Text Mode to %d", RequestedMode);
        #endif

        Status = REFIT_CALL_2_WRAPPER(gST->ConOut->SetMode, gST->ConOut, RequestedMode);
        if (!EFI_ERROR(Status)) {
            ChangedIt = TRUE;
        }
        else {
            SwitchToText (FALSE);

            MsgStr = StrDuplicate (L"Error Setting Text Mode ... Unsupported Mode!!");
            PrintUglyText (MsgStr, NEXTLINE);

            #if REFIT_DEBUG > 0
            LOG_MSG("%s", MsgStr);
            LOG_MSG("\n");
            #endif

            MY_FREE_POOL(MsgStr);

            MsgStr = StrDuplicate (L"Seek Available Modes:");
            PrintUglyText (MsgStr, NEXTLINE);

            #if REFIT_DEBUG > 0
            ALT_LOG(1, LOG_LINE_NORMAL,
                L"Error Setting Text Mode %d ... Available Modes Are:",
                RequestedMode
            );
            LOG_MSG("%s", MsgStr);
            LOG_MSG("\n");
            #endif

            MY_FREE_POOL(MsgStr);

            do {
                Status = REFIT_CALL_4_WRAPPER(
                    gST->ConOut->QueryMode, gST->ConOut,
                    i, &Width, &Height
                );
                if (!EFI_ERROR(Status)) {
                    MsgStr = PoolPrint (L"  - Mode[%d] (%d x %d)", i, Width, Height);
                    PrintUglyText (MsgStr, NEXTLINE);

                    #if REFIT_DEBUG > 0
                    ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
                    LOG_MSG("%s", MsgStr);
                    LOG_MSG("\n");
                    #endif

                    MY_FREE_POOL(MsgStr);
                }
            } while (++i < gST->ConOut->Mode->MaxMode);

            MsgStr = PoolPrint (L"Use Default Mode[%d]:", DONT_CHANGE_TEXT_MODE);
            PrintUglyText (MsgStr, NEXTLINE);

            #if REFIT_DEBUG > 0
            ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
            LOG_MSG("%s", MsgStr);
            LOG_MSG("\n");
            #endif

            PauseForKey();
            SwitchToGraphicsAndClear (TRUE);
            MY_FREE_POOL(MsgStr);
        } // if/else successful change
    } // if need to change mode

    return ChangedIt;
} // BOOLEAN egSetTextMode()

CHAR16 * egScreenDescription (VOID) {
    CHAR16  *GraphicsInfo  = NULL;
    CHAR16  *TextInfo      = NULL;

    if (!egHasGraphics) {
        GraphicsInfo = PoolPrint (
            L"Text-Only Console: %d x %d",
            ConWidth, ConHeight
        );
    }
    else {
        if (GOPDraw != NULL) {
            GraphicsInfo = PoolPrint (
                L"Graphics Output Protocol @ %d x %d",
                egScreenWidth, egScreenHeight
            );
        }
        else if (UGADraw != NULL) {
            GraphicsInfo = PoolPrint (
                L"Universal Graphics Adapter @ %d x %d",
                egScreenWidth, egScreenHeight
            );
        }
        else {
            GraphicsInfo = StrDuplicate (L"Could Not Get Graphics Details");
        }

        if (!AllowGraphicsMode) {
            // Graphics Capable Hardware in Text Mode
            TextInfo = PoolPrint (
                L"(Text Mode: %d x %d [Graphics Capable])",
                ConWidth, ConHeight
            );
            MergeStrings (&GraphicsInfo, TextInfo, L' ');
        }
    }

    MY_FREE_POOL(TextInfo);

    return GraphicsInfo;
} // CHAR16 * egScreenDescription()

BOOLEAN egHasGraphicsMode (VOID) {
    return egHasGraphics;
} // BOOLEAN egHasGraphicsMode()

BOOLEAN egIsGraphicsModeEnabled (VOID) {
    EFI_CONSOLE_CONTROL_SCREEN_MODE CurrentMode;

    if (ConsoleControl != NULL) {
        REFIT_CALL_4_WRAPPER(
            ConsoleControl->GetMode, ConsoleControl,
            &CurrentMode, NULL, NULL
        );

        return (CurrentMode == EfiConsoleControlScreenGraphics) ? TRUE : FALSE;
    }

    return FALSE;
} // BOOLEAN egIsGraphicsModeEnabled()

VOID egSetGraphicsModeEnabled (
    IN BOOLEAN Enable
) {
    EFI_CONSOLE_CONTROL_SCREEN_MODE CurrentMode;
    EFI_CONSOLE_CONTROL_SCREEN_MODE NewMode;

    #if REFIT_DEBUG > 1
    CHAR16 *FuncTag = L"egSetGraphicsModeEnabled";
    #endif

    LOG_SEP(L"X");
    LOG_INCREMENT();
    BREAD_CRUMB(L"%s:  1 - START", FuncTag);

    if (ConsoleControl == NULL) {
        BREAD_CRUMB(L"%s:  1a 1 - END:- VOID (Aborted ... ConsoleControl == NULL)", FuncTag);
        LOG_DECREMENT();
        LOG_SEP(L"X");

        // Early Return
        return;
    }

    BREAD_CRUMB(L"%s:  2", FuncTag);
    REFIT_CALL_4_WRAPPER(
        ConsoleControl->GetMode, ConsoleControl,
        &CurrentMode, NULL, NULL
    );

    BREAD_CRUMB(L"%s:  3", FuncTag);
    if (Enable) {
        BREAD_CRUMB(L"%s:  3a 1 - (Tag for Graphics Mode)", FuncTag);
        NewMode = EfiConsoleControlScreenGraphics;
    }
    else {
        BREAD_CRUMB(L"%s:  3b 1 - (Tag for Text Mode)", FuncTag);
        NewMode = EfiConsoleControlScreenText;
    }

    BREAD_CRUMB(L"%s:  4", FuncTag);
    if (CurrentMode != NewMode) {
        BREAD_CRUMB(L"%s:  4a 1 - (Set to Tagged Mode)", FuncTag);
        REFIT_CALL_2_WRAPPER(ConsoleControl->SetMode, ConsoleControl, NewMode);
    }
    else {
        BREAD_CRUMB(L"%s:  4b 1 - (Tagged Mode is Already Active)", FuncTag);
    }

    BREAD_CRUMB(L"%s:  5 - END:- VOID", FuncTag);
    LOG_DECREMENT();
    LOG_SEP(L"X");
} // VOID egSetGraphicsModeEnabl()

//
// Drawing to the screen
//

VOID egClearScreen (
    IN EG_PIXEL *Color
) {
    EFI_UGA_PIXEL FillColor;

    #if REFIT_DEBUG > 1
    CHAR16 *FuncTag = L"egClearScreen";
    #endif

    LOG_SEP(L"X");
    LOG_INCREMENT();
    BREAD_CRUMB(L"%s:  1 - START", FuncTag);

    if (!egHasGraphics) {
        BREAD_CRUMB(L"%s:  1a 1 - END:- VOID (Clearing in Text Mode)", FuncTag);
        LOG_DECREMENT();
        LOG_SEP(L"X");

        // Try to clear in text mode
        REFIT_CALL_2_WRAPPER(gST->ConOut->SetAttribute, gST->ConOut, ATTR_BASIC);
        REFIT_CALL_1_WRAPPER(gST->ConOut->ClearScreen,  gST->ConOut);

        // Early Return
        return;
    }

    BREAD_CRUMB(L"%s:  2 - (Set Fill Colour)", FuncTag);
    if (Color != NULL) {
        BREAD_CRUMB(L"%s:  2a 1 - (Use Input Colour)", FuncTag);
        FillColor.Red   = Color->r;
        FillColor.Green = Color->g;
        FillColor.Blue  = Color->b;
    }
    else {
        BREAD_CRUMB(L"%s:  2b 1 - (Use Default Black)", FuncTag);
        FillColor.Red   = 0x0;
        FillColor.Green = 0x0;
        FillColor.Blue  = 0x0;
    }
    FillColor.Reserved = 0;

    BREAD_CRUMB(L"%s:  3", FuncTag);
    if (GOPDraw != NULL) {
        BREAD_CRUMB(L"%s:  3a 1 - (Apply Fill via GOP)", FuncTag);
        // EFI_GRAPHICS_OUTPUT_BLT_PIXEL and EFI_UGA_PIXEL have the same
        // layout and the TianoCore header file actually defines them
        // as being the same type.
        REFIT_CALL_10_WRAPPER(
            GOPDraw->Blt, GOPDraw,
            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *) &FillColor, EfiBltVideoFill,
             0, 0,
             0, 0,
             egScreenWidth, egScreenHeight, 0
         );
    }
    else if (UGADraw != NULL) {
        BREAD_CRUMB(L"%s:  3b 1 - (Apply Fill via UGA)", FuncTag);
        REFIT_CALL_10_WRAPPER(
            UGADraw->Blt, UGADraw,
            &FillColor, EfiUgaVideoFill,
            0, 0,
            0, 0,
            egScreenWidth, egScreenHeight, 0
        );
    }

    BREAD_CRUMB(L"%s:  4 - END:- VOID", FuncTag);
    LOG_DECREMENT();
    LOG_SEP(L"X");
} // VOID egClearScreen()

VOID egDrawImage (
    IN EG_IMAGE *Image,
    IN UINTN     ScreenPosX,
    IN UINTN     ScreenPosY
) {
    EG_IMAGE *CompImage = NULL;
    BOOLEAN   SetImage  = FALSE;

    // NOTE: Weird seemingly redundant tests because some placement code can "wrap around" and
    //   send "negative" values, which of course become very large unsigned ints that can then
    //   wrap around AGAIN if values are added to them.
    if ((!egHasGraphics)
        || ((ScreenPosX + Image->Width)  > egScreenWidth)
        || ((ScreenPosY + Image->Height) > egScreenHeight)
        || (ScreenPosX > egScreenWidth)
        || (ScreenPosY > egScreenHeight)
    ) {
        return;
    }

    if ((GlobalConfig.ScreenBackground == NULL) ||
        ((Image->Width == egScreenWidth) && (Image->Height == egScreenHeight))
    ) {
       CompImage = Image;
    }
    else if (GlobalConfig.ScreenBackground == Image) {
       CompImage = GlobalConfig.ScreenBackground;
    }
    else {
       CompImage = egCropImage (
           GlobalConfig.ScreenBackground,
           ScreenPosX, ScreenPosY,
           Image->Width, Image->Height
       );

       if (CompImage == NULL) {
           #if REFIT_DEBUG > 0
          LOG_MSG("Error! Cannot Crop Image in egDrawImage()!\n");
          #endif

          return;
       }

       egComposeImage (CompImage, Image, 0, 0);

       SetImage = TRUE;
    }

    if (GOPDraw != NULL) {
        REFIT_CALL_10_WRAPPER(
            GOPDraw->Blt, GOPDraw,
            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *) CompImage->PixelData, EfiBltBufferToVideo,
            0, 0,
            ScreenPosX, ScreenPosY,
            CompImage->Width, CompImage->Height, 0
        );
    }
    else if (UGADraw != NULL) {
        REFIT_CALL_10_WRAPPER(
            UGADraw->Blt, UGADraw,
            (EFI_UGA_PIXEL *) CompImage->PixelData, EfiUgaBltBufferToVideo,
            0, 0,
            ScreenPosX, ScreenPosY,
            CompImage->Width, CompImage->Height, 0
        );
    }

    if (SetImage) {
        MY_FREE_IMAGE(CompImage);
    }
} // VOID egDrawImage()

// Display an unselected icon on the screen, so that the background image shows
// through the transparency areas. The BadgeImage may be NULL, in which case
// it is not composited in.
VOID egDrawImageWithTransparency (
    EG_IMAGE *Image,
    EG_IMAGE *BadgeImage,
    UINTN     XPos,
    UINTN     YPos,
    UINTN     Width,
    UINTN     Height
) {
    EG_IMAGE *Background;

    Background = egCropImage (
        GlobalConfig.ScreenBackground,
        XPos, YPos,
        Width, Height
    );

    if (Background != NULL) {
        BltImageCompositeBadge (
            Background,
            Image, BadgeImage,
            XPos, YPos
        );
        MY_FREE_IMAGE(Background);
    }
} // VOID DrawImageWithTransparency()

VOID egDrawImageArea (
    IN EG_IMAGE *Image,
    IN UINTN     AreaPosX,
    IN UINTN     AreaPosY,
    IN UINTN     AreaWidth,
    IN UINTN     AreaHeight,
    IN UINTN     ScreenPosX,
    IN UINTN     ScreenPosY
) {
    if (!egHasGraphics) {
        return;
    }

    egRestrictImageArea (
        Image,
        AreaPosX, AreaPosY,
        &AreaWidth, &AreaHeight
    );

    if (AreaWidth == 0 || AreaHeight == 0) {
        return;
    }

    if (GOPDraw != NULL) {
        REFIT_CALL_10_WRAPPER(
            GOPDraw->Blt, GOPDraw,
            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *) Image->PixelData, EfiBltBufferToVideo,
            AreaPosX, AreaPosY,
            ScreenPosX, ScreenPosY,
            AreaWidth, AreaHeight, Image->Width * 4
        );
    }
    else if (UGADraw != NULL) {
        REFIT_CALL_10_WRAPPER(
            UGADraw->Blt, UGADraw,
            (EFI_UGA_PIXEL *) Image->PixelData, EfiUgaBltBufferToVideo,
            AreaPosX, AreaPosY,
            ScreenPosX, ScreenPosY,
            AreaWidth, AreaHeight, Image->Width * 4
        );
    }
} // VOID egDrawImageArea()

static
VOID egDisplayMessageEx (
    CHAR16    *Text,
    EG_PIXEL  *BGColor,
    UINTN      PositionCode,
    BOOLEAN    ResetPosition
) {
    UINTN BoxWidth, BoxHeight;
    static UINTN Position = 1;
    EG_IMAGE *Box;

    if (Text == NULL || BGColor == NULL) {
        // Early Return
        return;
    }

    egMeasureText (Text, &BoxWidth, &BoxHeight);
    BoxWidth  += 14;
    BoxHeight *=  2;
    if (BoxWidth > egScreenWidth) {
        BoxWidth = egScreenWidth;
    }
    Box = egCreateFilledImage (BoxWidth, BoxHeight, FALSE, BGColor);

    if (!ResetPosition) {
        // Get Luminance Index
        UINTN LumIndex = GetLumIndex (
            (UINTN) BGColor->r,
            (UINTN) BGColor->g,
            (UINTN) BGColor->b
        );

        egRenderText (
            Text, Box, 7,
            BoxHeight / 4,
            (UINT8) LumIndex
        );
    }

    switch (PositionCode) {
        case TOP:     Position  = 1;                                  break;
        case CENTER:  Position  = ((egScreenHeight - BoxHeight) / 2); break;
        case BOTTOM:  Position  = (egScreenHeight - (BoxHeight * 2)); break;
        default:      Position += (BoxHeight + (BoxHeight / 10));     break; // NEXTLINE
    } // switch

    if (ResetPosition) {
        if (PositionCode != TOP && PositionCode != CENTER && PositionCode != BOTTOM) {
            Position -= (BoxHeight + (BoxHeight / 10));
        }
    }

    egDrawImage (Box, (egScreenWidth - BoxWidth) / 2, Position);

    if ((PositionCode == CENTER) || (Position >= egScreenHeight - (BoxHeight * 5))) {
        Position = 1;
    }
} // VOID egDisplayMessageEx()

// Display a message in the center of the screen, surrounded by a box of the
// specified color. For the moment, uses graphics calls only. (It still works
// in text mode on GOP/UEFI systems, but not on UGA/EFI 1.x systems.)
VOID egDisplayMessage (
    CHAR16    *Text,
    EG_PIXEL  *BGColor,
    UINTN      PositionCode,
    UINTN      PauseLength,
    CHAR16    *PauseType     OPTIONAL
) {
    if (Text == NULL || BGColor == NULL) {
        // Early Return
        return;
    }

    // Display the message
    egDisplayMessageEx (Text, BGColor, PositionCode, FALSE);

    if (PauseType && PauseLength > 0) {
        if (MyStriCmp (PauseType, L"HaltSeconds")) {
            HaltSeconds (PauseLength);
        }
        else {
            PauseSeconds (PauseLength);
        }

        // Erase the message
        egDisplayMessageEx (Text, &MenuBackgroundPixel, PositionCode, TRUE);
    }
} // VOID egDisplayMessage()

// Copy the current contents of the display into an EG_IMAGE.
// Returns pointer if successful, NULL if not.
EG_IMAGE * egCopyScreen (VOID) {
   return egCopyScreenArea (0, 0, egScreenWidth, egScreenHeight);
} // EG_IMAGE * egCopyScreen()

// Copy the current contents of the specified display area into an EG_IMAGE.
// Returns pointer if successful, NULL if not.
EG_IMAGE * egCopyScreenArea (
    UINTN XPos,  UINTN YPos,
    UINTN Width, UINTN Height
) {
    EG_IMAGE *Image = NULL;

   if (!egHasGraphics) {
       return NULL;
   }

   // allocate a buffer for the screen area
   Image = egCreateImage (Width, Height, FALSE);
   if (Image == NULL) {
      return NULL;
   }

   // Get full screen image
   if (GOPDraw != NULL) {
       REFIT_CALL_10_WRAPPER(
           GOPDraw->Blt, GOPDraw,
           (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *) Image->PixelData, EfiBltVideoToBltBuffer,
           XPos, YPos,
           0, 0,
           Image->Width, Image->Height, 0
       );
   }
   else if (UGADraw != NULL) {
       REFIT_CALL_10_WRAPPER(
           UGADraw->Blt, UGADraw,
           (EFI_UGA_PIXEL *) Image->PixelData, EfiUgaVideoToBltBuffer,
           XPos, YPos,
           0, 0,
           Image->Width, Image->Height, 0
       );
   }

   return Image;
} // EG_IMAGE * egCopyScreenArea()


// Make a screenshot
VOID egScreenShot (VOID) {
    EFI_STATUS    Status;
    EFI_FILE     *BaseDir;
    EG_IMAGE     *Image;
    UINT8        *FileData;
    UINT8         Temp;
    UINTN         i;
    UINTN         FileDataSize;         ///< Size in bytes
    UINTN         FilePixelSize;        ///< Size in pixels
    CHAR16       *FileName    = NULL;
    CHAR16       *MsgStr      = NULL;
    EG_PIXEL      BGColorWarn = COLOR_RED;
    EG_PIXEL      BGColorGood = COLOR_LIGHTBLUE;

    #if REFIT_DEBUG > 0
    BOOLEAN CheckMute = FALSE;

    // Clear the Keystroke Buffer (Silently)
    ReadAllKeyStrokes();
    #endif

    #if REFIT_DEBUG > 0
    LOG_MSG("Received User Input:");
    LOG_MSG("%s  - Take Screenshot", OffsetNext);
    #endif

    Image = egCopyScreen();
    if (Image == NULL) {
        MsgStr = StrDuplicate (L"Unable to Take Screenshot ... Image is NULL");

        #if REFIT_DEBUG > 0
        MY_MUTELOGGER_SET;
        #endif
        egDisplayMessage (
            MsgStr, &BGColorWarn, CENTER,
            4, L"HaltSeconds"
        );
        #if REFIT_DEBUG > 0
        MY_MUTELOGGER_OFF;
        #endif

        #if REFIT_DEBUG > 0
        ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
        LOG_MSG("%s    ** WARN: %s", OffsetNext, MsgStr);
        LOG_MSG("\n\n");
        #endif

        MY_FREE_POOL(MsgStr);

        goto bailout_wait;
    }

    // Fix pixels
    FilePixelSize = Image->Width * Image->Height;
    for (i = 0; i < FilePixelSize; ++i) {
        Temp                   = Image->PixelData[i].b;
        Image->PixelData[i].b  = Image->PixelData[i].r;
        Image->PixelData[i].r  = Temp;
        Image->PixelData[i].a  = 0xFF;
    }

    // Encode as PNG
    Status = EncodeAsPNG (
        (VOID *)   Image->PixelData,
        (UINT32)   Image->Width,
        (UINT32)   Image->Height,
        (VOID **) &FileData,
        &FileDataSize
    );

    MY_FREE_IMAGE(Image);
    if (EFI_ERROR(Status)) {
        MsgStr = StrDuplicate (L"Could Not Encode PNG");

        #if REFIT_DEBUG > 0
        MY_MUTELOGGER_SET;
        #endif
        egDisplayMessage (
            MsgStr, &BGColorWarn, CENTER,
            4, L"HaltSeconds"
        );
        #if REFIT_DEBUG > 0
        MY_MUTELOGGER_OFF;
        #endif

        #if REFIT_DEBUG > 0
        ALT_LOG(1, LOG_LINE_NORMAL, L"%s", MsgStr);
        LOG_MSG("%s    ** WARN: %s", OffsetNext, MsgStr);
        LOG_MSG("\n\n");
        #endif

        MY_FREE_POOL(MsgStr);
        MY_FREE_POOL(FileData);

        return;
    }

    // Save to first available ESP if not running from ESP
    if (!MyStriCmp (SelfVolume->VolName, L"EFI") &&
        !MyStriCmp (SelfVolume->VolName, L"ESP")
    ) {
        Status = egFindESP (&BaseDir);
        if (EFI_ERROR(Status)) {
            MsgStr = StrDuplicate (L"Could Not Save Screenshot");

            #if REFIT_DEBUG > 0
            MY_MUTELOGGER_SET;
            #endif
            egDisplayMessage (
                MsgStr, &BGColorWarn, CENTER,
                4, L"HaltSeconds"
            );
            #if REFIT_DEBUG > 0
            MY_MUTELOGGER_OFF;
            #endif

            #if REFIT_DEBUG > 0
            LOG_MSG("%s    ** WARN: %s", OffsetNext, MsgStr);
            LOG_MSG("\n\n");
            #endif

            MY_FREE_POOL(MsgStr);
            MY_FREE_POOL(FileData);

            return;
        }
    }

    SelfRootDir = LibOpenRoot (SelfLoadedImage->DeviceHandle);
    if (SelfRootDir != NULL) {
        BaseDir = SelfRootDir;
    }
    else {
        // Try to save to first available ESP
        Status = egFindESP (&BaseDir);
        if (EFI_ERROR(Status)) {
            MsgStr = StrDuplicate (L"Could Not Find ESP for Screenshot");

            #if REFIT_DEBUG > 0
            MY_MUTELOGGER_SET;
            #endif
            egDisplayMessage (
                MsgStr, &BGColorWarn, CENTER,
                4, L"HaltSeconds"
            );
            #if REFIT_DEBUG > 0
            MY_MUTELOGGER_OFF;
            #endif

            #if REFIT_DEBUG > 0
            LOG_MSG("%s    ** WARN: %s", OffsetNext, MsgStr);
            LOG_MSG("\n\n");
            #endif

            MY_FREE_POOL(MsgStr);
            MY_FREE_POOL(FileData);

            return;
        }
    }

    // Search for existing screen shot files ... Increment index to an unused value
    i = 0;
    do {
        MY_FREE_POOL(FileName);
        FileName = PoolPrint (L"ScreenShot_%03d.png", i++);

        // Exit if is ndex is greater than 999
        if (i > 999) {
            MsgStr = StrDuplicate (L"Excessive Number of Saved Screenshot Files Found");

            #if REFIT_DEBUG > 0
            MY_MUTELOGGER_SET;
            #endif
            egDisplayMessage (
                MsgStr, &BGColorWarn, CENTER,
                4, L"HaltSeconds"
            );
            #if REFIT_DEBUG > 0
            MY_MUTELOGGER_OFF;
            #endif

            #if REFIT_DEBUG > 0
            LOG_MSG("%s    ** WARN: %s", OffsetNext, MsgStr);
            LOG_MSG("\n\n");
            #endif

            MY_FREE_POOL(MsgStr);
            MY_FREE_POOL(FileData);

            return;
        }
    } while (FileExists (BaseDir, FileName));

    // Save to file on the ESP
    Status = egSaveFile (BaseDir, FileName, (UINT8 *) FileData, FileDataSize);
    if (CheckError (Status, L"in egSaveFile")) {
        MY_FREE_POOL(FileName);
        MY_FREE_POOL(FileData);

        goto bailout_wait;
    }

    MsgStr = StrDuplicate (L"Saved Screenshot");

    #if REFIT_DEBUG > 0
    LOG_MSG("%s    * %s:- '%s'", OffsetNext, MsgStr, FileName);
    LOG_MSG("\n\n");
    #endif

    #if REFIT_DEBUG > 0
    MY_MUTELOGGER_SET;
    #endif
    egDisplayMessage (
        MsgStr, &BGColorGood, CENTER,
        2, L"HaltSeconds"
    );
    #if REFIT_DEBUG > 0
    MY_MUTELOGGER_OFF;
    #endif

    MY_FREE_POOL(MsgStr);
    MY_FREE_POOL(FileName);
    MY_FREE_POOL(FileData);

    return;

    // DEBUG: Switch to Text Mode
bailout_wait:
    i = 0;
    egSetGraphicsModeEnabled (FALSE);
    REFIT_CALL_3_WRAPPER(
        gBS->WaitForEvent, 1,
        &gST->ConIn->WaitForKey, &i
    );
} // VOID egScreenShot()

/* EOF */
