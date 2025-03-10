/*
 *  BootLog.c
 *
 *  Created by Slice  2011-08.19
 *  Edited by apianti 2012-09-08
 *
 *  Refactored by Dayo Akanji for RefindPlus
 */

#include "../../include/tiano_includes.h"
#include "MemLogLib.h"
#include "../../BootMaster/global.h"

#if REFIT_DEBUG > 0
// DBG Build Only - START

#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>
#include <Library/UefiBootServicesTableLib.h>
#include "../../BootMaster/lib.h"
#include "../../BootMaster/mystrings.h"
#include "../../include/refit_call_wrapper.h"

extern  EFI_GUID  gEfiMiscSubClassGuid;

extern  INT16  NowYear;
extern  INT16  NowMonth;
extern  INT16  NowDay;
extern  INT16  NowHour;
extern  INT16  NowMinute;
extern  INT16  NowSecond;

CHAR16  *PadStr    = NULL;
CHAR16  *gLogTemp  = NULL;
CHAR16  *mDebugLog = NULL;

BOOLEAN  TimeStamp =  TRUE;
BOOLEAN  UseMsgLog = FALSE;
BOOLEAN  DelMsgLog = FALSE;

EFI_FILE_PROTOCOL *mRootDir = NULL;

static
CHAR16 * GetAltMonth (VOID) {
    CHAR16 *AltMonth = NULL;

    switch (NowMonth) {
        case  1: AltMonth = L"b";  break;
        case  2: AltMonth = L"d";  break;
        case  3: AltMonth = L"f";  break;
        case  4: AltMonth = L"h";  break;
        case  5: AltMonth = L"j";  break;
        case  6: AltMonth = L"k";  break;
        case  7: AltMonth = L"n";  break;
        case  8: AltMonth = L"p";  break;
        case  9: AltMonth = L"r";  break;
        case 10: AltMonth = L"t";  break;
        case 11: AltMonth = L"v";  break;
        default: AltMonth = L"x";  break;
    } // switch

    return AltMonth;
} // CHAR16 * GetAltMonth()

static
CHAR16 * GetAltHour (VOID) {
    CHAR16 *AltHour = NULL;

    switch (NowHour) {
        case  0: AltHour = L"a";  break;
        case  1: AltHour = L"b";  break;
        case  2: AltHour = L"c";  break;
        case  3: AltHour = L"d";  break;
        case  4: AltHour = L"e";  break;
        case  5: AltHour = L"f";  break;
        case  6: AltHour = L"g";  break;
        case  7: AltHour = L"h";  break;
        case  8: AltHour = L"i";  break;
        case  9: AltHour = L"j";  break;
        case 10: AltHour = L"k";  break;
        case 11: AltHour = L"m";  break;
        case 12: AltHour = L"n";  break;
        case 13: AltHour = L"p";  break;
        case 14: AltHour = L"q";  break;
        case 15: AltHour = L"r";  break;
        case 16: AltHour = L"s";  break;
        case 17: AltHour = L"t";  break;
        case 18: AltHour = L"u";  break;
        case 19: AltHour = L"v";  break;
        case 20: AltHour = L"w";  break;
        case 21: AltHour = L"x";  break;
        case 22: AltHour = L"y";  break;
        default: AltHour = L"z";  break;
    } // switch

    return AltHour;
} // CHAR16 * GetAltHour()

static
CHAR16 * GetDateString (VOID) {
    static CHAR16 *DateStr = NULL;

    if (DateStr != NULL) {
        return DateStr;
    }

    INT16    ourYear   = (NowYear % 100);
    CHAR16  *ourMonth  = GetAltMonth();
    CHAR16  *ourHour   = GetAltHour();
    DateStr = PoolPrint(
        L"%02d%s%02d%s%02d%02d",
        ourYear, ourMonth,
        NowDay, ourHour,
        NowMinute, NowSecond
    );

    return DateStr;
} // CHAR16 * GetDateString()

static
EFI_FILE_PROTOCOL * GetDebugLogFile (VOID) {
    EFI_STATUS           Status;
    EFI_LOADED_IMAGE    *LoadedImage;
    EFI_FILE_PROTOCOL   *LogProtocol;

    // DA-TAG: Always get 'LoadedImage->DeviceHandle' each time
    //         That is, do not use static
    Status = REFIT_CALL_3_WRAPPER(
        gBS->HandleProtocol, gImageHandle,
        &gEfiLoadedImageProtocolGuid, (VOID **) &LoadedImage
    );
    if (EFI_ERROR(Status) || !LoadedImage->DeviceHandle) {
        return NULL;
    }

    // DA-TAG: Always get 'mRootDir' each time
    //
    // Get mRootDir from device we are loaded from
    if (mRootDir != NULL) {
        mRootDir = NULL;
    }
    mRootDir = EfiLibOpenRoot (LoadedImage->DeviceHandle);
    if (mRootDir == NULL) {
        return NULL;
    }

    if (mDebugLog == NULL) {
        CHAR16 *DateStr = GetDateString();
        mDebugLog = PoolPrint (L"EFI\\%s.log", DateStr);
        MY_FREE_POOL(DateStr);
    }

    // Open log file from current root
    Status = REFIT_CALL_5_WRAPPER(
        mRootDir->Open, mRootDir,
        &LogProtocol, mDebugLog,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0
    );

    // Try to create log file if not found
    if (Status == EFI_NOT_FOUND) {
        REFIT_CALL_5_WRAPPER(
            mRootDir->Open, mRootDir,
            &LogProtocol, mDebugLog,
            ReadWriteCreate, 0
        );
    }

    Status  = REFIT_CALL_1_WRAPPER(mRootDir->Close, mRootDir);
    if (EFI_ERROR(Status)) {
        // Try on first EFI partition
        mRootDir = NULL;
        Status = egFindESP (&mRootDir);
        if (!EFI_ERROR(Status)) {
            // Try to locate log file
            Status = REFIT_CALL_5_WRAPPER(
                mRootDir->Open, mRootDir,
                &LogProtocol, mDebugLog,
                EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0
            );

            // Try to create log file if not found
            if (Status == EFI_NOT_FOUND) {
                REFIT_CALL_5_WRAPPER(
                    mRootDir->Open, mRootDir,
                    &LogProtocol, mDebugLog,
                    ReadWriteCreate, 0
                );
            }

            Status = REFIT_CALL_1_WRAPPER(mRootDir->Close, mRootDir);
        }

        if (EFI_ERROR(Status)) {
            mRootDir = LogProtocol = NULL;
        }
    }

    // DA-TAG: Do not set 'mRootDir' to NULL here
    //         May be used in caller when 'LogProtocol' is not NULL
    return LogProtocol;
} // static EFI_FILE_PROTOCOL * GetDebugLogFile()

static
VOID SaveMessageToDebugLogFile (
    IN CHAR8 *LastMessage
) {
    static
    BOOLEAN           FirstTimeSave = TRUE;
    CHAR8            *MemLogBuffer;
    CHAR8            *Text;
    UINTN             MemLogLen;
    UINTN             TextLen;
    EFI_FILE_INFO    *Info;
    EFI_FILE_HANDLE   LogFile;

    LogFile = GetDebugLogFile();
    if (LogFile == NULL) {
        return;
    }

    if (GlobalConfig.LogLevel < MINLOGLEVEL) {
        EFI_STATUS Status = REFIT_CALL_5_WRAPPER(
            mRootDir->Open, mRootDir,
            &LogFile, mDebugLog,
            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0
        );
        if (!EFI_ERROR(Status)) {
            Status = REFIT_CALL_1_WRAPPER(LogFile->Delete, LogFile);
            if (!EFI_ERROR(Status)) {
                REFIT_CALL_1_WRAPPER(mRootDir->Close, mRootDir);
                DelMsgLog = TRUE;
            }
        }
    }

    if (DelMsgLog && GlobalConfig.LogLevel < MINLOGLEVEL) {
        return;
    }

    // Advance to the EOF so we append
    Info = EfiLibFileInfo (LogFile);
    if (Info) {
        MemLogBuffer = GetMemLogBuffer();
        MemLogLen    = GetMemLogLen();
        Text         = LastMessage;
        TextLen      = AsciiStrLen (LastMessage);

        LogFile->SetPosition (LogFile, Info->FileSize);
        // Write out whole log if we have not had root before this
        if (FirstTimeSave) {
            Text          = MemLogBuffer;
            TextLen       = MemLogLen;
            FirstTimeSave = FALSE;
        }

        // Write out this message
        LogFile->Write (LogFile, &TextLen, Text);
    }

    LogFile->Close (LogFile);
} // static VOID SaveMessageToDebugLogFile()

VOID WayPointer (
    IN CHAR16 *Msg
) {
    if (gKernelStarted) {
        // Early Return
        return;
    }

    if (!Msg) {
        // Early Return
        return;
    }

    // Stash and swap LogLevel
    // Needed to force DeepLogger on LogLevel 0
    UINTN TmpLogLevelStore = GlobalConfig.LogLevel;
    if (GlobalConfig.LogLevel < 1) GlobalConfig.LogLevel = 1;

    // Call DeepLogger
    gLogTemp = StrDuplicate (Msg);
    DeepLoggger (1, LOG_LINE_EXIT, &gLogTemp);

    // Restore LogLevel if changed
    GlobalConfig.LogLevel = TmpLogLevelStore;
} // VOID WayPointer()

VOID DeepLoggger (
    IN INTN     level,
    IN INTN     type,
    IN CHAR16 **Msg
) {
    UINTN    Limit     = 213;
    CHAR8   *FormatMsg = NULL;
    CHAR16  *StoreMsg  = NULL;
    CHAR16  *Tmp       = NULL;

    if (!(*Msg)) {
        // Early Return
        return;
    }

    // Make sure we are able to write
    BOOLEAN  EarlyReturn = (
        REFIT_DEBUG <= MINLOGLEVEL
        || GlobalConfig.LogLevel < level
        || GlobalConfig.LogLevel == MINLOGLEVEL
        || (DelMsgLog && GlobalConfig.LogLevel < MINLOGLEVEL)
        || ((type != LOG_LINE_FORENSIC) && (NativeLogger || MuteLogger))
    );

    if (REFIT_DEBUG > MAXLOGLEVEL && type == LOG_BLOCK_SEP && !MuteLogger) {
        EarlyReturn = FALSE;
    }
    if (EarlyReturn) {
        MY_FREE_POOL(*Msg);

        return;
    }

    CHAR16 *OurPad = (PadStr) ? PadStr : L"[ ";

    // Truncate message at MAXLOGLEVEL and lower (if required)
    if (GlobalConfig.LogLevel <= MAXLOGLEVEL) {
        BOOLEAN LongStr = TruncateString (*Msg, Limit);

        StoreMsg = StrDuplicate (*Msg);
        MY_FREE_POOL(*Msg);
        *Msg = (LongStr)
            ? PoolPrint (L"%s ... Snipped!!", StoreMsg)
            : StrDuplicate (StoreMsg);
    }

    // Disable Timestamp
    TimeStamp = FALSE;

    switch (type) {
        case LOG_BLOCK_SEP:
        case LOG_BLANK_LINE_SEP: Tmp = StrDuplicate (L"\n");                                                    break;
        case LOG_STAR_HEAD_SEP:  Tmp = PoolPrint (L"\n                ***[ %s\n",                        *Msg); break;
        case LOG_STAR_SEPARATOR: Tmp = PoolPrint (L"\n* ** ** *** *** ***[ %s ]*** *** *** ** ** *\n\n", *Msg); break;
        case LOG_LINE_SEPARATOR: Tmp = PoolPrint (L"\n===================[ %s ]===================\n",   *Msg); break;
        case LOG_LINE_THIN_SEP:  Tmp = PoolPrint (L"\n-------------------[ %s ]-------------------\n",   *Msg); break;
        case LOG_LINE_DASH_SEP:  Tmp = PoolPrint (L"\n- - - - - - - - - -[ %s ]- - - - - - - - - -\n",   *Msg); break;
        case LOG_THREE_STAR_SEP: Tmp = PoolPrint (L"\n. . . . . . . . ***[ %s ]*** . . . . . . . .\n",   *Msg); break;
        case LOG_THREE_STAR_END: Tmp = PoolPrint (L"                ***[ %s ]***\n\n",                   *Msg); break;
        case LOG_THREE_STAR_MID: Tmp = PoolPrint (L"                ***[ %s\n",                          *Msg); break;
        case LOG_LINE_FORENSIC:  Tmp = PoolPrint (L"            !!! ---%s%s\n",                  OurPad, *Msg); break;
        case LOG_LINE_SPECIAL:   Tmp = PoolPrint (L"\n                   %s",                            *Msg); break;
        case LOG_LINE_SAME:      Tmp = PoolPrint (L"%s",                                                 *Msg); break;
        case LOG_LINE_EXIT:      Tmp = PoolPrint (L"\n %s\n\n",                                          *Msg); break;
        default:                 Tmp = PoolPrint (L"%s\n",                                               *Msg);
            // Should be 'LOG_LINE_NORMAL' ... Using 'default' to catch coding errors
            // Enable Timestamp for this
            TimeStamp = TRUE;
    } // switch

    if (Tmp) {
        FormatMsg = AllocatePool (
            (StrLen (Tmp) + 1) * sizeof (CHAR8)
        );
        if (FormatMsg) {
            // Use Native Logging
            UseMsgLog = TRUE;

            // Write the Message String to File
            UnicodeStrToAsciiStr (Tmp, FormatMsg);
            DebugLog ((const CHAR8 *) FormatMsg);

            // Disable Native Logging
            UseMsgLog = FALSE;
        }
    }

    MY_FREE_POOL(Tmp);
    MY_FREE_POOL(*Msg);
    MY_FREE_POOL(StoreMsg);
    MY_FREE_POOL(FormatMsg);
} // VOID DeepLoggger()

VOID EFIAPI DebugLog (
    IN const CHAR8 *FormatString,
    ...
) {
    // Abort if Kernel has started
    if(gKernelStarted) return;

    // Make sure writing is allowed/possible
    if (MuteLogger
        || REFIT_DEBUG < 1
        || FormatString == NULL
        || (DelMsgLog && GlobalConfig.LogLevel < MINLOGLEVEL)
    ) {
        return;
    }

    // Abort on higher log levels if not forcing
    if (!UseMsgLog) {
        UseMsgLog = NativeLogger;
        if (!UseMsgLog && GlobalConfig.LogLevel > MINLOGLEVEL) {
            return;
        }
    }

    // Print message to log buffer
    VA_LIST Marker;
    VA_START(Marker, FormatString);
    MemLogVA (TimeStamp, REFIT_DEBUG, FormatString, Marker);
    VA_END(Marker);

    TimeStamp = TRUE;
} // VOID EFIAPI DebugLog()

VOID LogPadding (BOOLEAN Increment) {
    if (MuteLogger) {
        // Early Return
        return;
    }

    if (!PadStr) {
        PadStr = StrDuplicate (L"[ ");

        // Early Return
        return;
    }

    CHAR16 *TmpPad = StrDuplicate (PadStr);
    UINTN   PadPos = StrLen (PadStr);
    FreePool (PadStr);
    PadStr = NULL;

    if (Increment == TRUE) {
        if (NativeLogger) {
            PadStr = StrDuplicate (TmpPad);
        }
        else {
            PadStr = PoolPrint (L"%s. ", TmpPad);
        }
    }
    else {
        if (NativeLogger) {
            PadStr = StrDuplicate (TmpPad);
        }
        else {
            PadPos = PadPos - 2;
            if (PadPos < 3) {
                PadStr = StrDuplicate (L"[ ");
            }
            else {
                TmpPad[PadPos] = L'\0';
                PadStr = StrDuplicate (TmpPad);
            }
        }
    }

    FreePool (TmpPad);
} // VOID LogPadding()

// DBG Build Only - END
#endif

// DA-TAG: Allow REL Build to access this ... Without output
static
VOID EFIAPI MemLogCallback (
    IN INTN   DebugMode,
    IN CHAR8 *LastMessage
) {
    #if REFIT_DEBUG > 0
    if (DebugMode >= 1) {
        SaveMessageToDebugLogFile (LastMessage);
    }
    #endif

    return;
} // static VOID EFIAPI MemLogCallback()

VOID InitBooterLog (VOID) {
    SetMemLogCallback (MemLogCallback);
} // VOID InitBooterLog()
