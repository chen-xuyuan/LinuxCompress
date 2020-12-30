#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

char *mallocAndReset(size_t length)
{
    char *p = (char*)malloc(length);
    if (!p)
    {
        perror("malloc error");
        exit(1);
    }
    memset(p,0,length);
    return p;
}

int tar(char *path)
{
    struct stat statBuf;
    if (stat(path,&statBuf))
    {
        perror("stat error");
        return 1;
    }
    if (S_ISDIR(statBuf.st_mode))
    {
        DIR * dirPoint = opendir(path);
        if (!dirPoint)
        {
            perror("open directory error");
            return 1;
        }
        struct dirent *dirSata;
        while(dirSata = readdir(dirPoint))
        {
            if (!strcmp(".",dirSata->d_name) || !strcmp("..",dirSata->d_name)) continue;
            char *nextPath = (char *)mallocAndReset(strlen(path) + strlen(dirSata->d_name) + 2);
            strcat(nextPath,path);
            if (strcmp("/",path)) strcat(nextPath,"/"); // if dir is "/" don't add /
            strcat(nextPath,dirSata->d_name);
            tar(nextPath);
            free(nextPath);
        }
        printf("%s/\n",path);
        closedir(dirPoint);
    }
    else
    {
        printf("%s\n",path);
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
    if (path[strlen(path)-1] == '/' && strlen(path) > 1) path[strlen(path)-1] = '\0'; // if path end of '/' and path is not "/" or "."
    tar(path);
    return 0;
}