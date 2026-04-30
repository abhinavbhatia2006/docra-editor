#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

void clear_screen() {
    printf("\033[H\033[J");
}

int main(void) {
    DIR *d;
    struct dirent *dir;
    struct stat file_stat;

    while (1) {
        clear_screen();
        printf("====================================================\n");
        printf("          DOCRA SERVER LIVE MONITOR DASHBOARD       \n");
        printf("====================================================\n");
        printf("%-25s | %-15s | %-15s\n", "ROOM NAME", "FILE SIZE", "LAST ACTIVE");
        printf("----------------------------------------------------\n");

        d = opendir(".");
        if (d) {
            int active_rooms = 0;
            while ((dir = readdir(d)) != NULL) {
                if (strstr(dir->d_name, ".log") != NULL) {
                    char filepath[512]; 
                    snprintf(filepath, sizeof(filepath), "./%s", dir->d_name);
                    
                    if (stat(filepath, &file_stat) == 0) {
                        char room_name[256];
                        
                        snprintf(room_name, sizeof(room_name), "%s", dir->d_name);
                        char *ext = strstr(room_name, ".log");
                        if (ext != NULL) {
                            *ext = '\0'; 
                        }

                        char time_str[64];
                        struct tm *tm_info = localtime(&file_stat.st_mtime);
                        strftime(time_str, 26, "%H:%M:%S", tm_info);

                        printf("%-25s | %-10ld bytes | %-15s\n", 
                               room_name, file_stat.st_size, time_str);
                        active_rooms++;
                    }
                }
            }
            closedir(d);
            
            printf("----------------------------------------------------\n");
            printf("Total Active Rooms Monitored: %d\n", active_rooms);
        } else {
            printf("Error: Could not read server directory.\n");
        }

        printf("====================================================\n");
        printf("Press Ctrl+C to exit monitor...\n");

        sleep(1);
    }
    return 0;
}