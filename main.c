#include <stdio.h>

#include <cjson/cJSON.h>
#include <zip.h>

#include "sys_windows.h"
#include "resources.h"

#define WM_INSTALL_FINISHED WM_USER + 1

const char ClassName[] = "MainWindowClass";

HWND hWndMain;
HWND hWndProgressBar;
HWND hWndStatus;

HANDLE hDownload;

const char *bang_base_dir;

struct {
    char version[STRING_SIZE];
    char commit[STRING_SIZE];
    char zip_url[STRING_SIZE];
    size_t zip_size;
} bang_zip_information;

typedef long (__stdcall *entrypoint_fun_t)(const char*);

void launch_client() {
    HINSTANCE hinstLib;
    entrypoint_fun_t entrypoint;

    SetDllDirectory(bang_base_dir);
    hinstLib = LoadLibrary("libbangclient.dll");

    if (hinstLib != NULL) {
        entrypoint = (entrypoint_fun_t) GetProcAddress(hinstLib, "entrypoint");

        if (entrypoint != NULL) {
            (entrypoint) (bang_base_dir);
        }

        FreeLibrary(hinstLib);
    } else {
        launch_process(concat_path(bang_base_dir, "bangclient.exe"));
    }
}

int get_bang_version(cJSON *latest) {
    if (!cJSON_IsObject(latest)) return 1;

    cJSON *assets = cJSON_GetObjectItemCaseSensitive(latest, "assets");
    if (!assets || !cJSON_IsArray(assets) || cJSON_GetArraySize(assets) == 0) return 1;

    cJSON *json_version = cJSON_GetObjectItemCaseSensitive(latest, "name");
    if (!json_version || !cJSON_IsString(json_version)) return 1;

    cJSON *json_commit = cJSON_GetObjectItemCaseSensitive(latest, "target_commitish");
    if (!json_commit || !cJSON_IsString(json_version)) return 1;

    cJSON *asset = cJSON_GetArrayItem(assets, 0);
    if (!asset || !cJSON_IsObject(asset)) return 1;

    cJSON *json_zip_url = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
    if (!json_zip_url || !cJSON_IsString(json_zip_url)) return 1;

    strncpy(bang_zip_information.version, cJSON_GetStringValue(json_version), STRING_SIZE);
    strncpy(bang_zip_information.zip_url, cJSON_GetStringValue(json_zip_url), STRING_SIZE);
    strncpy(bang_zip_information.commit, cJSON_GetStringValue(json_commit), STRING_SIZE);

    cJSON *json_zip_size = cJSON_GetObjectItemCaseSensitive(asset, "size");
    if (!json_zip_url || !cJSON_IsNumber(json_zip_size)) return 1;

    bang_zip_information.zip_size = (int) cJSON_GetNumberValue(json_zip_size);
    
    return 0;
}

int get_bang_latest_version() {
    memory mem;
    memset(&mem, 0, sizeof(mem));
    
    int errcode = download_file(&mem, "https://api.github.com/repos/salvoilmiosi/bang-sdl/releases/latest", download_query_size, NULL);
    if (errcode == error_ok) {
        cJSON *json = cJSON_ParseWithLength(mem.data, mem.size);
        if (json) {
            if (get_bang_version(json) == 0) {
                errcode = error_ok;
            } else {
                errcode = error_no_release_found;
            }

            cJSON_Delete(json);
        } else {
            errcode = error_cant_parse_json;
        }
    }

    if (mem.data) free(mem.data);
    return errcode;
}

void set_status(const char *format, ...) {
    static char last_buffer[256] = {0};

    va_list arg;
    char buffer[256];

    va_start(arg, format);
    vsnprintf(buffer, 256, format, arg);
    va_end(arg);

    if (strcmp(last_buffer, buffer)) {
        SendMessage(hWndStatus, SB_SETTEXT, MAKEWPARAM(0, 0), buffer);
        strncpy(last_buffer, buffer, 256);
    }
}

void print_download_status(int bytes_read, int bytes_total) {
    SendMessage(hWndProgressBar, PBM_SETPOS, (float) bytes_read / bytes_total * 0xffff, 0);

    int percent = ((float) bytes_read / bytes_total * 100);
    set_status("Download: %s ... %d %%", bang_zip_information.version, percent);
}

DWORD download_bang_latest_version(void *param) {
    set_status("Download: %s...", bang_zip_information.version);

    memory mem;
    memset(&mem, 0, sizeof(mem));

    download_file(&mem, bang_zip_information.zip_url, bang_zip_information.zip_size, print_download_status);
    zip_error_t error;
    zip_source_t *source = zip_source_buffer_create(mem.data, mem.size, 1, &error);
    zip_t *archive = zip_open_from_source(source, 0, &error);

    zip_int64_t num_entries = zip_get_num_entries(archive, 0);
    for (zip_int64_t i=0; i<num_entries; ++i) {
        const char *path_basename = strchr(zip_get_name(archive, i, 0), '/') + 1;
        if (path_basename) {
            const char *path = concat_path(bang_base_dir, path_basename);

            zip_stat_t stat;
            if (zip_stat_index(archive, i, ZIP_STAT_SIZE, &stat) != 0) continue;

            zip_int64_t zip_file_size = stat.size;

            FILE *file_out = fopen(path, "wb");
            if (!file_out) continue;

            zip_file_t *file_in = zip_fopen_index(archive, i, 0);
            if (file_in) {
                set_status("Install: %s", path);
                char buffer[BUFFER_SIZE];
                
                while (zip_file_size != 0) {
                    zip_int64_t nbytes = zip_fread(file_in, buffer, BUFFER_SIZE);
                    fwrite(buffer, nbytes, 1, file_out);
                    zip_file_size -= nbytes;
                }

                zip_fclose(file_in);
            }
            fclose(file_out);
        }
    }

    zip_close(archive);

    FILE *version_file = fopen(concat_path(bang_base_dir, "version.txt"), "w");
    fprintf(version_file, "%s\n", bang_zip_information.commit);
    fclose(version_file);

    SendMessage(hWndMain, WM_INSTALL_FINISHED, 0, 0);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    switch (Msg) {
    case WM_CREATE: {
        hWndProgressBar = CreateWindowEx(
            0,
            PROGRESS_CLASS,
            (LPSTR)NULL,
            WS_VISIBLE | WS_CHILD,
            10,
            10,
            360,
            20,
            hWnd,
            (HMENU)IDPB_PROGRESS_BAR,
            (HINSTANCE)GetWindowLong(hWnd, GWLP_HINSTANCE),
            NULL);

        hWndStatus = CreateWindowEx(
            0,
            STATUSCLASSNAME,
            (LPSTR)NULL,
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            hWnd,
            (HMENU)IDPB_STATUS_BAR,
            (HINSTANCE)GetWindowLong(hWnd, GWLP_HINSTANCE),
            NULL);

        hDownload = CreateThread(NULL, 0, download_bang_latest_version, NULL, 0, NULL);
            
        SendMessage(hWndProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 0xffff));
        break;
    }
    case WM_INSTALL_FINISHED:
        DestroyWindow(hWndMain);
        launch_client();
        // fall through
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_CLOSE:
        TerminateThread(hDownload, 0);
        // fall through
    default:
        return (DefWindowProc(hWnd, Msg, wParam, lParam));
    }
    return 0;
}

void show_error_message(int errcode) {
    switch (errcode) {
    case error_cant_init_inet:
        message_box("Could not init WinInet", MB_ICONERROR);
        break;
    case error_cant_access_site:
        message_box("Could not access site", MB_ICONERROR);
        break;
    case error_cant_parse_json:
        message_box("Could not parse json output", MB_ICONERROR);
        break;
    case error_no_release_found:
        message_box("No release found", MB_ICONERROR);
        break;
    }
}

BOOL must_download_latest_version() {
    if (file_exists(bang_base_dir)) {
        FILE *file = fopen(concat_path(bang_base_dir, "version.txt"), "r");
        if (file) {
            char version[STRING_SIZE];
            if (fgets(version, STRING_SIZE, file)) {
                version[strlen(version)-1] = '\0';
                fclose(file);
                if (strcmp(bang_zip_information.commit, version) == 0) {
                    return FALSE;
                }
            }
        }
    } else {
        make_dir(bang_base_dir);
    }
    return TRUE;
}

INT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nCmdShow) {
    InitCommonControls();
    WNDCLASSEX wc;
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = ClassName;

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Failed To Register The Window Class.", "Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    
    memset(&bang_zip_information, 0, sizeof(bang_zip_information));

    bang_base_dir = get_bang_bin_path();
    int errcode = get_bang_latest_version();
    if (errcode != error_ok) {
        show_error_message(errcode);
        return 0;
    }

    if (must_download_latest_version()) {
        RECT desktop_rect;
        GetClientRect(GetDesktopWindow(), &desktop_rect);

        const int window_width = 400;
        const int window_height = 100;
        const int window_left = desktop_rect.left + (desktop_rect.right - desktop_rect.left - window_width) / 2;
        const int window_top = desktop_rect.top + (desktop_rect.bottom - desktop_rect.top - window_height) / 2;

        hWndMain = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            ClassName,
            "Bang! Launcher",
            WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            window_left,
            window_top,
            window_width,
            window_height,
            NULL,
            NULL,
            hInstance,
            NULL);

        if (!hWndMain) {
            MessageBox(NULL, "Window Creation Failed.", "Error", MB_OK | MB_ICONERROR);
            return 0;
        }

        ShowWindow(hWndMain, SW_SHOW);
        UpdateWindow(hWndMain);

        MSG Msg;
        while (GetMessage(&Msg, NULL, 0, 0)) {
            TranslateMessage(&Msg);
            DispatchMessage(&Msg);
        }

        return Msg.wParam;
    } else {
        launch_client();
        return 0;
    }
}