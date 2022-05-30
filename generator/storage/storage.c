#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <jansson.h>
#include <hiredis.h>
#include "storage.h"

typedef struct capacity_t {
    uint64_t seed;
    uint64_t size;
    uint64_t *offsets;
    uint64_t *results;
    size_t length;

} capacity_t;

typedef struct backend_t {
    char *host;
    int port;
    char *namespace;
    char *password;

} backend_t;

static char *human_readable_suffix = "kMGT";

size_t *human_readable_parse(char *input, size_t *target) {
    char *endp = input;
    char *match = NULL;
    size_t shift = 0;
    errno = 0;

    long double value = strtold(input, &endp);
    if(errno || endp == input || value < 0)
        return NULL;

    if(!(match = strchr(human_readable_suffix, *endp)))
        return NULL;

    if(*match)
        shift = (match - human_readable_suffix + 1) * 10;

    *target = value * (1LU << shift);

    return target;
}

static double time_spent(struct timeval *timer) {
    return (((size_t) timer->tv_sec * 1000000) + timer->tv_usec) / 1000000.0;
}

static uint64_t time64(struct timeval *input) {
    return (((size_t) input->tv_sec * 1000000) + input->tv_usec);
}

static double speed(size_t size, double timed) {
    return (size / timed) / (1024 * 1024);
}

static int u64cmp(const void *a1, const void *a2) {
    uint64_t xa1 = *(const uint64_t *) a1;
    uint64_t xa2 = *(const uint64_t *) a2;
    return xa1 - xa2;
}

void offsets_generate(uint64_t *dst, size_t size, uint64_t from, uint64_t to) {
    uint64_t limit = to - from;

    for(size_t i = 0; i < size; i++) {
        dst[i] = (rand64() % limit) + from;
    }

    qsort(dst, size, sizeof(uint64_t), u64cmp);
}

uint64_t crcxx(uint64_t source) {
    return crc64((uint8_t *) &source, sizeof(source));
}

char *capacity_dumps(capacity_t *capacity) {
    char key[32], convert[32];

    json_t *root = json_object();

    sprintf(convert, "%016lx", capacity->seed);
    json_object_set_new(root, "seed", json_string(convert));

    json_t *results = json_object();

    for(size_t i = 0; i < capacity->length; i++) {
        sprintf(key, "%lu", capacity->offsets[i]);
        sprintf(convert, "%016lx", capacity->results[i]);
        json_object_set_new(results, key, json_string(convert));
    }

    json_object_set_new(root, "results", results);
    json_object_set_new(root, "size", json_integer(capacity->size));

    return json_dumps(root, JSON_SORT_KEYS | JSON_COMPACT);
}

int capacity_save(backend_t *backend, char *key, char *json) {
    redisContext *kntxt = redisConnect(backend->host, backend->port);
    redisReply *reply;

    if(!kntxt) {
        perror("zdb");
        return 0;
    }

    if(kntxt->err) {
        fprintf(stderr, "[-] zdb: %s\n", kntxt->errstr);
        return 0;
    }

    if(!(reply = redisCommand(kntxt, "SELECT %s", backend->namespace))) {
        fprintf(stderr, "[-] zdb: could not execute command\n");
        return 0;
    }

    if(strcmp(reply->str, "OK") != 0) {
        printf("[-] warning: %s\n", reply->str);
    }

    freeReplyObject(reply);

    if(!(reply = redisCommand(kntxt, "SET %s %s", key, json))) {
        fprintf(stderr, "[-] zdb: could not execute command\n");
        return 0;
    }

    if(strcmp(reply->str, key) != 0) {
        printf("[-] could not commit report: %s\n", reply->str);
        freeReplyObject(reply);
        return 0;
    }

    printf("[+] capacity report saved\n");
    freeReplyObject(reply);

    return 1;
}

int main(int argc, char *argv[]) {
    capacity_t capacity = {
        .size = 1 << 30,
    };

    printf(COLOR_CYAN "[+] initializing storage-proof generator\n" COLOR_RESET);

    if(argc > 1) {
        human_readable_parse(argv[1], &capacity.size);
    }

    if(capacity.size < (1 << 30)) {
        fprintf(stderr, "[-] please do not use size smaller than 1 GB\n");
        return 1;
    }

    // amount of crc to compute
    size_t values = capacity.size / sizeof(uint64_t);

    printf("[+] generating dataset size: " COLOR_GREEN "%.0f GB" COLOR_RESET " (%lu bytes)\n", GB(capacity.size), capacity.size);
    printf("[+] generating crc length: %lu\n", values);

    // time statistics
    struct timeval time_begin, time_end, time_total_begin;
    gettimeofday(&time_begin, NULL);
    gettimeofday(&time_total_begin, NULL);

    // randomize
    uint64_t time_seed = time64(&time_begin);
    printf("[+] generated time seed: %lu\n", time_seed);
    srand64(time_seed);

    // generate seed
    capacity.seed = rand64();
    // capacity.seed = 0x0f6f8ca19f2c59c2; // FIXME
    uint64_t seed = capacity.seed; // variable seed
    printf("[+] generated storage seed: " COLOR_YELLOW "0x%016lx" COLOR_RESET "\n", seed);

    // amount of datapoint to compute
    // we request 256 datapoints per 100 GB
    size_t size_range = (capacity.size / (20 * S_GB)) + 1;

    capacity.length = 256 * size_range;

    // segments to divide full length (to maximize unifority)
    size_t offsets_segments = 32 * size_range;

    // datapoint per segments
    size_t offsets_segsize = capacity.length / offsets_segments;

    // generate list of offsets
    capacity.offsets = calloc(sizeof(uint64_t), capacity.length);
    capacity.results = calloc(sizeof(uint64_t), capacity.length);

    printf("[+] offsets to compute: %lu (%lu segments)\n", capacity.length, offsets_segments);

    // compute 64 offsets for each quarter
    // to maximize chance to have offset spread all over
    // the disk, we randomize offsets for each 1/8 of
    // the disk
    //
    // note: offsets are not bytes offsets but crc index

    size_t index_from = 0;
    size_t index_to = values / offsets_segments;
    size_t segment = values / offsets_segments;

    for(size_t i = 0; i < offsets_segments; i++) {
        offsets_generate(capacity.offsets + (i * offsets_segsize), offsets_segsize, index_from, index_to);
        index_from = index_to;
        index_to += segment;
    }

    size_t source = 0;

    printf(COLOR_GREEN "[+] starting generating sequence" COLOR_RESET "\n");
    printf("[+] computing: initializing...");
    fflush(stdout);

    for(size_t offset = 0; offset < capacity.length; offset++) {
        gettimeofday(&time_begin, NULL);

        for(size_t i = source; i < capacity.offsets[offset]; i++)
            seed = crcxx(seed);

        gettimeofday(&time_end, NULL);

        size_t length = (capacity.offsets[offset] - source) * sizeof(uint64_t);
        double timed = time_spent(&time_end) - time_spent(&time_begin);
        double cspeed = speed(length, timed);
        double progress = (offset / (double) capacity.length) * 100;

        printf("\r[+] computing: %.2f %% [%.0f MB/s]\033[0K", progress, cspeed);
        fflush(stdout);

        source = capacity.offsets[offset];
        capacity.results[offset] = seed;
    }

    // grand total speed summary
    gettimeofday(&time_end, NULL);
    double timed = time_spent(&time_end) - time_spent(&time_total_begin);
    double cspeed = speed(capacity.size, timed);

    printf("\r[+] data generated in %.1f seconds [%.2f MB/s]\n", timed, cspeed);

    char *json = capacity_dumps(&capacity);

    // puts(json);
    printf("[+] capacity result length: %lu bytes\n", strlen(json));

    char keyname[128];
    sprintf(keyname, "storage-%lu-%016lx", capacity.size, capacity.seed);

    printf("[+] saving capacity report: %s\n", keyname);

    backend_t backend = {
        .host = "127.0.0.1",
        .port = 9911,
        .namespace = "storage-pool",
        .password = NULL,
    };

    capacity_save(&backend, keyname, json);

    return 0;
}
