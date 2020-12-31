#include <stdio.h>
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
#define SYMLINKLONG 'K'

typedef union Record
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

int Frequency[256] = {0};

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

void countFrequency(Record *block)
{
    for (int i = 0;i<512;i++) Frequency[((char *)block)[i]]++;
}

void copyNByte(char *dest, char *src,int n)
{
    for (int i=0;i<n;i++) dest[i] = src[i];
}

void copyName(char *path,Record *block)
{
    copyNByte((char *)block,path,strlen(path) < 100 ? strlen(path) : 100);
}

char *numberToNChar(long number,int n)
{
    char *temp = (char *)mallocAndReset(n);
    int i = n-2;
    while(i>=0)
    {
        temp[i] = '0' + (number & 0x7);
        number = number >> 3;
        i--;
    }
    return temp;
}

int tarLongName(char *path,FILE *fout)
{
    Record *block = (Record *)mallocAndReset(((strlen(path)+511)/512 + 1) * 512);
    copyName("././@LongLink",block); // LongName lable
    return 0;
}

int tar(char *path,FILE *fout)
{
    struct stat statBuf;
    if (stat(path,&statBuf))
    {
        perror("stat error");
        return 1;
    }
    if (S_ISDIR(statBuf.st_mode))
    {
        if (strlen(path) + 2 > 100)
        {
            char *dirPath = (char *)malloc(strlen(path)+2);
            strcat(dirPath,path);
            strcat(dirPath,"/");
            tarLongName(dirPath,fout);
        }
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
            tar(nextPath,fout);
            free(nextPath);
        }
        printf("%s/\n",path);
        closedir(dirPoint);
    }
    else
    {
        if (strlen(path) + 1 > 100) tarLongName(path,fout);
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
    char tarPath[] = "/home/ricksanchez/test.tar";

    if (path[strlen(path)-1] == '/' && strlen(path) > 1) path[strlen(path)-1] = '\0'; // if path end of '/' and path is not "/" or "."

    FILE *fout = fopen(tarPath,"wb");
    if (!fout)
    {
        perror("fopen");
        return 1;
    }

    tar(path,fout);

    fclose(fout);

    char * test = numberToNChar(262,12);
    printf("%s\n",test);
    return 0;
}