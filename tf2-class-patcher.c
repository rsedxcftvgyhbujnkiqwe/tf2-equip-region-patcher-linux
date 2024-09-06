#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/user.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#define RETRY_INTERVAL 1000
#define MAX_RETRY_TIME 300

#define PATTERN_SIZE 8
#define REPLACE_SIZE 55
const unsigned char pattern[PATTERN_SIZE] = {0xE8,0x46,0x60,0xEF,0xFF,0x4C,0x8D,0xA8};
unsigned char replacement[REPLACE_SIZE] = {0x48, 0xC7, 0xC3, 0x01, 0x00, 0x00, 0x00, 0x89, 0xD9, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xD3, 0xE0, 0x09, 0x04, 0x87, 0x4c, 0x63, 0xF9, 0x41, 0x8B, 0x84, 0x24, 0xA0, 0x02, 0x00, 0x00, 0x43, 0x89, 0x84, 0xBC, 0x74, 0x03, 0x00, 0x00, 0x48, 0xFF, 0xC3, 0x48, 0x83, 0xFB, 0x0A, 0x74, 0xD8, 0xEB, 0x7};

int compare_with_wildcard(const unsigned char *memory, const unsigned char *pattern, int size);
long attach_process(pid_t pid);
void detach_process(pid_t pid);
pid_t find_pid_by_name(const char *process_name);
unsigned long find_and_replace_pattern(int fd, unsigned long start, unsigned long end);

int main() {
    printf("========== Equip Patcher ==========\n");
    printf("Waiting for game to start....\n");

    const char *process_name = "tf_linux64";
    pid_t pid = -1;
    time_t start_time = time(NULL);
    int pattern_found = 0;

    while (pid == -1 && difftime(time(NULL), start_time) < MAX_RETRY_TIME) {
        pid = find_pid_by_name(process_name);
        if (pid == -1) {
            usleep(RETRY_INTERVAL * 1000);
        }
    }

    if (pid == -1) {
        fprintf(stderr, "Could not find process '%s' within %d seconds.\n", process_name, MAX_RETRY_TIME);
        return 1;
    }

    printf("Found game at PID %d\n",pid);

    printf("Waiting for pattern module load...\n");

    while (difftime(time(NULL), start_time) < MAX_RETRY_TIME && !pattern_found) {
        if (attach_process(pid) != 0) {
            usleep(RETRY_INTERVAL * 1000);
            continue;
        }

        char maps_filename[256], mem_filename[256];
        sprintf(maps_filename, "/proc/%d/maps", pid);
        sprintf(mem_filename, "/proc/%d/mem", pid);
        FILE *maps_file = fopen(maps_filename, "r");
        int mem_fd = open(mem_filename, O_RDWR);
        if (maps_file == NULL || mem_fd == -1) {
            perror("Error opening files");
            detach_process(pid);
            usleep(RETRY_INTERVAL * 1000);
            continue;
        }

        char line[256];
        unsigned long start, end;

        fseek(maps_file, 0, SEEK_SET);
        while (fgets(line, sizeof(line), maps_file) != NULL) {
            if (strstr(line, "client.so")) {
                sscanf(line, "%lx-%lx", &start, &end);
                
                unsigned long address = find_and_replace_pattern(mem_fd, start, end);
                if (address) {
                    pattern_found = 1;
                    printf("Found pattern\n");
                    printf("Replaced pattern\n");
                    break;
                }
            }
        }

        fclose(maps_file);
        close(mem_fd);
        detach_process(pid);

        if (!pattern_found) {
            usleep(RETRY_INTERVAL * 1000);
        }
    }

    return 0;
}

int compare_with_wildcard(const unsigned char *memory, const unsigned char *pattern, int size) {
    for (int i = 0; i < size; ++i) {
        if (pattern[i] != 0xFF && memory[i] != pattern[i]) {
            return 0;
        }
    }
    return 1;
}

long attach_process(pid_t pid) {
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
        perror("ptrace attach");
        return -1;
    }
    waitpid(pid, NULL, 0);
    return 0;
}

void detach_process(pid_t pid) {
    if (ptrace(PTRACE_DETACH, pid, NULL, NULL) == -1) {
        perror("ptrace detach");
    }
}

pid_t find_pid_by_name(const char *process_name) {
    char cmd[512];
    sprintf(cmd, "pgrep %s", process_name);
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        perror("popen");
        return -1;
    }
    pid_t pid = -1;
    fscanf(fp, "%d", &pid);
    pclose(fp);
    return pid;
}

unsigned long find_and_replace_pattern(int fd, unsigned long start, unsigned long end) {
    unsigned char *buffer = malloc(end - start);
    if (buffer == NULL) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 0;
    }

    lseek(fd, start, SEEK_SET);
    if (read(fd, buffer, end - start) != end - start) {
        perror("Error reading memory");
        free(buffer);
        return 0;
    }

    unsigned long address = 0;
    for (unsigned long i = 0; i < end - start - PATTERN_SIZE; ++i) {
        if (compare_with_wildcard(buffer + i, pattern, PATTERN_SIZE)) {
            address = start + i;
            if (pwrite(fd, replacement, REPLACE_SIZE, address) != REPLACE_SIZE) {
                perror("Error writing memory");
            }
            break;
        }
    }

    free(buffer);
    return address;
}
