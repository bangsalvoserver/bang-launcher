#include <stdio.h>
#include <assert.h>

#include <cjson/cJSON.h>
#include <zip.h>

#include "sys_windows.h"
#include "resources.h"

#define WM_INSTALL_FINISHED WM_USER + 1
#define WM_INSTALL_FAILED   WM_USER + 2

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

    char cards_commit[STRING_SIZE];
    char cards_pak_url[STRING_SIZE];
    size_t cards_pak_size;

} bang_zip_information;

typedef long (__stdcall *entrypoint_fun_t)(const char*);
typedef const char * (__stdcall *client_version_fun_t)(void);

HINSTANCE load_bangclient_dll() {
    if (!file_exists(bang_base_dir)) {
        return FALSE;
    }
    SetDllDirectory(bang_base_dir);
    return LoadLibrary("libbangclient.dll");
}

BOOL must_download_latest_version() {
    BOOL ret = TRUE;
    HINSTANCE lib = load_bangclient_dll();
    if (lib != NULL) {
        client_version_fun_t fun = (client_version_fun_t) GetProcAddress(lib, "get_client_commit_hash");
        if (fun) {
            const char *version = (*fun)();
            if (strcmp(bang_zip_information.commit, version) == 0) {
                ret = FALSE;
            }
        }
        FreeLibrary(lib);
    }
    return ret;
}

BOOL must_download_cards_pak() {
    BOOL ret = TRUE;
    if (bang_zip_information.cards_pak_size == 0) {
        ret = FALSE;
    } else {
        HINSTANCE lib = load_bangclient_dll();
        if (lib != NULL) {
            client_version_fun_t fun = (client_version_fun_t) GetProcAddress(lib, "get_cards_commit_hash");
            if (fun) {
                const char *version = (*fun)();
                if (strcmp(bang_zip_information.cards_commit, version) == 0) {
                    ret = FALSE;
                }
            }
            FreeLibrary(lib);
        }
    }
    return ret;
}

int launch_client() {
    int ret = 1;
    HINSTANCE lib = load_bangclient_dll();
    if (lib != NULL) {
        entrypoint_fun_t fun = (entrypoint_fun_t) GetProcAddress(lib, "entrypoint");
        if (fun) {
            (*fun)(bang_base_dir);
            ret = 0;
        }
        FreeLibrary(lib);
    }
    return ret;
}

void get_bang_version(cJSON *latest) {
    assert(cJSON_IsObject(latest));

    cJSON *assets = cJSON_GetObjectItemCaseSensitive(latest, "assets");
    assert(assets && cJSON_IsArray(assets) && cJSON_GetArraySize(assets) > 0);

    cJSON *json_version = cJSON_GetObjectItemCaseSensitive(latest, "name");
    assert(json_version && cJSON_IsString(json_version));

    cJSON *json_commit = cJSON_GetObjectItemCaseSensitive(latest, "target_commitish");
    assert(json_commit && cJSON_IsString(json_version));

    cJSON *asset = cJSON_GetArrayItem(assets, 0);
    assert(asset && cJSON_IsObject(asset));

    cJSON *json_zip_url = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
    assert(json_zip_url && cJSON_IsString(json_zip_url));
    
    strncpy(bang_zip_information.version, cJSON_GetStringValue(json_version), STRING_SIZE);
    strncpy(bang_zip_information.zip_url, cJSON_GetStringValue(json_zip_url), STRING_SIZE);
    strncpy(bang_zip_information.commit, cJSON_GetStringValue(json_commit), STRING_SIZE);

    cJSON *json_zip_size = cJSON_GetObjectItemCaseSensitive(asset, "size");
    assert(json_zip_size && cJSON_IsNumber(json_zip_size));

    bang_zip_information.zip_size = (int) cJSON_GetNumberValue(json_zip_size);

    if (cJSON_GetArraySize(assets) > 1) {
        cJSON *cards_asset = cJSON_GetArrayItem(assets, 1);
        assert(cards_asset && cJSON_IsObject(cards_asset));
        
        cJSON *json_cards_pak_url = cJSON_GetObjectItemCaseSensitive(cards_asset, "browser_download_url");
        assert(json_cards_pak_url && cJSON_IsString(json_zip_url));

        strncpy(bang_zip_information.cards_pak_url, cJSON_GetStringValue(json_cards_pak_url), STRING_SIZE);

        cJSON *cards_json_zip_size = cJSON_GetObjectItemCaseSensitive(cards_asset, "size");
        assert(cards_json_zip_size && cJSON_IsNumber(cards_json_zip_size));

        bang_zip_information.cards_pak_size = (int) cJSON_GetNumberValue(cards_json_zip_size);
    }
}

cJSON *find_item_in_tree(cJSON *json, const char *path) {
    assert(cJSON_IsObject(json));

    cJSON *json_tree = cJSON_GetObjectItemCaseSensitive(json, "tree");
    assert(json_tree && cJSON_IsArray(json_tree));

    int json_tree_size = cJSON_GetArraySize(json_tree);
    for (int i=0; i<json_tree_size; ++i) {
        cJSON *json_tree_item = cJSON_GetArrayItem(json_tree, i);
        assert(json_tree_item && cJSON_IsObject(json_tree_item));

        cJSON *json_path = cJSON_GetObjectItemCaseSensitive(json_tree_item, "path");
        assert(json_path && cJSON_IsString(json_path));

        if (strcmp(cJSON_GetStringValue(json_path), path) == 0) {
            return json_tree_item;
        }
    }
    return NULL;
}

int get_cards_latest_version() {
    memory mem;
    int errcode;
    char buffer[STRING_SIZE];

    snprintf(buffer, STRING_SIZE, "https://api.github.com/repos/salvoilmiosi/bang-sdl/git/trees/%s", bang_zip_information.commit);
    errcode = download_file(&mem, buffer, download_query_size, NULL, NULL);
    if (errcode == error_ok) {
        cJSON *json = cJSON_ParseWithLength(mem.data, mem.size);
        free(mem.data);

        if (json) {
            cJSON *json_resources = find_item_in_tree(json, "resources");
            if (json_resources) {
                cJSON *json_url = cJSON_GetObjectItemCaseSensitive(json_resources, "url");
                assert(json_url && cJSON_IsString(json_url));

                strncpy(buffer, cJSON_GetStringValue(json_url), STRING_SIZE);
                cJSON_Delete(json);

                errcode = download_file(&mem, buffer, download_query_size, NULL, NULL);
                if (errcode == error_ok) {
                    json = cJSON_ParseWithLength(mem.data, mem.size);
                    free(mem.data);

                    cJSON *json_cards = find_item_in_tree(json, "cards");
                    if (json_cards) {
                        cJSON *json_cards_sha = cJSON_GetObjectItemCaseSensitive(json_cards, "sha");
                        assert(json_cards_sha && cJSON_IsString(json_cards_sha));

                        strncpy(bang_zip_information.cards_commit, cJSON_GetStringValue(json_cards_sha), STRING_SIZE);
                    } else {
                        errcode = error_no_release_found;
                    }
                    cJSON_Delete(json);
                }
            } else {
                errcode = error_no_release_found;
                cJSON_Delete(json);
            }
        } else {
            errcode = error_cant_parse_json;
        }
    }

    return errcode;
}

int get_bang_latest_version() {
    memory mem;
    
    int errcode = download_file(&mem, "https://api.github.com/repos/salvoilmiosi/bang-sdl/releases/latest", download_query_size, NULL, NULL);
    if (errcode == error_ok) {
        cJSON *json = cJSON_ParseWithLength(mem.data, mem.size);
        free(mem.data);

        if (json) {
            get_bang_version(json);
            cJSON_Delete(json);
        } else {
            errcode = error_cant_parse_json;
        }

        errcode = get_cards_latest_version();
    }

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

void print_download_status(int bytes_read, int bytes_total, void *params) {
    SendMessage(hWndProgressBar, PBM_SETPOS, (float) bytes_read / bytes_total * 0xffff, 0);

    int percent = ((float) bytes_read / bytes_total * 100);
    set_status("Download: %s ... %d %%", params, percent);
}

int unzip_bang_zip(memory *mem) {
    int result = 0;
    zip_error_t error;
    zip_source_t *source = zip_source_buffer_create(mem->data, mem->size, 1, &error);
    zip_t *archive = zip_open_from_source(source, 0, &error);

    if (!file_exists(bang_base_dir)) {
        make_dir(bang_base_dir);
    }

    zip_int64_t num_entries = zip_get_num_entries(archive, 0);
    for (zip_int64_t i=0; i<num_entries; ++i) {
        const char *path_basename = strchr(zip_get_name(archive, i, 0), '/') + 1;
        if (path_basename) {
            const char *path = concat_path(bang_base_dir, path_basename);

            zip_stat_t stat;
            if (zip_stat_index(archive, i, ZIP_STAT_SIZE, &stat) != 0) continue;

            zip_int64_t zip_file_size = stat.size;

            if (is_directory(path)) continue;

            FILE *file_out = fopen(path, "wb");
            if (!file_out) {
                result = 1;
                break;
            }

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
    return result;
}

DWORD download_bang_latest_version(void *param) {
    set_status("Download: %s...", bang_zip_information.version);
    
    int result = WM_INSTALL_FINISHED;

    memory mem;

    const char *cards_pak_path = concat_path(bang_base_dir, "cards.pak");
    if (!file_exists(cards_pak_path) || must_download_cards_pak()) {
        if (!file_exists(bang_base_dir)) {
            make_dir(bang_base_dir);
        }
        
        download_file(&mem, bang_zip_information.cards_pak_url, bang_zip_information.cards_pak_size, print_download_status, "cards.pak");

        FILE *file_out = fopen(cards_pak_path, "wb");
        if (!file_out) {
            result = WM_INSTALL_FAILED;
        } else {
            set_status("Install: cards.pak");

            char *read_pos = mem.data;
            int remaining_bytes = mem.size;
            while (remaining_bytes != 0) {
                int nbytes = min(BUFFER_SIZE, remaining_bytes);
                fwrite(read_pos, nbytes, 1, file_out);

                read_pos += nbytes;
                remaining_bytes -= nbytes;
            }
            fclose(file_out);
        }
        free(mem.data);
    }

    if (result == WM_INSTALL_FINISHED) {
        download_file(&mem, bang_zip_information.zip_url, bang_zip_information.zip_size, print_download_status, bang_zip_information.version);
        if (unzip_bang_zip(&mem) != 0) {
            result = WM_INSTALL_FAILED;
        }
    }

    SendMessage(hWndMain, result, 0, 0);
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
    case WM_INSTALL_FAILED:
        message_box("Installation failed!", MB_ICONERROR);
        DestroyWindow(hWndMain);
        PostQuitMessage(0);
        break;
    case WM_INSTALL_FINISHED:
        DestroyWindow(hWndMain);
        launch_client();
        PostQuitMessage(0);
        break;
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