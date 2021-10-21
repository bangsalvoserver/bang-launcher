#include <stdio.h>

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <zip.h>

#ifdef _WIN32
#include "sys_windows.h"
#endif

#define BUFFER_SIZE 1024
#define STRING_SIZE 256

#define error_ok                0
#define error_cant_init_curl    1
#define error_cant_access_site  2
#define error_cant_parse_json   3
#define error_no_release_found  4

typedef struct _bang_zip_information {
    char version[STRING_SIZE];
    char zip_url[STRING_SIZE];
} bang_zip_information;

typedef struct _memory {
    char *data;
    size_t size;
} memory;

size_t append_to_memory(char *data, size_t size, size_t nmemb, memory *mem) {
    size_t realsize = size * nmemb;
    mem->data = realloc(mem->data, mem->size + realsize);
    memcpy(mem->data + mem->size, data, realsize);
    mem->size += realsize;
    return realsize;
}

int get_bang_version(cJSON *json, bang_zip_information *out) {
    if (!cJSON_IsArray(json) || cJSON_GetArraySize(json) == 0) return 1;

    cJSON *latest = cJSON_GetArrayItem(json, 0);
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
    
    return 0;
}

int get_bang_latest_version(bang_zip_information *out) {
    int errcode = error_ok;

    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/salvoilmiosi/bang-sdl/releases");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);

        memory mem;
        memset(&mem, 0, sizeof(mem));

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_to_memory);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
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
        } else {
            errcode = error_cant_access_site;
        }

        if (mem.data) {
            free(mem.data);
        }

        curl_easy_cleanup(curl);
    } else {
        errcode = error_cant_init_curl;
    }

    return errcode;
}

void download_file(const char *url, memory *out) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

int download_bang_last_version(const char *base_dir) {
    bang_zip_information info;
    memset(&info, 0, sizeof(info));

    int errcode = get_bang_latest_version(&info);
    if (errcode == error_ok) {
        printf("Ultima versione: %s\n", info.version);

        if (file_exists(base_dir)) {
            FILE *file = fopen(concat_path(base_dir, "version.txt"), "r");
            if (file) {
                char version[STRING_SIZE];
                if (fgets(version, STRING_SIZE, file)) {
                    version[strlen(version)-1] = '\0';
                    fclose(file);
                    if (strcmp(info.version, version) == 0) {
                        printf("Hai l'ultima versione\n");
                        return error_ok;
                    }
                }
            }
        } else {
            make_dir(base_dir);
        }

        printf("Download: %s\n", info.zip_url);

        memory mem;
        memset(&mem, 0, sizeof(mem));

        download_file(info.zip_url, &mem);
        printf("%d byte scaricati\n", mem.size);
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
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const char *base_dir = get_bin_path();

    switch (download_bang_last_version(base_dir)) {
    case error_ok:
        if (argc > 1 && strcmp(argv[1], "server") == 0) {
            launch_process(concat_path(base_dir, "bangserver.exe"));
        } else {
            launch_process(concat_path(base_dir, "bangclient.exe"));
        }
        break;
    case error_cant_init_curl:
        fprintf(stderr, "Impossibile inizializzare curl\n");
        break;
    case error_cant_access_site:
        fprintf(stderr, "Impossibile accedere al sito\n");
        break;
    case error_cant_parse_json:
        fprintf(stderr, "Impossibile leggere l'output json\n");
        break;
    case error_no_release_found:
        fprintf(stderr, "Nessuna release trovata\n");
        break;
    }

    curl_global_cleanup();
    
    return 0;
}