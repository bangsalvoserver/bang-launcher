#ifndef __SYS_WINDOWS_H__
#define __SYS_WINDOWS_H__

#include <Windows.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <WinInet.h>

typedef struct _memory {
    char *data;
    size_t size;
    size_t capacity;
} memory;

#define BUFFER_SIZE 1024
#define STRING_SIZE 256

#define error_ok                0
#define error_cant_init_inet    1
#define error_cant_access_site  2
#define error_cant_parse_json   3
#define error_no_release_found  4

#define download_query_size ((size_t) -1)

typedef void (*downloading_callback) (int bytes_read, int bytes_total, void *params);

static int download_file(memory *mem, const char *url, size_t download_size, downloading_callback callback, void *params) {
    memset(mem, 0, sizeof(memory));

    int errcode = error_ok;

    HINTERNET hInternet = NULL;
    HINTERNET hConnect = NULL;

    char buffer[BUFFER_SIZE];
    DWORD bytes_to_read = BUFFER_SIZE;
    DWORD bytes_read = 0;

    if (!(hInternet = InternetOpen(
        "Mozilla/5.0",
        INTERNET_OPEN_TYPE_DIRECT,
        NULL,
        NULL,
        0
    ))) {
        errcode = error_cant_init_inet;
        goto finish;
    }

    if (!(hConnect = InternetOpenUrlA(hInternet, url, NULL, 0, 0, (DWORD_PTR) NULL))) {
        errcode = error_cant_access_site;
        goto finish;
    }

    if (!HttpQueryInfo(hConnect, HTTP_QUERY_STATUS_CODE, &buffer, &bytes_to_read, 0)) {
        errcode = error_cant_access_site;
        goto finish;
    }
    if (strcmp(buffer, "200") != 0) {
        errcode = error_cant_access_site;
        goto finish;
    }

    if (download_size != download_query_size) {
        mem->capacity = download_size;
        mem->data = malloc(download_size);
    }

    size_t remaining_bytes = download_size;
    while (1) {
        bytes_to_read = 0;
        bytes_read = 0;

        if (download_size == download_query_size) {
            if (!InternetQueryDataAvailable(hConnect, &bytes_to_read, 0, 0)) {
                errcode = error_cant_access_site;
                goto finish;
            }
        } else {
            bytes_to_read = remaining_bytes;
        }

        if (bytes_to_read > BUFFER_SIZE) {
            bytes_to_read = BUFFER_SIZE;
        } else if (bytes_to_read == 0) {
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);
        if (!InternetReadFile(hConnect, buffer, bytes_to_read, &bytes_read)) {
            errcode = error_cant_access_site;
            goto finish;
        }

        if (mem->size + bytes_read > mem->capacity) {
            mem->data = realloc(mem->data, mem->size + bytes_read);
            mem->capacity = mem->size + bytes_read;
        }
        memcpy(mem->data + mem->size, buffer, bytes_read);
        mem->size += bytes_read;

        if (callback) {
            callback(mem->size, download_size, params);
        }

        if (download_size != download_query_size) {
            remaining_bytes -= bytes_read;
        }
    }

finish:
    if (hConnect) InternetCloseHandle(hConnect);
    if (hInternet) InternetCloseHandle(hInternet);
    return errcode;
}

static void message_box(const char *message, int flags) {
    MessageBox(NULL, message, "Bang!", MB_OK | flags);
}

static void launch_process(const char *filename) {
    STARTUPINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cb = sizeof(info);

    PROCESS_INFORMATION processInfo;
    if (CreateProcessA(filename, NULL, NULL, NULL, 0, 0, NULL, NULL, &info, &processInfo)) {
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
    }
}

static const char *get_bang_bin_path() {
    static char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        PathAppendA(path, "bang-sdl\\bin");
        return path;
    }
    return NULL;
}

static const char *concat_path(const char *dir, const char *filename) {
    static char path[MAX_PATH];
    strncpy(path, dir, MAX_PATH);
    PathAppendA(path, filename);
    return path;
}

static void make_dir(const char *filename) {
    DWORD file_attrs = GetFileAttributesA(filename);
    if (file_attrs == INVALID_FILE_ATTRIBUTES) {
        char *last_slash_pos = strrchr(filename, '\\');
        if (last_slash_pos) {
            char sub_path[MAX_PATH];
            strncpy(sub_path, filename, last_slash_pos - filename);
            sub_path[last_slash_pos - filename] = '\0';

            make_dir(sub_path);
        }

        CreateDirectoryA(filename, NULL);
    }
}

static int file_exists(const char *filename) {
    return PathFileExistsA(filename);
}

static BOOL is_directory(const char *filename) {
    DWORD attr = GetFileAttributesA(filename);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

static int get_file_size(const char *filename) {
    if (!file_exists(filename)) return 0;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesEx(filename, GetFileExInfoStandard, &fad))
        return -1;
    
    LARGE_INTEGER size;
    size.HighPart = fad.nFileSizeHigh;
    size.LowPart = fad.nFileSizeLow;
    return size.QuadPart;
}

#endif