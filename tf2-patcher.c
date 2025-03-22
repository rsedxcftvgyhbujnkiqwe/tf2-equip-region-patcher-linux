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

#define RETRY_INTERVAL 1000    // in milliseconds
#define MAX_RETRY_TIME 300     // in seconds

// Updated patch_t with separate sizes for pattern and replacement.
typedef struct {
    const unsigned char *pattern;      // The pattern to find
    size_t pattern_size;               // Size of the pattern
    const unsigned char *replacement;  // The replacement bytes
    size_t replacement_size;           // Size of the replacement bytes
} patch_t;

// Example patterns and replacements:
static const unsigned char pattern1[] = {
    0x66, 0x0F, 0xD6, 0x83, 0x0C, 0x02, 0x00, 0x00
};
static const unsigned char replacement1[] = {
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};


// List all patches with their individual sizes:
static patch_t g_patches[] = {
    { pattern1, sizeof(pattern1), replacement1, sizeof(replacement1) },
};

static const int NUM_PATCHES = sizeof(g_patches) / sizeof(g_patches[0]);

int compare_with_wildcard(const unsigned char *memory, const unsigned char *pattern, int size);
long attach_process(pid_t pid);
void detach_process(pid_t pid);
pid_t find_pid_by_name(const char *process_name);

/**
 * Scans memory from [start, end) in one read, and attempts to find/replace
 * each pattern in g_patches exactly once.
 *
 * Return value:
 *   - Nonzero if at least one patch was applied in this region.
 *   - 0 if no patches were applied in this region.
 */
int find_and_replace_patterns(int fd, unsigned long start, unsigned long end) {
    size_t region_size = end - start;
    if (region_size == 0) {
        return 0;
    }

    unsigned char *buffer = (unsigned char *)malloc(region_size);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory for region scan\n");
        return 0;
    }

    // Read the memory region.
    if (lseek(fd, start, SEEK_SET) < 0) {
        perror("lseek");
        free(buffer);
        return 0;
    }
    if (read(fd, buffer, region_size) != (ssize_t)region_size) {
        perror("Error reading memory");
        free(buffer);
        return 0;
    }

    int patched_any = 0;

    // For each patch, scan through the region to find & replace the pattern.
    for (int p = 0; p < NUM_PATCHES; p++) {
        const unsigned char *pattern      = g_patches[p].pattern;
        size_t pattern_size               = g_patches[p].pattern_size;
        const unsigned char *replacement  = g_patches[p].replacement;
        size_t replacement_size           = g_patches[p].replacement_size;

        for (unsigned long i = 0; i + pattern_size <= region_size; i++) {
            if (compare_with_wildcard(buffer + i, pattern, (int)pattern_size)) {
                // Compute the address in the process memory.
                unsigned long address = start + i;

                // Write the full replacement bytes to memory.
                if (pwrite(fd, replacement, replacement_size, address) != (ssize_t)replacement_size) {
                    perror("Error writing replacement bytes to memory");
                } else {
                    patched_any = 1;
                    printf("Found pattern (size=%zu) at 0x%lX; replaced with %zu bytes.\n",
                           pattern_size, address, replacement_size);
                }
                // If you want to replace only the first occurrence of each pattern, break here.
                break;
            }
        }
    }

    free(buffer);
    return patched_any;
}

int main(void) {
    printf("========== Equip Patcher ==========\n");
    printf("Waiting for game to start....\n");

    const char *process_name = "tf_linux64";
    pid_t pid = -1;
    time_t start_time = time(NULL);

    // Wait up to MAX_RETRY_TIME seconds for the process to appear.
    while ((pid == -1) && (difftime(time(NULL), start_time) < MAX_RETRY_TIME)) {
        pid = find_pid_by_name(process_name);
        if (pid == -1) {
            usleep(RETRY_INTERVAL * 1000);
        }
    }

    if (pid == -1) {
        fprintf(stderr, "Could not find process '%s' within %d seconds.\n",
                process_name, MAX_RETRY_TIME);
        return 1;
    }

    printf("Found game at PID %d\n", pid);
    printf("Waiting for pattern module load (client.so)...\n");

    int pattern_found = 0;
    start_time = time(NULL); // reset start time to measure patch wait

    // Wait up to MAX_RETRY_TIME seconds for client.so to be mapped and patches to be found.
    while ((difftime(time(NULL), start_time) < MAX_RETRY_TIME) && !pattern_found) {
        if (attach_process(pid) != 0) {
            usleep(RETRY_INTERVAL * 1000);
            continue;
        }

        char maps_filename[256], mem_filename[256];
        snprintf(maps_filename, sizeof(maps_filename), "/proc/%d/maps", pid);
        snprintf(mem_filename, sizeof(mem_filename), "/proc/%d/mem",  pid);

        FILE *maps_file = fopen(maps_filename, "r");
        int mem_fd       = open(mem_filename, O_RDWR);

        if (!maps_file || mem_fd == -1) {
            perror("Error opening maps or mem file");
            if (maps_file) fclose(maps_file);
            if (mem_fd != -1) close(mem_fd);
            detach_process(pid);
            usleep(RETRY_INTERVAL * 1000);
            continue;
        }

        // Scan /proc/PID/maps for "client.so" regions and patch them.
        char line[256];
        while (fgets(line, sizeof(line), maps_file)) {
            if (strstr(line, "client.so")) {
                unsigned long start_addr = 0, end_addr = 0;
                if (sscanf(line, "%lx-%lx", &start_addr, &end_addr) == 2) {
                    int patched = find_and_replace_patterns(mem_fd, start_addr, end_addr);
                    if (patched) {
                        pattern_found = 1;
                        break;
                    }
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
    // Allows 0xFF to act as a wildcard, matching any byte.
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
    snprintf(cmd, sizeof(cmd), "pgrep %s", process_name);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        return -1;
    }
    pid_t pid = -1;
    if (fscanf(fp, "%d", &pid) != 1) {
        pid = -1; // no PID found
    }
    pclose(fp);
    return pid;
}
