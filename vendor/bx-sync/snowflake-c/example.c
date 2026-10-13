/**
 * Example for snowflake
 *
 * Create a bunch of process that generate in loop ID and store into a file
 * for each process.
 * Not perfect testing, but still kind of testing.
 */
#define SNOWFLAKE_IMPLEMENTATION
#include "snowflake.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_PROCESS 50
#define MAX_ID 10000

int main(void) {
    int process = 0;
new_fork:    
    if (fork() == 0) {
        snowflake_ctx_t ctx;
        char filename[255];
        FILE *fp = NULL;

        snprintf(filename, 255, "./process.%d.txt", getpid());
        fp = fopen(filename, "w");
        if (!fp) {
            fprintf(stderr, "Cannot open file %s for writing\n", filename);
            return EXIT_FAILURE;
        }
        
        if (!snowflake_open(&ctx, 10)) {
            fprintf(stderr, "Cannot open snowflake context\n");
            return EXIT_FAILURE;
        }

        for(int i = 0; i < MAX_ID; i++) {
            uint64_t id = snowflake_get(&ctx);
            if (id == SNOWFLAKE_INVALID) {
                fprintf(stderr, "Error generating an id\n");
                continue;
            }
            fprintf(fp, "%lX\n", id);
        }
        fclose(fp);

        snowflake_close(&ctx);
        return EXIT_SUCCESS;
    } else {
        process++;
        if (process < MAX_PROCESS) {
            goto new_fork;
        }
        printf("Should generate %d ID\n", MAX_PROCESS * MAX_ID);
    }
    return EXIT_SUCCESS;
}
