#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>

char *mallocAndReset(size_t length)
{
    char *p = (char*)malloc(length);
    if (!p)
    {
        perror("no enough space to malloc");
        exit(1);
    }
    memset(p,0,length);
    return p;
}

int tar(char *dirFile)
{
    char *dirPath = mallocAndReset(strlen(dirFile) + 2);
    strcat(dirPath,dirFile);
    strcat(dirPath,"/");
    printf("%s\n",dirPath);
    DIR *dirPoint = opendir(dirPath);
    if (!dirPoint)
    {
        perror("open dir error");
        exit(1);
    }
    struct dirent *fileStat;
    while(fileStat = readdir(dirPoint))
    {
        char *filePath = mallocAndReset(strlen(dirPath) + strlen(fileStat->d_name) + 1);
        strcat(filePath,dirPath);
        strcat(filePath,fileStat->d_name);
        if (fileStat->d_type == DT_DIR && strcmp(".",fileStat->d_name) && strcmp("..",fileStat->d_name))
        {
            tar(filePath);
        }
        else
        {
            printf("%s\n",filePath);
        }
        free(filePath);
    }
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
    tar(path);
    return 0;
}