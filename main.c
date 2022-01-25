#include <stdio.h>

#include <cjson/cJSON.h>
#include <zip.h>

#include "sys_windows.h"
#include <WinInet.h>

#define BUFFER_SIZE 1024
#define STRING_SIZE 256

#define error_ok                0
#define error_cant_init_inet    1
#define error_cant_access_site  2
#define error_cant_parse_json   3
#define error_no_release_found  4

#define download_query_size ((size_t) -1)

typedef struct _bang_zip_information {
    char version[STRING_SIZE];
    char zip_url[STRING_SIZE];
    size_t zip_size;
} bang_zip_information;

typedef struct _memory {
    char *data;
    size_t size;
} memory;

int download_file(memory *mem, const char *url, size_t download_size) {
    int errcode = error_ok;

    HINTERNET hInternet = NULL;
    HINTERNET hConnect = NULL;

    char buffer[BUFFER_SIZE];

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

    while (1) {
        DWORD bytes_to_read = 0, bytes_read = 0;

        if (download_size == download_query_size) {
            if (!InternetQueryDataAvailable(hConnect, &bytes_to_read, 0, 0)) {
                errcode = error_cant_access_site;
                goto finish;
            }
        } else {
            bytes_to_read = download_size;
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

        mem->data = realloc(mem->data, mem->size + bytes_read);
        memcpy(mem->data + mem->size, buffer, bytes_read);
        mem->size += bytes_read;

        if (download_size != download_query_size) {
            download_size -= bytes_read;
        }
    }

finish:
    if (errcode && mem->data) free(mem->data);
    if (hConnect) InternetCloseHandle(hConnect);
    if (hInternet) InternetCloseHandle(hInternet);
    return errcode;
}

int get_bang_version(cJSON *latest, bang_zip_information *out) {
    if (!cJSON_IsObject(latest)) return 1;

    cJSON *assets = cJSON_GetObjectItemCaseSensitive(latest, "assets");
    if (!assets || !cJSON_IsArray(assets) || cJSON_GetArraySize(assets) == 0) return 1;

    cJSON *json_version = cJSON_GetObjectItemCaseSensitive(latest, "name");
    if (!json_version || !cJSON_IsString(json_version)) return 1;

    cJSON *asset = cJSON_GetArrayItem(assets, 0);
    if (!asset || !cJSON_IsObject(asset)) return 1;

    cJSON *json_zip_url = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
    if (!json_zip_url || !cJSON_IsString(json_zip_url)) return 1;

    strncpy(out->version, cJSON_GetStringValue(json_version), STRING_SIZE);
    strncpy(out->zip_url, cJSON_GetStringValue(json_zip_url), STRING_SIZE);

    cJSON *json_zip_size = cJSON_GetObjectItemCaseSensitive(asset, "size");
    if (!json_zip_url || !cJSON_IsNumber(json_zip_size)) return 1;

    out->zip_size = (int) cJSON_GetNumberValue(json_zip_size);
    
    return 0;
}

int get_bang_latest_version(bang_zip_information *out) {
    memory mem;
    memset(&mem, 0, sizeof(mem));
    
    int errcode = download_file(&mem, "https://api.github.com/repos/salvoilmiosi/bang-sdl/releases/latest", download_query_size);
    if (errcode == error_ok) {
        cJSON *json = cJSON_ParseWithLength(mem.data, mem.size);
        if (json) {
            if (get_bang_version(json, out) == 0) {
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

int download_bang_last_version(const char *base_dir) {
    bang_zip_information info;
    memset(&info, 0, sizeof(info));

    int errcode = get_bang_latest_version(&info);
    if (errcode == error_ok) {
        printf("Latest version: %s\n", info.version);

        if (file_exists(base_dir)) {
            FILE *file = fopen(concat_path(base_dir, "version.txt"), "r");
            if (file) {
                char version[STRING_SIZE];
                if (fgets(version, STRING_SIZE, file)) {
                    version[strlen(version)-1] = '\0';
                    fclose(file);
                    if (strcmp(info.version, version) == 0) {
                        printf("You have the latest version\n");
                        return error_ok;
                    }
                }
            }
        } else {
            make_dir(base_dir);
        }

        printf("Download: %s (%d bytes)\n", info.zip_url, info.zip_size);

        memory mem;
        memset(&mem, 0, sizeof(mem));

        download_file(&mem, info.zip_url, info.zip_size);
        zip_error_t error;
        zip_source_t *source = zip_source_buffer_create(mem.data, mem.size, 1, &error);
        zip_t *archive = zip_open_from_source(source, 0, &error);

        zip_int64_t num_entries = zip_get_num_entries(archive, 0);
        for (zip_int64_t i=0; i<num_entries; ++i) {
            const char *path_basename = strchr(zip_get_name(archive, i, 0), '/') + 1;
            if (path_basename) {
                const char *path = concat_path(base_dir, path_basename);
                size_t file_size = get_file_size(path);

                zip_stat_t stat;
                if (zip_stat_index(archive, i, ZIP_STAT_SIZE, &stat) != 0) continue;

                zip_int64_t zip_file_size = stat.size;

                if (file_size == zip_file_size) continue;

                FILE *file_out = fopen(path, "wb");
                if (!file_out) continue;

                zip_file_t *file_in = zip_fopen_index(archive, i, 0);
                if (file_in) {
                    printf("Install: %s\n", path);
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

        FILE *version_file = fopen(concat_path(base_dir, "version.txt"), "w");
        fprintf(version_file, "%s\n", info.version);
        fclose(version_file);
    }
    return errcode;
}

int main(int argc, char **argv) {
    const char *base_dir = get_bin_path();

    switch (download_bang_last_version(base_dir)) {
    case error_ok:
        if (argc > 1 && strcmp(argv[1], "server") == 0) {
            launch_process(concat_path(base_dir, "bangserver.exe"));
        } else {
            launch_process(concat_path(base_dir, "bangclient.exe"));
        }
        break;
    case error_cant_init_inet:
        fprintf(stderr, "Could not init WinInet\n");
        break;
    case error_cant_access_site:
        fprintf(stderr, "Could not access site\n");
        break;
    case error_cant_parse_json:
        fprintf(stderr, "Could not parse json output\n");
        break;
    case error_no_release_found:
        fprintf(stderr, "No release found\n");
        break;
    }

    return 0;
}