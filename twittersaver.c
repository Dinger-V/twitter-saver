#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#define BUFSIZE 64
#include <sys/stat.h>
#include <errno.h>
#include "platform.h"

#define CONFIG_FILE "dirpaths.txt"

struct memory_buffer {
    char *data;
    size_t size;
};

size_t write_cb(void *data, size_t size, size_t nmemb, void *userp) {
    FILE *fp = (FILE *)userp;
    return fwrite(data, size, nmemb, fp);
}

size_t write_mem_cb(void *data, size_t size, size_t nmemb, void *userp) {
    struct memory_buffer *chunk = (struct memory_buffer *)userp;
    size_t new_bytes = size * nmemb;
    char *alloc = realloc(chunk->data, chunk->size + new_bytes);
    if (!alloc) {
        perror("Allocation failed");
        return 0;
    }
    chunk->data = alloc;
    char *dest = chunk->data + chunk->size;
    memcpy(dest, data, new_bytes);
    chunk->size = chunk->size + new_bytes;
    return size * nmemb;
}

void expand_home_path(char *path, size_t max_len) {
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\\' || path[1] == '\0')) {
        const char *home = get_home();
        if (home != NULL) {
            char temp[512];
            snprintf(temp, sizeof(temp), "%s%s", home, path + 1);
            snprintf(path, max_len, "%s", temp);
        }
    }
}

void strip_newline(char *s) {
    s[strcspn(s, "\r\n")] = '\0';
}

void save_dir(const char *dir) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (fp != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            strip_newline(line);
            if (strcmp(line, dir) == 0) {
                fclose(fp);
                return;
            }
        }
        fclose(fp);
    }

    fp = fopen(CONFIG_FILE, "a");
    if (fp == NULL) {
        perror("Could not open config file");
        return;
    }
    fprintf(fp, "%s\n", dir);
    fclose(fp);
}

void load_dir(char dirs[][256], int *count) {
    char line[256];
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (fp == NULL) {
        printf("file doesn't exist\n");
        return;
    }
    int dir_count = 0;
    while (dir_count < 20 && fgets(line, sizeof(line), fp)) {
        strip_newline(line);
        strcpy(dirs[dir_count], line);
        dir_count++;
    }
    *count = dir_count;
    fclose(fp);
}

// extract url from json, no https bullshit
int extract_media_urls(const char *json, char urls[][512], int max_urls) {
    const char *media_pos = strstr(json, "\"mediaURLs\":[");
    if (media_pos == NULL) {
        return 0;
    }
    const char *pos = media_pos + 13;
    int count = 0;

    while (count < max_urls) {
        if (*pos == ']') {
            break;
        }
        const char *start_quote = strchr(pos, '"');
        if (!start_quote) break;
        const char *end_quote = strchr(start_quote + 1, '"');
        if (!end_quote) break;

        char url[512];
        int i = 0;
        const char *src = start_quote + 1;
        while (src + i != end_quote && i < (int)sizeof(url) - 1) {
            url[i] = src[i];
            i++;
        }
        url[i] = '\0';

        strcpy(urls[count], url);
        count++;
        pos = end_quote + 1;
    }
    return count;
}

void pick_directory(char *selected_dir, size_t max_len) {
    const char *home = get_home();
    if (home == NULL) {
        home = ".";
    }

    struct stat stats;
    char saved_dirs[20][256];
    int dir_count = 0;

    load_dir(saved_dirs, &dir_count);

    if (dir_count > 0) {
        printf("existing saved directory\n");
        for (int i = 0; i < dir_count; i++) {
            printf("%d) %s\n", i + 1, saved_dirs[i]);
        }
        printf("%d) enter a new custom directory\n", dir_count + 1);

        while (1) {
            int choice;
            printf("Choose an option (1-%d): ", dir_count + 1);
            int z = scanf("%d", &choice);
            if (z != 1) {
                while (getchar() != '\n');
                printf("Invalid input, try again\n");
                continue;
            }
            while (getchar() != '\n');

            if (choice < 1 || choice > dir_count + 1) {
                printf("invalid choice\n");
                continue;
            }
            if (choice <= dir_count) {
                snprintf(selected_dir, max_len, "%s", saved_dirs[choice - 1]);
                expand_home_path(selected_dir, max_len);
                printf("The directory selected is: %s\n", selected_dir);
                break;
            } else {
                printf("Enter custom directory: ");
                fgets(selected_dir, max_len, stdin);
                strip_newline(selected_dir);
                expand_home_path(selected_dir, max_len);
                save_dir(selected_dir);
                break;
            }
        }
    } else {
        printf("Enter directory (or press enter for default ~/Pictures): ");
        fgets(selected_dir, max_len, stdin);
        strip_newline(selected_dir);

        if (selected_dir[0] == '\0') {
            snprintf(selected_dir, max_len, "%s/Pictures", home);
        } else {
            expand_home_path(selected_dir, max_len);
            save_dir(selected_dir);
        }

        if (stat(selected_dir, &stats) == -1) {
            perror("Directory doesn't exist");
            snprintf(selected_dir, max_len, "%s/Pictures", home);
        } else if (!S_ISDIR(stats.st_mode)) {
            perror("This is not a directory");
            snprintf(selected_dir, max_len, "%s/Pictures", home);
        }
    }
}

int main(void) {

    char selected_dir[256];
    pick_directory(selected_dir, sizeof(selected_dir));
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init failed\n");
        return 1;
    }

    while (1) {
        char url[512];
        char category[256];

        printf("Enter tweet URL (or 'quit' to exit): ");
        if (!fgets(url, sizeof(url), stdin)) {
            break;
        }
        strip_newline(url);
        if (strcmp(url, "quit") == 0) {
            break;
        }
        if (strcmp(url, "dir") == 0) {
            pick_directory(selected_dir, sizeof(selected_dir));
            continue;
        }
        if (url[0] == '\0') {
            continue;
        }

        printf("Enter category: ");
        if (!fgets(category, sizeof(category), stdin)) {
            break;
        }
        strip_newline(category);

        char dirpath[512];
        if (category[0] != '\0') {
            snprintf(dirpath, sizeof(dirpath), "%s/%s", selected_dir, category);
        } else {
            snprintf(dirpath, sizeof(dirpath), "%s", selected_dir);
        }

       
        // now it fetches api.vxtwitter, essentially handling the bs created
        char api_url[600];
        snprintf(api_url, sizeof(api_url), "https://api.vxtwitter%s", url + 9);

        struct memory_buffer chunk;
        chunk.data = malloc(1);
        chunk.size = 0;

        curl_easy_setopt(curl, CURLOPT_URL, api_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "download failed: %s\n", curl_easy_strerror(res));
            free(chunk.data);
            continue;
        }

        char *tmp = realloc(chunk.data, chunk.size + 1);
        if (!tmp) {
            perror("allocation error");
            free(chunk.data);
            continue;
        }
        chunk.data = tmp;
        chunk.data[chunk.size] = '\0';
        

        // extract all media url
        char media_urls[4][512];
        int media_count = extract_media_urls(chunk.data, media_urls, 4);
        free(chunk.data);

        if (media_count == 0) {
            fprintf(stderr, "Could not find any media URLs\n");
            continue;
        }

        if (make_dir(dirpath, 0755) != 0) {
            if (errno != EEXIST) {
                perror("mkdir failed");
                continue;
            }
        }

        // file name extraction is simplified by ALOT because we got the evil raw image
        for (int m = 0; m < media_count; m++) {
            char *media_url = media_urls[m];

            char *slash_pos = strrchr(media_url, '/');
            char *dot_pos = strrchr(media_url, '.');

            char id[128];
            int i = 0;
            char *src = slash_pos + 1;
            while (src + i != dot_pos && i < (int)sizeof(id) - 1) {
                id[i] = src[i];
                i++;
            }
            id[i] = '\0';

            char ext[16];
            char *src2 = dot_pos + 1;
            int z = 0;
            while (src2[z] != '\0' && z < (int)sizeof(ext) - 1) {
                ext[z] = src2[z];
                z++;
            }
            ext[z] = '\0';

            char filename[160];
            snprintf(filename, sizeof(filename), "%s.%s", id, ext);
            char filepath[600];
            snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, filename);

            FILE *fp = fopen(filepath, "wb");
            if (fp == NULL) {
                perror("Failed to open file for writing");
                continue;
            }

            curl_easy_setopt(curl, CURLOPT_URL, media_url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
            res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                fprintf(stderr, "download failed: %s\n", curl_easy_strerror(res));
            } else {
                printf("Saved: %s\n", filepath);
            }

            fclose(fp);
        }
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
