#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <curl/curl.h>
#include <jansson.h>
#include <libgen.h>
#include "storage.h"

static struct option long_options[] = {
    {"disk",   required_argument, 0, 'd'},
    {"nodeid", required_argument, 0, 'n'},
    {"help",   no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

typedef struct http_t {
    char *data;
    size_t size;

} http_t;

void diep(char *str) {
    perror(str);
    exit(EXIT_FAILURE);
}

size_t curl_write_cb(char *in, size_t size, size_t nmemb, http_t *data) {
    size_t r = size * nmemb;

    data->data = realloc(data->data, r);
    memcpy(data->data + data->size, in, r);
    data->size += r;
    data->data[data->size - 1] = '\0';

    return r;
}

static char *fetch_datapoints(char *endpoint) {
    CURL *curl;
    CURLcode res;
    http_t http = {
        .data = NULL,
        .size = 0,
    };

    if(!(curl = curl_easy_init()))
        return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &http);

    res = curl_easy_perform(curl);

    if(res != CURLE_OK) {
        fprintf(stderr, "[-] fetching data failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);

    return http.data;
}

static char *send_response(char *json, char *endpoint) {
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;

    if(!(curl = curl_easy_init()))
        return NULL;

    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, -1L);

    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    res = curl_easy_perform(curl);

    if(res != CURLE_OK) {
        fprintf(stderr, "[-] fetching data failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_cleanup(curl);

    return NULL;
}

int main(int argc, char *argv[]) {
    int option_index = 0;
    char *target = NULL;
    char *nodeid = NULL;
    char endpoint[1024];

    printf(COLOR_CYAN "[+] initializing storage-proof verifier" COLOR_RESET "\n");

    while(1) {
        int i = getopt_long_only(argc, argv, "", long_options, &option_index);

        if(i == -1)
            break;

        switch(i) {
            case 'd':
                target = optarg;
                break;

            case 'n':
                nodeid = optarg;
                break;

            case 'h':
                printf("help\n");
                return 1;

            case '?':
            default:
               exit(EXIT_FAILURE);
        }

    }

    if(target == NULL) {
        fprintf(stderr, "[-] missing target device\n");
        return 1;
    }

    if(nodeid == NULL) {
        fprintf(stderr, "[-] missing nodeid\n");
        return 1;
    }

    char *webtarget = basename(target);

    struct stat sb;
    if(lstat(target, &sb) < 0)
        diep(target);

    if(sb.st_mode & S_IFBLK) {
        printf(COLOR_GREEN "[+] running on block device" COLOR_RESET "\n");
    }

    if(sb.st_mode & S_IFREG) {
        printf(COLOR_YELLOW "[+] running on regular file (debug only)" COLOR_RESET "\n");
    }

    int fd;
    if((fd = open(target, O_RDONLY)) < 0)
        diep("open");

    sprintf(endpoint, "http://127.0.0.1:6010/proof/challenge/%s/%s", nodeid, webtarget);
    printf("[+] fetching verification datapoints: %s\n", endpoint);
    char *json = fetch_datapoints(endpoint);

    json_error_t jsonerror;
    json_t *root = json_loads(json, 0, &jsonerror);

    if(!json_is_array(root)) {
        printf("malformed expected json response\n");
        return 1;
    }

    size_t length = json_array_size(root);
    json_t *response = json_object();
    char convert[32];

    printf("[+] reading %lu datapoints\n", length);

    for(size_t i = 0; i < length; i++) {
        json_t *item = json_array_get(root, i);
        const char *sitem = json_string_value(item);
        uint64_t index = atoll(sitem);

        lseek(fd, index * sizeof(uint64_t), SEEK_SET);
        uint64_t value;

        if(read(fd, &value, sizeof(value)) != sizeof(value))
            diep("read");

        sprintf(convert, "%016lx", value);
        json_object_set_new(response, sitem, json_string(convert));
    }

    char *reply = json_dumps(response, 0);
    puts(reply);

    sprintf(endpoint, "http://127.0.0.1:6010/proof/verify/%s/%s", nodeid, webtarget);
    send_response(reply, endpoint);

    return 0;
}
