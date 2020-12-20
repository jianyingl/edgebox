/*
 * logger.c
 *
 *  Created on: 2020/11/22
 *      Author: luojianying
 *
 *  Refer to: https://github.com/yksz/c-logger/blob/master/src/logger.c
 */


#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>


#include "logger.h"

char * log_level_string[LogLevel_MAXLEVEL] = {"DEBUG2", "DEBUG1", "DEBUG", "INFO", "ERROR"};
static pthread_mutex_t s_mutex;

/* File logger */
static struct {
    FILE* output;
    char filename[128];
    long maxFileSize;
    unsigned char maxBackupFiles;
    long currentFileSize;
    long flushedTime;
} s_flog;

static volatile long s_flushInterval = 0; /* msec, 0 is auto flush off */
static volatile int s_initialized = 0; /* false */
LogLevel s_logLevel = LogLevel_INFO;
char s_logFile[258];
static long vflog(FILE* fp, char *level, const char* timestamp, const char* file, int line, const char* fmt, va_list arg, long currentTime, long* flushedTime);



void logging_init(char *filename)
{
    if (s_initialized) {
        return;
    }

    pthread_mutex_init(&s_mutex, NULL);

    s_initialized = 1;

    s_flog.output = fopen(s_logFile, "a");
    if (s_flog.output == NULL) {
        fprintf(stderr, "ERROR: logger: Failed to open log file: `%s`\n", s_logFile);
        exit(-1);
    }

    s_flog.currentFileSize = getFileSize(s_logFile);
    strncpy(s_flog.filename, s_logFile, sizeof(s_flog.filename));
    s_flog.maxFileSize = 1024 * 1024 * 10;
    s_flog.maxBackupFiles = 2;
}

static long getFileSize(const char* filename)
{
    FILE* fp;
    long size;

    if ((fp = fopen(filename, "rb")) == NULL) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fclose(fp);
    return size;
}


static int logger_isEnabled(LogLevel level)
{
    return level >= s_logLevel;
}

void logger_log(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    struct timeval now;
    long currentTime; /* milliseconds */
    char levelc;
    char timestamp[32];
    va_list carg, farg;

    if (!s_initialized) {
        //assert(0 && "logger is not initialized");
        return;
    }

    if (!logger_isEnabled(level)) {
    	/* not printing for this level */
        return;
    }

    gettimeofday(&now, NULL);
    currentTime = now.tv_sec * 1000 + now.tv_usec / 1000;
    getTimestamp(&now, timestamp, sizeof(timestamp));

    pthread_mutex_lock(&s_mutex);
    if (rotateLogFiles()) {
        va_start(farg, fmt);
        s_flog.currentFileSize += vflog(s_flog.output, log_level_string[level], timestamp,
                file, line, fmt, farg, currentTime, &s_flog.flushedTime);
        va_end(farg);
    }
    pthread_mutex_unlock(&s_mutex);
}


static long vflog(FILE* fp, char *level, const char* timestamp,
        const char* file, int line, const char* fmt, va_list arg,
        long currentTime, long* flushedTime)
{
    int size;
    long totalsize = 0;

    if ((size = fprintf(fp, "%s %5s %s:%d: ", timestamp, level, file, line)) > 0) {
        totalsize += size;
    }
    if ((size = vfprintf(fp, fmt, arg)) > 0) {
        totalsize += size;
    }
    if ((size = fprintf(fp, "\n")) > 0) {
        totalsize += size;
    }

    fflush(fp);
    *flushedTime = currentTime;

    return totalsize;
}


static int rotateLogFiles(void)
{

    int i;

    char src[128], dst[128];

    if (s_flog.currentFileSize < s_flog.maxFileSize) {
    	/* dont need rotate */
        return s_flog.output != NULL;
    }

    /* need rotate */
    fclose(s_flog.output);
    for (i = (int) s_flog.maxBackupFiles; i > 0; i--) {
        getBackupFileName(s_flog.filename, i - 1, src, sizeof(src));
        getBackupFileName(s_flog.filename, i, dst, sizeof(dst));
        if (isFileExist(dst)) {
            if (remove(dst) != 0) {
                fprintf(stderr, "ERROR: logger: Failed to remove file: `%s`\n", dst);
            }
        }
        if (isFileExist(src)) {
            if (rename(src, dst) != 0) {
                fprintf(stderr, "ERROR: logger: Failed to rename file: `%s` -> `%s`\n", src, dst);
            }
        }
    }
    s_flog.output = fopen(s_flog.filename, "a");
    if (s_flog.output == NULL) {
        fprintf(stderr, "ERROR: logger: Failed to open file: `%s`\n", s_flog.filename);
        return 0;
    }
    s_flog.currentFileSize = getFileSize(s_flog.filename);
    return 1;
}


static void getBackupFileName(const char* basename, unsigned char index,
        char* backupname, size_t size)
{
    char indexname[5];

    //assert(size >= strlen(basename) + sizeof(indexname));

    strncpy(backupname, basename, size);
    if (index > 0) {
        sprintf(indexname, ".%d", index);
        strncat(backupname, indexname, strlen(indexname));
    }
}

static int isFileExist(const char* filename)
{
    FILE* fp;

    if ((fp = fopen(filename, "r")) == NULL) {
        return 0;
    } else {
        fclose(fp);
        return 1;
    }
}

static void getTimestamp(const struct timeval* time, char* timestamp, size_t size)
{
    time_t sec = time->tv_sec;
    struct tm calendar;

    //assert(size >= 25);

    localtime_r(&sec, &calendar);
    strftime(timestamp, size, "%Y-%m-%d %H:%M:%S", &calendar);
    sprintf(&timestamp[19], ".%03ld", (long) time->tv_usec);
}

