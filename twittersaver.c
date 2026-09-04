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

// mem callback to handle the curl bullshit because libcurl is ASS
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
// expand character so it accepts ~/directory
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
// strip newline by fgets
void strip_newline(char *s) {
    s[strcspn(s, "\r\n")] = '\0';
}

void save_dir(const char *dir) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (fp != NULL) {
        char line[256];
        while(fgets(line, sizeof(line), fp)) {
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

// image extractor here since we parse the raw twitter page (html part)
int og_imageExtractor(const char *html, char *outurl, size_t maxLen) {
    const char *ogtag = strstr(html, "property=\"og:image\"");
    if (!ogtag) {
        ogtag = strstr(html, "name=\"og:image\"");
    }
    if (!ogtag) {
        return 0;
    }
    const char *content_str = strstr(ogtag, "content=\"");
    if (!content_str) {
        return 0;
    }
    content_str += 9;
    size_t i = 0;
    while (content_str[i] != '"' && content_str[i] != '\0' && i < maxLen - 1) {
        outurl[i] = content_str[i];
        i++;
    }
    outurl[i] = '\0';
    return 1;
}

int main(void) {
    const char *home = get_home();
    if (home == NULL) {
        home = ".";
    }

    struct stat stats;
    char saved_dirs[20][256];
    int dir_count = 0;
    char selected_dir[256];

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
                snprintf(selected_dir, sizeof(selected_dir), "%s", saved_dirs[choice - 1]);
                expand_home_path(selected_dir, sizeof(selected_dir));
                printf("The directory selected is: %s\n", selected_dir);
                break;
            } else {
                printf("Enter custom directory: ");
                fgets(selected_dir, sizeof(selected_dir), stdin);
                strip_newline(selected_dir);
                expand_home_path(selected_dir, sizeof(selected_dir));
                save_dir(selected_dir);
                break;
            }
        }
    } else {
        printf("Enter directory (or press enter for default ~/Pictures): ");
        fgets(selected_dir, sizeof(selected_dir), stdin);
        strip_newline(selected_dir);

        if (selected_dir[0] == '\0') {
            snprintf(selected_dir, sizeof(selected_dir), "%s/Pictures", home);
        } else {
            expand_home_path(selected_dir, sizeof(selected_dir));
            save_dir(selected_dir);
        }

        if (stat(selected_dir, &stats) == -1) {
            perror("Directory doesn't exist");
            snprintf(selected_dir, sizeof(selected_dir), "%s/Pictures", home);
        } else if (!S_ISDIR(stats.st_mode)) {
            perror("This is not a directory");
            snprintf(selected_dir, sizeof(selected_dir), "%s/Pictures", home);
        }
    }

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

        struct memory_buffer chunk;
        chunk.data = malloc(1);
        chunk.size = 0;
        // fetch the raw html
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "download failed: %s\n", curl_easy_strerror(res));
            free(chunk.data);
            continue;
        }
        // realloc if its missing space and add +1 to hold the \0
        char *tmp = realloc(chunk.data, chunk.size + 1);
        if (!tmp) {
            perror("allocation error");
            free(chunk.data);
            continue;
        }
        chunk.data = tmp;
        chunk.data[chunk.size] = '\0';

        char image_url[512];
        if (!og_imageExtractor(chunk.data, image_url, sizeof(image_url))) {
            fprintf(stderr, "Could not find image URL\n");
            free(chunk.data);
            continue;
        }
        free(chunk.data);
        // image check here, gotta do multiple pointers based on the format i was given and what twitter is using
        char *slash_pos = strrchr(image_url, '/');
        char *query_pos = strchr(slash_pos, '?');
        char *end_pos = strrchr(image_url, '\0');
        char *ext_start, *ext_end, *id_end;

        if (query_pos != NULL) {
            char *format_pos = strstr(query_pos, "format=");
            char *amp_pos = strchr(format_pos + 7, '&');
            ext_start = format_pos + 7;
            if (amp_pos == NULL) {
                ext_end = end_pos;
            } else {
                ext_end = amp_pos;
            }
            id_end = query_pos;
        } else {
            char *dot_pos = strrchr(image_url, '.');
            char *colon_pos = strchr(dot_pos, ':');
            ext_start = dot_pos + 1;
            if (colon_pos == NULL) {
                ext_end = end_pos;
            } else {
                ext_end = colon_pos;
            }
            id_end = dot_pos;
        }
        // copying the whole thing back to its separate place and join it into a proper file
        char id[128];
        int i = 0;
        char *src = slash_pos + 1;
        while (src + i != id_end && i < (int)sizeof(id) - 1) {
            id[i] = src[i];
            i++;
        }
        id[i] = '\0';

        char ext[16];
        char *src2 = ext_start;
        int z = 0;
        while (src2 + z != ext_end && z < (int)sizeof(ext) - 1) {
            ext[z] = src2[z];
            z++;
        }
        ext[z] = '\0';

        char filename[160];
        snprintf(filename, sizeof(filename), "%s.%s", id, ext);
        char filepath[600];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, filename);

        if (make_dir(dirpath, 0755) != 0) {
            if (errno != EEXIST) {
                perror("mkdir failed");
                continue;
            }
        }

        FILE *fp = fopen(filepath, "wb");
        if (fp == NULL) {
            perror("Failed to open file for writing");
            continue;
        }
        // fetch the actual image after extraction
        curl_easy_setopt(curl, CURLOPT_URL, image_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "download failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("Saved: %s\n", filepath);
        }

        fclose(fp);
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
