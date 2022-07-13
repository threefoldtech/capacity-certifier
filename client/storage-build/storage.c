#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include "storage.h"

static struct option long_options[] = {
    {"disk", required_argument, 0, 'd'},
    {"seed", required_argument, 0, 's'},
    {"help", no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

void diep(char *str) {
    perror(str);
    exit(EXIT_FAILURE);
}

static double time_spent(struct timeval *timer) {
    return (((size_t) timer->tv_sec * 1000000) + timer->tv_usec) / 1000000.0;
}

static double speed(size_t size, double timed) {
    return (size / timed) / (1024 * 1024);
}

int main(int argc, char *argv[]) {
    int option_index = 0;
    char *target = NULL;
    char *seeds = NULL;

    printf(COLOR_CYAN "[+] initializing storage-proof client" COLOR_RESET "\n");

    while(1) {
        int i = getopt_long_only(argc, argv, "", long_options, &option_index);

        if(i == -1)
            break;

        switch(i) {
            case 'd':
                target = optarg;
                break;

            case 's':
                seeds = optarg;
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

    if(seeds == NULL) {
        fprintf(stderr, "[-] missing original seed\n");
        return 1;
    }

    if(strlen(seeds) != 18 || strncmp(seeds, "0x", 2) != 0) {
        fprintf(stderr, "[-] malformed seed (expected: 0x................)\n");
        return 1;
    }

    uint64_t seed = strtoull(seeds, NULL, 16);

    printf("[+] target device: %s\n", target);
    printf("[+] parsed seed: 0x%016lx\n", seed);

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
    if((fd = open(target, O_RDWR)) < 0)
        diep("open");

    off_t fullsize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    printf("[+] target size: " COLOR_GREEN "%.0f GB" COLOR_RESET " [%lu bytes]\n", GB(fullsize), fullsize);

    if(fullsize % 8 != 0) {
        fprintf(stderr, "[-] target length not correctly aligned\n");
        return 1;
    }

    size_t values = fullsize / sizeof(uint64_t);

    printf("[+] generating crc length: %lu\n", values);

    // time statistics
    struct timeval time_total_begin, time_total_end;
    struct timeval time_begin, time_end;
    gettimeofday(&time_begin, NULL);

    printf("[+] writing data: initializing...");
    fflush(stdout);

    ssize_t bufsize = 8 * 1024 * 1024;
    ssize_t bufoff = 0;
    char *buffer = calloc(sizeof(char), bufsize);

    if(fullsize % bufsize != 0) {
        printf("buffer not possible\n");
        return 1;
    }

    gettimeofday(&time_total_begin, NULL);

    for(size_t offset = 0; offset < values; offset++) {
        memcpy(buffer + bufoff, &seed, sizeof(seed));
        bufoff += sizeof(seed);

        if(bufoff == bufsize) {
            if(write(fd, buffer, bufsize) != bufsize)
                diep("write");

            gettimeofday(&time_end, NULL);

            double timed = time_spent(&time_end) - time_spent(&time_begin);
            double cspeed = speed(bufsize, timed);
            double progress = (offset / (double) values) * 100;

            printf("\r[+] writing data: %.2f %% [%.0f MB/s]\033[0K", progress, cspeed);
            fflush(stdout);

            gettimeofday(&time_begin, NULL);
            bufoff = 0;
        }

        seed = crc64((uint8_t *) &seed, sizeof(seed));
    }

    gettimeofday(&time_total_end, NULL);

    double timed = time_spent(&time_total_end) - time_spent(&time_total_begin);
    double cspeed = speed(fullsize, timed);

    printf("\n[+] device ready, write speed: %.0f MB/s\n", cspeed);

    return 0;
}
