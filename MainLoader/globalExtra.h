#define REFIT_DEBUG (0)

extern CHAR16           *gLogTemp;

#define LOG_LINE_NORMAL      1
#define LOG_LINE_SEPARATOR   2
#define LOG_LINE_THIN_SEP    3


VOID
DebugLog (
  IN        INTN  DebugMode,
  IN  CONST CHAR8 *FormatString, ...
);

#if REFIT_DEBUG == 0
  #define MsgLog(...)
#else
  #define MsgLog(...)  DebugLog(REFIT_DEBUG, __VA_ARGS__)
#endif

VOID
DeepLog (
  IN  INTN     DebugMode,
  IN  INTN     level,
  IN  INTN     type,
  IN  CHAR16 **Message
);

#define LOG(level, type, ...) \
        gLogTemp = PoolPrint(__VA_ARGS__); \
        DeepLog(REFIT_DEBUG, level, type, &gLogTemp);
