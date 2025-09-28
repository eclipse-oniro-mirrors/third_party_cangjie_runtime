/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * This source file is part of the Cangjie project, licensed under Apache-2.0
 * with Runtime Library Exception.
 *
 * See https://cangjie-lang.cn/pages/LICENSE for license information.
 */

#ifndef _MSC_BUILD
#include "securec.h"
#else
#ifndef EOK
#define EOK 0
#endif // EOK
#endif // _MSC_BUILD

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_PATH_LEN 4096

#ifdef __MINGW64__
#include <windows.h>
#include <fileapi.h>
#include <tchar.h>

static wchar_t* GetWPath(const char* cstr, int64_t pathLen)
{
    if (cstr == NULL) {
        return NULL;
    }
    int length = MultiByteToWideChar(CP_UTF8, 0, cstr, pathLen, NULL, 0);
    if (length == 0) {
        return NULL;
    }

    length += 1;
    wchar_t* wstr = (wchar_t*)malloc(sizeof(wchar_t) * length);
    if (NULL == wstr) {
        return NULL;
    }
    (void)memset_s(wstr, length * sizeof(wchar_t), 0, length * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, cstr, pathLen, wstr, length);
    return wstr;
}

extern int64_t CJ_TIME_GetFileSize(const char* path, int64_t pathLen)
{
    wchar_t* wPath = GetWPath(path, pathLen);
    if (wPath == NULL) {
        return -1;
    }
    WIN32_FILE_ATTRIBUTE_DATA wfad;
    if (GetFileAttributesExW(wPath, GetFileExInfoStandard, &wfad)) {
        int64_t size = ((int64_t)wfad.nFileSizeHigh << 32) | wfad.nFileSizeLow;
        free(wPath);
        return size;
    }
    free(wPath);
    return -1;
}

static HANDLE GetFileHandle(const char* path, int64_t pathLen)
{
    wchar_t* conv = (wchar_t*)GetWPath(path, pathLen);
    if (conv == NULL) {
        return NULL;
    }
    HANDLE hFile = CreateFileW(conv, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free((void*)conv);
    return hFile;
}

extern int64_t CJ_TIME_ReadAllBytesFromFile(const char* path, int64_t pathLen, char* buffer, int64_t maxLen)
{
    HANDLE fd = GetFileHandle(path, pathLen);
    if (fd == INVALID_HANDLE_VALUE) {
        return -1;
    }
    DWORD numOfBytesToRead = (DWORD)maxLen;
    DWORD numOfBytesRead = 0;
    BOOL success = ReadFile(fd, buffer, numOfBytesToRead, &numOfBytesRead, NULL);
    CloseHandle(fd);
    if (!success) {
        return -1;
    }
    return (int64_t)numOfBytesRead;
}

extern const int64_t CJ_TIME_GetLocalTimeOffset()
{
    DYNAMIC_TIME_ZONE_INFORMATION tzInfo;

    DWORD dwRetVal = GetTimeZoneInformation(&tzInfo);

    int ret = 0;
    if (dwRetVal == TIME_ZONE_ID_DAYLIGHT) {
        ret = 0 - tzInfo.DaylightBias;
    } else if (dwRetVal == TIME_ZONE_ID_STANDARD) {
        ret = 0 - tzInfo.StandardBias;
    } else if (dwRetVal == TIME_ZONE_ID_UNKNOWN) {
        ret = 0;
    } else {
        return 0;
    }
    return ret - tzInfo.Bias;
}

#else // __MINGW64__

extern const char* CJ_TIME_GetRealPath(const char* path)
{
    return realpath(path, NULL);
}

static const char* CJ_TIME_GetCPath(const char* path, int64_t pathLen)
{
    if (pathLen <= 0 || pathLen > MAX_PATH_LEN || path == NULL) {
        return NULL;
    }
    size_t pathSize = (size_t)pathLen;
    char* cPath = (char*)malloc(pathSize + 1);
    if (cPath == NULL) {
        return NULL;
    }
    errno_t ret = memcpy_s(cPath, pathSize + 1, path, pathSize);
    if (ret != EOK) {
        free(cPath);
        return NULL;
    }
    cPath[pathLen] = '\0';
    return cPath;
}

static int64_t CJ_TIME_Stat(const char* path, int64_t pathLen, struct stat* buf)
{
    const char* cPath = CJ_TIME_GetCPath(path, pathLen);
    if (cPath == NULL) {
        return -1;
    }
    int64_t ret = stat(cPath, buf);
    free((void*)cPath);
    return ret;
}

extern int64_t CJ_TIME_GetFileSize(const char* path, int64_t pathLen)
{
    struct stat buf;
    if (memset_s(&buf, sizeof(buf), 0, sizeof(buf)) != EOK) {
        return -1;
    }
    int64_t ret = CJ_TIME_Stat(path, pathLen, &buf);
    if (ret < 0) {
        return -1;
    }
    // Check whether `path` refers to a readable file
    // To utilize "short-circuit evaluation", we list cases heuristically with their usage frequency
    bool readable = S_ISREG(buf.st_mode) // Regular file
        || S_ISLNK(buf.st_mode)          // Symlink
        || S_ISBLK(buf.st_mode)          // Block device
        || S_ISFIFO(buf.st_mode)         // FIFO (named pipe)
        || S_ISCHR(buf.st_mode);         // Character device
    if (!readable) {
        return -1;
    }
    return buf.st_size;
}

extern int64_t CJ_TIME_ReadAllBytesFromFile(const char* path, int64_t pathLen, char* buf, int64_t bufLen)
{
    if (buf == NULL || bufLen <= 0) {
        return -1;
    }
    const char* cPath = CJ_TIME_GetCPath(path, pathLen);
    if (cPath == NULL) {
        return -1;
    }

    FILE* fp = fopen(cPath, "rb");
    free((void*)cPath);
    if (fp == NULL) {
        return -1;
    }
    int64_t size = (int64_t)fread(buf, 1, (size_t)bufLen, fp);
    (void)fclose(fp);
    return size;
}

#include <time.h>
extern const int64_t CJ_TIME_GetLocalTimeOffset()
{
    time_t now;
    (void)time(&now);
    struct tm* tmInfo = localtime(&now);
    if (tmInfo == NULL) {
        return 0; // not reachable
    }
    return tmInfo->tm_gmtoff / 60; // one minute contains 60 seconds
}
#endif