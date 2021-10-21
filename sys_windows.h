#ifndef __SYS_WINDOWS_H__
#define __SYS_WINDOWS_H__

#include <Windows.h>
#include <Shlwapi.h>
#include <ShlObj.h>

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

static const char *get_bin_path() {
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