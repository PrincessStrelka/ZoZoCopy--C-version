#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <selinux/label.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define COUNT(arr) (sizeof(arr) / sizeof(*arr))

// my global variables
#define nullptr NULL
char osSep = '/';
long targetFileCount = 1347375; // the aim is to process 1347375 files in a reasonable speed

// https://www.geeksforgeeks.org/how-to-append-a-character-to-a-string-in-c/
void addChar(char *s, char c) {
        while (*s++)
                ;     // Move pointer to the end
        *(s - 1) = c; // Append the new character
        *s = '\0';    // Add null terminator to mark new end
}
// https:www.geeksforgeeks.org/how-to-convert-an-integer-to-a-string-in-c/
void intToStr(int N, char *str) {
        int i = 0;

        // Save the copy of the number for sign
        int sign = N;

        // If the number is negative, make it positive
        if (N < 0)
                N = -N;

        // Extract digits from the number and add them to the
        // string
        while (N > 0) {

                // Convert integer digit to character and store
                // it in the str
                str[i++] = N % 10 + '0';
                N /= 10;
        }

        // If the number was negative, add a minus sign to the
        // string
        if (sign < 0) {
                str[i++] = '-';
        }

        // Null-terminate the string
        str[i] = '\0';

        // Reverse the string to get the correct order
        for (int j = 0, k = i - 1; j < k; j++, k--) {
                char temp = str[j];
                str[j] = str[k];
                str[k] = temp;
        }
}
void bitwisePrint(int var) {
        int var_copy = var;                                            // copy the input into a modifyable variable
        int mask_size = sizeof(var) * 8;                               // get the amount of bits in the input variable
        for (size_t bit = 0; bit < mask_size; bit++) {                 // itterate through every bit in the input variable
                bool is_one = var_copy & 1 << (mask_size - 1);         // check if the highest bit is one
                printf("\033[%im%u\033[0m", is_one ? 97 : 30, is_one); // print the bit
                var_copy = var_copy << 1;                              // put the next bit into the highest bit
        }
}
void ensureOsSeperator(char *s) {
        if (s[strlen(s) - 1] != osSep) {
                addChar(s, osSep);
        }
}
void printTime(char label[], unsigned int mask, unsigned int maskConst, struct statx_timestamp timestamp) {
        if (mask & maskConst) {
                // get times from timestamp
                signed long long tv_epoch = timestamp.tv_sec;
                unsigned int tv_nsec = timestamp.tv_nsec;

                // get times for set inode field
                long long adjusted = tv_epoch + 2147483648;
                signed long long sif_time = (adjusted % 4294967296) - 2147483648;

                // shift it over by two bits
                // store the multiplier in the lower two bits
                unsigned int sif_extra = (tv_nsec << 2) + floor(adjusted / 4294967296);

                // get epoch as a formatted string
                struct tm tm;
                char dateStr[255];
                char epochAsstr[10];
                intToStr(timestamp.tv_sec, epochAsstr);
                memset(&tm, 0, sizeof(struct tm));
                strptime(epochAsstr, "%s", &tm);
                strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S", &tm);

                // print time data
                printf("%s: %s.%u | %lld %u\n", label, dateStr, tv_nsec, sif_time, sif_extra);

        } else
                printf("%s: -\n", label);
}

void replaceFirstInstance(char *src_string, char *substring, char *replacement) {
        char const *substring_position = strstr(src_string, substring);
        if (substring_position) {
                // find the index of the substring match
                int matchindex = substring_position - src_string;

                // get everything from before the substring
                char modifiedString[255] = "";
                strncpy(modifiedString, src_string, matchindex);

                // get everything from after the substring
                char lastPart[255] = "";
                strncpy(lastPart, src_string + matchindex + strlen(substring), strlen(src_string));

                // concatinate first part of string, replacement, and last part of string
                strcat(modifiedString, replacement);
                strcat(modifiedString, lastPart);

                // put the modified string into string
                strcpy(src_string, modifiedString);
        }
}

void copyPath(char src_path[], char *baseFolder, char *destFolder) {
        printf("\033[34m%s\033[0m\n", src_path);
        /* ---- CREATE NEW FILE PATH ---- */
        // ensure basefolder does not end with os sep
        char copyBaseFolder[1000];
        strcpy(copyBaseFolder, baseFolder);
        if ((copyBaseFolder[strlen(copyBaseFolder) - 1]) == '/')
                copyBaseFolder[strlen(copyBaseFolder) - 1] = '\0';

        // get the source path base
        char *lastOsSepIndex = strrchr(copyBaseFolder, osSep);
        char *sourcePathBase = lastOsSepIndex ? lastOsSepIndex + 1 : copyBaseFolder;

        // create a modified base path
        char modifiedBaseFolder[1000];
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        sprintf(modifiedBaseFolder, "%s%s-%d-%02d-%02d_%02d.%02d.%02d", destFolder, sourcePathBase, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

        // swap out the base path in the filepath for the modified base path
        char dst_path[1000] = "";
        strcpy(dst_path, src_path);
        replaceFirstInstance(dst_path, copyBaseFolder, modifiedBaseFolder);
        printf("\033[35m%s\033[0m\n", dst_path);

        /* ---- GET BASE FILE STATS ---- */
        // fill in the parameters for statx call, and call it
        struct statx src_stxBuf;

        // print the file we are statting, and the return code from calling statx
        int src_statx_err = statx(AT_FDCWD,
                                  src_path,
                                  AT_EMPTY_PATH | AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW | AT_STATX_FORCE_SYNC,
                                  STATX_BASIC_STATS | STATX_BTIME | STATX_MNT_ID | STATX_DIOALIGN | STATX_MNT_ID_UNIQUE,
                                  &src_stxBuf);
        printf("statx result: %i\n", src_statx_err);
        if (src_statx_err < 0)
                return;

        // print the stx mask result
        printf("stx_mask: ");
        bitwisePrint(src_stxBuf.stx_mask);
        printf("\n");

        // if the stx buffer contains a time, print the time, else dont
        printTime("aTime        (Access)", src_stxBuf.stx_mask, STATX_ATIME, src_stxBuf.stx_atime);
        printTime("mTime        (Modify)", src_stxBuf.stx_mask, STATX_MTIME, src_stxBuf.stx_mtime);
        printTime("cTime        (Change)", src_stxBuf.stx_mask, STATX_CTIME, src_stxBuf.stx_ctime);
        printTime("crTime/bTime (Birth )", src_stxBuf.stx_mask, STATX_BTIME, src_stxBuf.stx_btime);

        /* ---- COPY FILE ---- */

        /*
        int byte_count, src_fd, dst_fd, copy_ret;
        byte_count = 4096; // how big does this need to be?
        unsigned char copy_buf[byte_count];

        // get file descriptors
        src_fd = open(src_path, O_RDONLY);                                         // get file descriptor from src path as read only
        dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_EXCL | O_NOATIME, S_IRWXU); // get file descriptor from src path as write only, possibly create dir
        if (src_fd == -1 || dst_fd == -1)
                printf("Error getting File Descriptor.\n");

        close(src_fd);
        close(dst_fd);*/
}

void travelDirectory(char sourceDirectory[], char *baseFolder, char *destFolder) {
        ensureOsSeperator(sourceDirectory);

        copyPath(sourceDirectory, baseFolder, destFolder);

        DIR *sourceDir = opendir(sourceDirectory); // returns DIR struct upon success, NULL upon failure
        struct dirent *dp;
        char *file_name; // define the filename variable
        struct stat filePathStatBuf;

        // declare and initialise a variable that will store the sourcepath +
        // filename should implement hannahs suggestion for dynamic allocation
        char filePath[1000];

        // if sourcedir is not a directory, exit
        if (!sourceDir)
                return;

        // loop through every file under sourcePath
        while ((dp = readdir(sourceDir)) != NULL) {
                // get the file name from dp
                file_name = dp->d_name;

                // if we are on a refrence to the current(.) or parent(..)
                // directory, skip this loop
                if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0)
                        continue;

                // concatonate the source path and file name together into a new
                // variable
                strcpy(filePath, sourceDirectory);
                strcat(filePath, file_name);

                // find if the filepath is a directory or not
                stat(filePath, &filePathStatBuf);
                if (S_ISDIR(filePathStatBuf.st_mode)) {
                        // if it is a directory
                        // getFileInfo(sourcePath);
                        travelDirectory(filePath, baseFolder, destFolder);
                } else {
                        // if it is a filepath
                        copyPath(filePath, baseFolder, destFolder);
                }
        }
        closedir(sourceDir); // closes the sourceDir DIR struct
}

struct ext4_time {
        char label[64];
        struct statx_timestamp statx_time;
        signed long long epoch;
        unsigned long long extra;
        signed long long ns_epoch;
};

// global variables
struct ext4_time lowest_time = {.ns_epoch = INT64_MAX};
struct ext4_time *timestamps_to_fix[4];
size_t fix_ts_index = 0;
unsigned int stx_mask = 0;

void printExt4Time(struct ext4_time *ext4_time) {
        // get epoch as a formatted string
        struct tm tm;
        char dateStr[255];
        char epochAsstr[10];
        intToStr(ext4_time->statx_time.tv_sec, epochAsstr);
        memset(&tm, 0, sizeof(struct tm));
        strptime(epochAsstr, "%s", &tm);
        strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S", &tm);

        // print the time struct
        printf("%s: %s.%u | %lld %llu | %lld", ext4_time->label, dateStr, ext4_time->statx_time.tv_nsec, ext4_time->epoch, ext4_time->extra, ext4_time->ns_epoch);
}

void fillInTime(struct ext4_time *ext4_time, char label[], struct statx_timestamp stx_time, unsigned int mask_const) {
        strcpy(ext4_time->label, label);

        if (stx_mask & mask_const) {
                // put times into ext4_time struct
                long long adjusted = stx_time.tv_sec + 2147483648;                         // epoch adjusted to be a positive number?
                ext4_time->epoch = (adjusted % (4294967296)) - 2147483648;                 // the epoch adjusted to be a modifer for adjusted?
                ext4_time->extra = (stx_time.tv_nsec << 2) + floor(adjusted / 4294967296); // shift tv_nsec over by two bits store the epoch multiplier in the lower two bits
                ext4_time->ns_epoch = (stx_time.tv_sec * 1000000000) + stx_time.tv_nsec;   // the nanosecond unix epoch
                ext4_time->statx_time = stx_time;                                          // put the time from the result of stx into our struct

                // print ext4 time
                printExt4Time(ext4_time);

                // if the ns timestamp is the lowest ns timestamp, put into lowest variable
                if (ext4_time->ns_epoch < lowest_time.ns_epoch) {
                        printf(" | NEW LOWEST: %lld -> %lld", lowest_time.ns_epoch, ext4_time->ns_epoch);
                        lowest_time.statx_time = ext4_time->statx_time;
                        lowest_time.epoch = ext4_time->epoch;
                        lowest_time.extra = ext4_time->extra;
                        lowest_time.ns_epoch = ext4_time->ns_epoch;
                }
                printf("\n");
        } else {

                // mark this timestamp as one to be filled in with the lowest timestamp
                printf("%s: -\n", ext4_time->label);
                timestamps_to_fix[fix_ts_index] = ext4_time;
                fix_ts_index += 1;
        }
}

int main() {
        // █ ▓ ░
        // ---- COPY FILE AND DATA ----
        // char *src_path = "/home/zoey/Desktop/source/sourcefile.txt";
        char *src_path = "/media/zoey/DATA/source/sourcefile.txt";

        // TODO: figure out the_bit_to_swap_out
        // char *the_bit_to_swap_out = "/home/zoey/Desktop/source";
        char *the_bit_to_swap_out = "/media/zoey/DATA/source";

        // ENSURE dst_folder AND ALL PARENT DIRECTORIES EXIST
        char dst_folder[] = "/home/zoey/Desktop/test/test/test";
        char temp_path[COUNT(dst_folder)];
        // bitwisePrint(temp_path);
        // printf("\n");
        for (size_t i = 0; i < (COUNT(dst_folder)); i++) {
                // if the current character is not os seperator,  or not the end of the loop, skip
                if (dst_folder[i] != osSep && i != COUNT(dst_folder) - 1)
                        continue;
                // copy everything up to this character into a temporary variable
                strncpy(temp_path, dst_folder, i + 1);
                // set last bit of temp_path to null terminator to avoid reading junk data as part of the path
                temp_path[i + 1] = '\0';
                // create a directory using the string in temp path
                // if (mkdir(temp_path, S_IRWXU | S_IRWXG | S_IRWXO) == -1)
                //        printf("Error creating [%s]: \033[31m%s\033[0m\n", temp_path, strerror(errno));
                // else
                //        printf("Created path [\033[32m%s\033[0m]\n", temp_path);

                if (mkdir(temp_path, S_IRWXU | S_IRWXG | S_IRWXO) != -1)
                        printf("Created path [\033[32m%s\033[0m]\n", temp_path);
        }

        // GENERATE PATH TO COPY FILE TO
        char dst_path[1000] = "";
        strcpy(dst_path, src_path);

        // swap out the_bit_to_swap_out in src_path with dst_folder
        char string_before_substring[255] = "";
        char string_after_substring[255] = "";
        int match_position = strstr(dst_path, the_bit_to_swap_out) - dst_path;
        strncpy(string_before_substring, dst_path, match_position);
        strncpy(string_after_substring, dst_path + match_position + strlen(the_bit_to_swap_out), strlen(dst_path));
        sprintf(dst_path, "%s%s%s", string_before_substring, dst_folder, string_after_substring);

        // and append current time
        // time_t t = time(NULL);
        // struct tm tm = *localtime(&t);
        // sprintf(dst_path, "%s%s%s-%d-%02d-%02d_%02d.%02d.%02d",
        //        string_before_substring, dst_folder, string_after_substring,
        //        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

        // print source and destination path
        printf("Src path: \033[34m%s\033[0m\n", src_path);
        printf("Dst path: \033[35m%s\033[0m\n", dst_path);

        // ---- the following only works on non directory files. if path was a directory, it would have been created by prior steps  ----

        // get the file directories of the source and destination paths
        int src_fd, dst_fd;
        if ((src_fd = open(src_path, O_RDONLY)) == -1)
                printf("Error creating source File Descriptor: \033[31m%s\033[0m\n", strerror(errno));
        if ((dst_fd = open(dst_path, O_CREAT | O_WRONLY | O_TRUNC, 0644)) == -1)
                printf("Error creating destination File Descriptor: \033[31m%s\033[0m\n", strerror(errno));

        // get the filesize of src_fd
        // TODO: is it better to use stat or fstat here?
        struct stat src_stat = {0};
        int result = 0;
        result = fstat(src_fd, &src_stat);
        if (result == -1)
                printf("Error getting source file stat: \033[31m%s\033[0m\n", strerror(errno));

        // copy all the bytes from the source file to the destination file
        // TODO: figure out how to stop from updating the last data access timestamp of the file
        size_t byte_buf_size = LONG_MAX; // TODO: how big should this be?
        off_t copy_start_point = 0;
        printf("Copy %lu bytes from FD %i to FD %i, %lu bytes at a time\n", src_stat.st_size, src_fd, dst_fd, byte_buf_size);
        size_t loopcount = 0;
        while (copy_start_point < src_stat.st_size) {
                printf("Loop #%lu, copy start [%ld]\n", loopcount += 1, copy_start_point);
                if (sendfile(dst_fd, src_fd, &copy_start_point, byte_buf_size) == -1)
                        break;
        }
        close(src_fd);
        close(dst_fd);

        // get stats of source file from statx
        struct statx src_stxBuf;
        result = statx(AT_FDCWD,
                       src_path,
                       AT_EMPTY_PATH | AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW | AT_STATX_FORCE_SYNC,
                       STATX_BASIC_STATS | STATX_BTIME | STATX_MNT_ID | STATX_DIOALIGN | STATX_MNT_ID_UNIQUE,
                       &src_stxBuf);
        if (result == -1)
                printf("Error getting statx of source file: \033[31m%s\033[0m\n", strerror(errno));

        // print the stx mask result
        stx_mask = src_stxBuf.stx_mask;
        printf("stx_mask of \033[34m%s\033[0m: ", src_path);
        bitwisePrint(stx_mask);
        printf("\n\n");

        // get all the times of the source file
        lowest_time.ns_epoch = INT64_MAX;
        fix_ts_index = 0;
        struct ext4_time a_time, m_time, c_time, b_time; // TODO: figure out how to get rid of this line
        fillInTime(&a_time, "aTime        (Access)", src_stxBuf.stx_atime, STATX_ATIME);
        fillInTime(&m_time, "mTime        (Modify)", src_stxBuf.stx_mtime, STATX_MTIME);
        fillInTime(&c_time, "cTime        (Change)", src_stxBuf.stx_ctime, STATX_CTIME);
        fillInTime(&b_time, "bTime/crTime (Birth )", src_stxBuf.stx_btime, STATX_BTIME);

        // fix missing timestamps
        for (size_t i = 0; i < fix_ts_index; i++) {
                struct ext4_time *timestamp_to_fix = timestamps_to_fix[i];
                printf("\033[32m");
                timestamp_to_fix->statx_time = lowest_time.statx_time;
                timestamp_to_fix->epoch = lowest_time.epoch;
                timestamp_to_fix->extra = lowest_time.extra;
                timestamp_to_fix->ns_epoch = lowest_time.ns_epoch;
                printExt4Time(timestamp_to_fix);
                printf("\033[0m\n");
        }

        // put the source stats into the dest file
        struct statx dst_stx;
        result = statx(AT_FDCWD,
                       dst_path,
                       AT_EMPTY_PATH | AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW | AT_STATX_FORCE_SYNC,
                       STATX_BASIC_STATS | STATX_BTIME | STATX_MNT_ID | STATX_DIOALIGN | STATX_MNT_ID_UNIQUE,
                       &dst_stx);
        if (result == -1)
                printf("Error getting statx of source file: \033[31m%s\033[0m\n", strerror(errno));

        printf("%lld %u | ", dst_stx.stx_btime.tv_sec, dst_stx.stx_btime.tv_nsec);
        dst_stx.stx_btime = b_time.statx_time;
        printf("%lld %u\n", dst_stx.stx_btime.tv_sec, dst_stx.stx_btime.tv_nsec);

        return 0;
}
