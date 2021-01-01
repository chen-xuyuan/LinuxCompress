#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#define REGULAR      0
#define NORMAL      '0'
#define HARDLINK    '1'
#define SYMLINK     '2'
#define CHAR        '3'
#define BLOCK       '4'
#define DIRECTORY   '5'
#define FIFO        '6'
#define LONGNAME    'L'
#define LINKLONG    'K'

typedef union record
{
    union
    {
        // Pre-POSIX.1-1988 format
        struct
        {
            char name[100];      // file name
            char mode[8];        // permissions
            char uid[8];         // user id (octal)
            char gid[8];         // group id (octal)
            char size[12];       // size (octal)
            char mtime[12];      // modefication time (octal)
            char check[8];       // sum of unsigned characters in block (octal)
            char link;           // link indicator
            char link_name[100]; // name of linked file
        };

        // UStar format (POSIX IEEE P1003.1)
        struct
        {
            char old[156];            // first 156 octets of Pre-POSIX.1-1998 format
            char type;                // file type
            char also_link_name[100]; // name of linked file
            char ustar[8];            // ustar\000
            char owner[32];           // user name (string)
            char group[32];           // group name (string)
            char major[8];            // device major number
            char minor[8];            // device minor number
            char prefix[155];
        };
    };

    char block[512]; // raw memory (padded to 1 block)
}Record;

typedef struct node
{
    u_int64_t offset;
    u_int64_t size;
    struct node *child,*brother;
} Node;

long long int Frequency[256] = { 0 };

char* mallocAndReset(size_t length, int n)
{
    char* p = (char*)malloc(length);
    if (!p)
    {
        perror("malloc error");
        exit(1);
    }
    memset(p, n, length);
    return p;
}

void copyNByte(char* dest, char* src, int n)
{
    for (int i = 0; i < n; i++) dest[i] = src[i];
}

void copySrcName(char* path, Record* block)
{
    copyNByte(block->name, path, strlen(path) < 100 ? strlen(path) : 100);
}

void copyLinkName(char* path, Record* block)
{
    copyNByte(block->link_name, path, strlen(path) < 100 ? strlen(path) : 100);
}

char* numberToNChar(long number, int n)
{
    char* temp = (char*)mallocAndReset(n, 0);
    int i = n - 2;
    while (i >= 0)
    {
        temp[i] = '0' + (number & 0x7);
        number = number >> 3;
        i--;
    }
    return temp;
}

void printOneBlock(Record* block, FILE* fout)
{
    char* p = (char*)block;
    for (int i = 0; i < 512; i++)
    {
        Frequency[p[i]]++;
        fprintf(fout, "%c", p[i]);
    }
}

int calculateCheckSum(Record* block)
{
    unsigned char* content = (unsigned char*)block;
    unsigned int sum = 0;
    for (int i = 0; i < 512; i++) sum += content[i];
    return sum;
}

int tarLongName(char* path, FILE* fout, char tarType)
{
    int blockNumber = 1 + (strlen(path) + 511) / 512;
    Record* block = (Record*)mallocAndReset(blockNumber * 512, 0);

    copySrcName("././@LongLink", block); // LongName lable
    copyNByte(block->mode, "0000644", 8);
    copyNByte(block->uid, "0000000", 8);
    copyNByte(block->gid, "0000000", 8);

    char* tarSize = numberToNChar(strlen(path) + 1, 12);
    copyNByte(block->size, tarSize, 12);
    free(tarSize);

    copyNByte(block->mtime, "00000000000", 12);
    copyNByte(block->check, "\x20\x20\x20\x20\x20\x20\x20\x20", 8);
    block->type = tarType;
    copyNByte(block->ustar, "ustar  ", 8);
    copyNByte(block->owner, "root", 5);
    copyNByte(block->group, "root", 5);

    int checkSum = calculateCheckSum(block);
    char* checkSumChar = numberToNChar(checkSum, 7);
    copyNByte(block->check, checkSumChar, 7);
    free(checkSumChar);

    copyNByte((char*)(block + 1), path, strlen(path));

    for (int i = 0; i < blockNumber; i++) printOneBlock(block + i, fout);

    free(block);
    return 0;
}

int tar(char* path, FILE* fout)
{
    struct stat statBuf;
    if (lstat(path, &statBuf))
    {
        printf("%s", path);
        perror(" stat error");
        return 1;
    }

    statBuf.st_ino;

    Record* block = (Record*)mallocAndReset(512, 0);

    copyNByte(block->mode, "0000000", 8);

    block->mode[3] = ((007000 & statBuf.st_mode) >> 9) + '0';
    block->mode[4] = ((000700 & statBuf.st_mode) >> 6) + '0';
    block->mode[5] = ((000070 & statBuf.st_mode) >> 3) + '0';
    block->mode[6] = (000007 & statBuf.st_mode) + '0';

    char* tarUID = numberToNChar(statBuf.st_uid, 8);
    char* tarGID = numberToNChar(statBuf.st_gid, 8);
    copyNByte(block->uid, tarUID, 8);
    copyNByte(block->gid, tarGID, 8);
    free(tarUID);
    free(tarGID);

    char* tarMTime = numberToNChar(statBuf.st_mtime, 12);
    copyNByte(block->mtime, tarMTime, 12);
    free(tarMTime);

    copyNByte(block->check, "\x20\x20\x20\x20\x20\x20\x20\x20", 8);

    if (S_ISREG(statBuf.st_mode)) block->type = NORMAL;
    else if (S_ISDIR(statBuf.st_mode)) block->type = DIRECTORY;
    else if (S_ISCHR(statBuf.st_mode)) block->type = CHAR;
    else if (S_ISBLK(statBuf.st_mode)) block->type = BLOCK;
    else if (S_ISLNK(statBuf.st_mode)) block->type = SYMLINK;
    else if (S_ISFIFO(statBuf.st_mode)) block->type = FIFO;

    copyNByte(block->ustar, "ustar  ", 8);

    struct passwd* userInfo;
    userInfo = getpwuid(statBuf.st_uid);
    copyNByte(block->owner, userInfo->pw_name, strlen(userInfo->pw_name));

    struct group* groupInfo;
    groupInfo = getgrgid(statBuf.st_gid);
    copyNByte(block->group, groupInfo->gr_name, strlen(groupInfo->gr_name));

    if (S_ISDIR(statBuf.st_mode))
    {
        if (strcmp("/",path))
        {
            char *dirPath = (char*)mallocAndReset(strlen(path) + 2, 0);
            if (path[0] == '/') strcat(dirPath, path + 1);
            else strcat(dirPath, path);
            strcat(dirPath, "/");

            if (strlen(dirPath) > 100)
            {
                tarLongName(dirPath, fout, LONGNAME);
            }

            copySrcName(dirPath, block);

            char* tarSize = numberToNChar(0, 12);
            copyNByte(block->size, tarSize, 12);
            free(tarSize);

            int checkSum = calculateCheckSum(block);
            char* checkSumChar = numberToNChar(checkSum, 7);
            copyNByte(block->check, checkSumChar, 7);
            free(checkSumChar);

            printOneBlock(block, fout);

            free(dirPath);
        }

        DIR* dirPoint = opendir(path);
        if (!dirPoint)
        {
            printf("%s", path);
            perror(" open directory error");
            return 1;
        }
        struct dirent* dirSata;
        while (dirSata = readdir(dirPoint))
        {
            if (!strcmp(".", dirSata->d_name) || !strcmp("..", dirSata->d_name)) continue;
            char* nextPath = (char*)mallocAndReset(strlen(path) + strlen(dirSata->d_name) + 2, 0);
            strcat(nextPath, path);
            if (strcmp("/", path)) strcat(nextPath, "/"); // if dir is "/" don't add /
            strcat(nextPath, dirSata->d_name);
            tar(nextPath, fout);
            free(nextPath);
        }
        closedir(dirPoint);
    }
    else
    {
        char* tarSize;
        if (S_ISLNK(statBuf.st_mode))
        {
            char* linkPath = (char*)mallocAndReset(5000, 0);
            if (-1 == readlink(path, linkPath, 5000))
            {
                printf("%s", path);
                perror(" readlink error");
                return 1;
            }
            if (strlen(linkPath) > 100) tarLongName(linkPath, fout, LINKLONG);
            copyLinkName(linkPath, block);
            free(linkPath);
            tarSize = numberToNChar(0, 12);
        }
        else
        {
            tarSize = numberToNChar(statBuf.st_size, 12);
        }

        copyNByte(block->size, tarSize, 12);
        free(tarSize);

        if (path[0] == '/')
        {
            if (strlen(path+1) > 100) tarLongName(path + 1, fout, LONGNAME);
            copySrcName(path + 1, block);
        }
        else
        {
            if (strlen(path) > 100) tarLongName(path, fout, LONGNAME);
            copySrcName(path, block);
        }
        
        int checkSum = calculateCheckSum(block);
        char* checkSumChar = numberToNChar(checkSum, 7);
        copyNByte(block->check, checkSumChar, 7);
        free(checkSumChar);

        printOneBlock(block, fout);

        if (!(S_ISLNK(statBuf.st_mode)))
        {
            int blockNumber = (statBuf.st_size + 511) / 512;

            FILE* fin = fopen(path, "rb");

            if (!fin)
            {
                perror("fopen");
                return 1;
            }

            char* content = (char*)block;
            int ch;

            for (int i = 0; i < blockNumber; i++)
            {
                memset(block, 0, 512);
                for (int i = 0; i < 512; i++)
                {
                    if ((ch = fgetc(fin)) != EOF) content[i] = ch;
                    else break;
                }
                printOneBlock(block, fout);
            }
        }
    }

    free(block);
    return 0;
}

int untar()
{
    return 0;
}

int huffman()
{
    return 0;
}

int compress()
{
    return 0;
}

int uncompress()
{
    return 0;
}

int main()
{
    char path[] = "/home/ricksanchez/test";
    char tarPath[] = "/home/ricksanchez/tarTest/test.tar";

    if (path[strlen(path) - 1] == '/' && strlen(path) > 1) path[strlen(path) - 1] = '\0'; // if path end of '/' and path is not "/" or "."

    FILE* fout = fopen(tarPath, "wb");
    if (!fout)
    {
        perror("fopen");
        return 1;
    }

    tar(path, fout);

    Record* lastRecord = (Record*)mallocAndReset(512, 0);
    for (int i = 0; i < 2;i++) printOneBlock(lastRecord, fout);
    free(lastRecord);

    fclose(fout);

    return 0;
}