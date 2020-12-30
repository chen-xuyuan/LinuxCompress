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

int tar(char *dirPath)
{
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
        char *nextPath = mallocAndReset(strlen(dirPath) + 2 + strlen(fileStat->d_name));
        strcat(nextPath,dirPath);
        strcat(nextPath,fileStat->d_name);
        if (fileStat->d_type == DT_DIR && strcmp(".",fileStat->d_name) && strcmp("..",fileStat->d_name))
        {
            strcat(nextPath,"/");
            tar(nextPath);
        }
        else
        {
            printf("%s\n",nextPath);
        }
        free(nextPath);
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
    char path[] = "/home/ricksanchez/test/";
    tar(path);
    return 0;
}