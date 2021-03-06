#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include "cpubench.h"

static struct option long_options[] = {
    {"seed", required_argument, 0, 's'},
    {"help", no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

typedef struct benchmark_t {
    struct timeval time_begin;
    struct timeval time_end;
    uint64_t seed;
    uint64_t final;
    size_t length;

} benchmark_t;

void diep(char *str) {
    perror(str);
    exit(EXIT_FAILURE);
}

static char *cpu_modelname() {
    char *modelname = NULL;
    char buffer[1024], *match, *endline;
    ssize_t len;
    int fd;

    if((fd = open("/proc/cpuinfo", O_RDONLY)) < 0)
        return strdup("unknown");

    while((len = read(fd, buffer, sizeof(buffer))) > 0) {
        buffer[len] = '\0';

        if(!(match = strstr(buffer, "model name")))
            continue;

        if(!(match = strchr(match, ':')))
            continue;

        if(!(endline = strchr(match, '\n')))
            continue;

        modelname = strndup(match + 2, endline - match - 2);
        break;
    }

    close(fd);

    return modelname;
}

static double time_spent(struct timeval *timer) {
    return (((size_t) timer->tv_sec * 1000000) + timer->tv_usec) / 1000000.0;
}

static double speed(size_t size, double timed) {
    // return (size / timed) / (1024 * 1024);
    return (size / timed) / 1024;
}

benchmark_t *benchmark(benchmark_t *source) {
    gettimeofday(&source->time_begin, NULL);

    size_t values = 120 * 1024 * 1024;
    uint64_t seed = source->seed;

    source->length = values * sizeof(seed);

    for(size_t offset = 0; offset < values; offset++) {
        seed = crc64((uint8_t *) &seed, sizeof(seed));
    }

    source->final = seed;

    gettimeofday(&source->time_end, NULL);

    return source;
}

int main(int argc, char *argv[]) {
    int option_index = 0;
    char *seeds = NULL;

    printf(COLOR_CYAN "[+] initializing grid-cpu-benchmark client" COLOR_RESET "\n");

    while(1) {
        int i = getopt_long_only(argc, argv, "", long_options, &option_index);

        if(i == -1)
            break;

        switch(i) {
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

    if(seeds == NULL) {
        fprintf(stderr, "[-] missing original seed\n");
        return 1;
    }

    if(strlen(seeds) != 18 || strncmp(seeds, "0x", 2) != 0) {
        fprintf(stderr, "[-] malformed seed (expected: 0x................)\n");
        return 1;
    }

    uint64_t seed = strtoull(seeds, NULL, 16);

    printf("[+] parsed seed: 0x%016lx\n", seed);

    char *cpumodel = cpu_modelname();
    printf("[+] cpu model name: " COLOR_GREEN "%s" COLOR_RESET "\n", cpumodel);
    free(cpumodel);

    benchmark_t cpubench = {
        .seed = seed,
    };

    // single-thread
    printf("[+] testing single-thread\n");
    benchmark(&cpubench);

    {
    double timed = time_spent(&cpubench.time_end) - time_spent(&cpubench.time_begin);
    double cspeed = speed(cpubench.length, timed);

    printf("[+] seed 0x%lx: 0x%lx\n", cpubench.seed, cpubench.final);
    printf("[+] single thread score: %.0f\n", cspeed);
    }

    // multi-thread
    long cpucount = sysconf(_SC_NPROCESSORS_ONLN);
    double totalspeed = 0;

    printf("[+] testing multi-threads (%ld threads)\n", cpucount);

    #pragma omp parallel for num_threads(cpucount)
    for(long a = 0; a < cpucount; a++) {
        benchmark_t cpubench = {
            .seed = seed,
        };

        benchmark(&cpubench);

        printf("[+] seed 0x%lx: 0x%lx\n", cpubench.seed, cpubench.final);

        double timed = time_spent(&cpubench.time_end) - time_spent(&cpubench.time_begin);
        double cspeed = speed(cpubench.length, timed);

        #pragma omp atomic
        totalspeed += cspeed;

        // printf("\r[+] single thread score: %.0f MB/s\n", cspeed);
    }

    printf("\r[+] multi-threads score: %.0f\n", totalspeed);

    return 0;
}
