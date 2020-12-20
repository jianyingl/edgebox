#if !defined(__LOGGER_H__)
#define __LOGGER_H__

typedef enum {
    LogLevel_DEBUG2,   /* Even more debug */
    LogLevel_DEBUG1,   /* More debug */
    LogLevel_DEBUG,    /* Debug */
    LogLevel_INFO,
    LogLevel_ERROR,
    LogLevel_MAXLEVEL
} LogLevel;


#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define LOG_DEBUG(fmt, ...) logger_log(LogLevel_DEBUG, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG1(fmt, ...) logger_log(LogLevel_DEBUG1, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG2(fmt, ...) logger_log(LogLevel_DEBUG2, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  logger_log(LogLevel_INFO , __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logger_log(LogLevel_ERROR, __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

void logging_init();
static long getFileSize(const char* filename);
static int logger_isEnabled(LogLevel level);
static int rotateLogFiles(void);
static void getBackupFileName(const char* basename, unsigned char index, char* backupname, size_t size);
static int isFileExist(const char* filename);
static void getTimestamp(const struct timeval* time, char* timestamp, size_t size);

void logger_log(LogLevel level, const char* file, int line, const char* fmt, ...);

extern LogLevel s_logLevel;
extern char * log_level_string[LogLevel_MAXLEVEL];
extern char s_logFile[];
#endif
