#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <utime.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/kdev_t.h>

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

typedef struct inode
{
    u_int64_t inode;
    char* path;
    struct inode* next;
} iNode;

typedef struct huffmannode
{
    int ch;
    struct huffmannode* left;
    struct huffmannode* right;
} huffmanNode;

typedef struct linknode
{
    u_int64_t frequency;
    huffmanNode* node;
    struct linknode* next;
} linkNode;

typedef struct huffmanitem
{
    unsigned char length;
    char* huffmanCode;
} huffmanItem;

iNode iNodeHead;

linkNode linkNodeHead;

u_int64_t Frequency[256] = { 0 };

huffmanItem huffmanTable[256];

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

int createDir(char* path)
{
    int pathLength = strlen(path);
    char* temp = (char*)mallocAndReset(pathLength + 1, 0);
    strcat(temp, path);
    for (int i = 0; i < pathLength; i++)
    {
        if (temp[i] == '/')
        {
            temp[i] = '\0';
            if (access(temp, F_OK))
            {
                if (mkdir(temp, 0777))
                {
                    perror("mkdir error");
                    return 1;
                }
            }
            temp[i] = '/';
        }
    }
    return 0;
}

void copyNByte(char* dest, char* src, int n)
{
    for (int i = 0; i < n; i++) dest[i] = src[i];
}

char* findAndAddINode(u_int64_t inode, char* path)
{
    iNode* p = &iNodeHead;
    for (u_int64_t i = 0; i < iNodeHead.inode; i++)
    {
        p = p->next;
        if (p->inode == inode) return p->path;
    }
    iNode* temp = (iNode*)mallocAndReset(sizeof(iNode), 0);
    temp->inode = inode;
    temp->path = (char*)mallocAndReset(strlen(path) + 1, 0);
    copyNByte(temp->path, path, strlen(path));
    p->next = temp;
    iNodeHead.inode++;
    return NULL;
}

void freeINode()
{
    for (u_int64_t i = 0; i < iNodeHead.inode; i++)
    {
        iNode* temp = iNodeHead.next;
        iNodeHead.next = temp->next;
        free(temp->path);
        free(temp);
    }
    iNodeHead.inode = 0;
}

int addLinkNode(linkNode* tempNode)
{
    linkNode* pre = &linkNodeHead;
    linkNode* p = pre->next;
    for (int i = 0; i < linkNodeHead.frequency; i++)
    {
        if (tempNode->frequency < p->frequency)
        {
            pre->next = tempNode;
            tempNode->next = p;
            linkNodeHead.frequency++;
            return 0;
        }
        pre = pre->next;
        p = pre->next;
    }
    tempNode->next = p;
    pre->next = tempNode;
    linkNodeHead.frequency++;
    return 0;
}

int generateHuffmanCode(huffmanNode* huffmanTree, unsigned char length, char* code)
{
    if (huffmanTree->ch != -1)
    {
        huffmanTable[huffmanTree->ch].huffmanCode = code;
        huffmanTable[huffmanTree->ch].length = length;
        return 0;
    }
    char* leftHuffmanCode = (char*)mallocAndReset(strlen(code) + 2, 0);
    char* rightHuffmanCode = (char*)mallocAndReset(strlen(code) + 2, 0);
    strcat(leftHuffmanCode, code);
    strcat(leftHuffmanCode, "0");
    strcat(rightHuffmanCode, code);
    strcat(rightHuffmanCode, "1");
    if (generateHuffmanCode(huffmanTree->left, length + 1, leftHuffmanCode)) free(leftHuffmanCode);
    if (generateHuffmanCode(huffmanTree->right, length + 1, rightHuffmanCode)) free(rightHuffmanCode);
    return 1;
}

void copySrcName(char* path, Record* block)
{
    copyNByte(block->name, path, strlen(path) < 100 ? strlen(path) : 100);
}

void copyLinkName(char* path, Record* block)
{
    copyNByte(block->link_name, path, strlen(path) < 100 ? strlen(path) : 100);
}

char* numberToNChar(u_int64_t number, int n)
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

u_int64_t charToNumber(char* octalString)
{
    u_int64_t temp = 0;
    int length = strlen(octalString);
    for (int i = 0; i < length; i++) temp = temp * 8 + (octalString[i] - '0');
    return temp;
}

int calculateCheckSum(Record* block)
{
    unsigned char* content = (unsigned char*)block;
    unsigned int sum = 0;
    for (int i = 0; i < 512; i++) sum += content[i];
    return sum;
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

Record* readOneBlock(FILE* fin)
{
    Record* block = (Record*)mallocAndReset(512, 0);
    int ch;
    for (int i = 0; i < 512; i++)
    {
        if ((ch = fgetc(fin)) != EOF) ((char*)block)[i] = ch;
        else
        {
            perror("readOneBlock error");
            return 0;
        }
    }
    return block;
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

    if (S_ISCHR(statBuf.st_mode) || S_ISBLK(statBuf.st_mode))
    {
        char* major = numberToNChar(MAJOR(statBuf.st_rdev), 8);
        char* minor = numberToNChar(MINOR(statBuf.st_rdev), 8);
        copyNByte(block->major, major, 8);
        copyNByte(block->minor, minor, 8);
    }

    copyNByte(block->ustar, "ustar  ", 8);

    struct passwd* userInfo = NULL;
    userInfo = getpwuid(statBuf.st_uid);
    copyNByte(block->owner, userInfo->pw_name, strlen(userInfo->pw_name));

    struct group* groupInfo = NULL;
    groupInfo = getgrgid(statBuf.st_gid);
    copyNByte(block->group, groupInfo->gr_name, strlen(groupInfo->gr_name));

    if (S_ISDIR(statBuf.st_mode))
    {
        if (strcmp("/", path))
        {
            char* dirPath = (char*)mallocAndReset(strlen(path) + 2, 0);
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
        char* tarSize = NULL;
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

        char* hardLinkPath = NULL;
        if (statBuf.st_nlink > 1)
        {
            if (path[0] == '/') hardLinkPath = findAndAddINode(statBuf.st_ino, path + 1);
            else hardLinkPath = findAndAddINode(statBuf.st_ino, path);
            if (hardLinkPath)
            {
                block->type = HARDLINK;
                if (strlen(hardLinkPath) > 100) tarLongName(hardLinkPath, fout, LINKLONG);
                copyLinkName(hardLinkPath, block);
                tarSize = numberToNChar(0, 12);
            }
        }

        copyNByte(block->size, tarSize, 12);
        free(tarSize);

        if (path[0] == '/')
        {
            if (strlen(path + 1) > 100) tarLongName(path + 1, fout, LONGNAME);
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

        if (S_ISREG(statBuf.st_mode) && !hardLinkPath)
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

            fclose(fin);
        }
    }

    free(block);
    return 0;
}

int untar(FILE* fin)
{
    while (1)
    {
        Record* tarHead = readOneBlock(fin);
        char* linkPath = NULL;
        char* srcPath = NULL;

        if (tarHead->name[0] == '\0') return 0;

        if (tarHead->type == LINKLONG)
        {
            if (strcmp("././@LongLink", tarHead->name))
            {
                printf("tarHead linkName error\n");
                free(tarHead);
                return 1;
            }
            u_int64_t linkNameSize = charToNumber(tarHead->size);
            int linkNameBlock = (linkNameSize + 511) / 512;
            linkPath = (char*)mallocAndReset(linkNameSize, 0);
            int ch;
            for (int i = 0; i < 512 * linkNameBlock; i++)
            {
                if ((ch = fgetc(fin)) != EOF)
                {
                    if (i < linkNameSize) linkPath[i] = ch;
                }
                else
                {
                    perror("untar-linkName");
                    free(linkPath);
                    free(tarHead);
                    return 1;
                }
            }
            free(tarHead);
            tarHead = readOneBlock(fin);
        }

        if (tarHead->type == LONGNAME)
        {
            if (strcmp("././@LongLink", tarHead->name))
            {
                printf("tarHead linkName error\n");
                free(tarHead);
                return 1;
            }
            u_int64_t srcNameSize = charToNumber(tarHead->size);
            int srcNameBlock = (srcNameSize + 511) / 512;
            srcPath = (char*)mallocAndReset(srcNameSize, 0);
            int ch;
            for (int i = 0; i < 512 * srcNameBlock; i++)
            {
                if ((ch = fgetc(fin)) != EOF)
                {
                    if (i < srcNameSize) srcPath[i] = ch;
                }
                else
                {
                    perror("untar-srcName");
                    free(srcPath);
                    free(tarHead);
                    return 1;
                }
            }
            free(tarHead);
            tarHead = readOneBlock(fin);
        }

        if (srcPath)
        {
            if (strncmp(tarHead->name, srcPath, 100))
            {
                printf("srcName bupipei\n");
                free(srcPath);
                if (linkPath) free(linkPath);
                free(tarHead);
                return 1;
            }
        }
        else
        {
            srcPath = (char*)mallocAndReset(101, 0);
            copyNByte(srcPath, tarHead->name, 100);
        }

        if (linkPath)
        {
            if (strncmp(tarHead->link_name, linkPath, 100))
            {
                printf("linkName bupipei\n");
                free(linkPath);
                free(srcPath);
                free(tarHead);
                return 1;
            }
        }
        else
        {
            linkPath = (char*)mallocAndReset(101, 0);
            copyNByte(linkPath, tarHead->link_name, 100);
        }

        if (tarHead->type == DIRECTORY)
        {
            if (access(srcPath, F_OK)) createDir(srcPath);
            continue;
        }

        createDir(srcPath);
        remove(srcPath);

        if (tarHead->type == SYMLINK)
        {
            if (symlink(linkPath, srcPath))
            {
                printf("%s\n", srcPath);
                perror("symLink error");
                free(linkPath);
                free(srcPath);
                free(tarHead);
                return 1;
            }
            continue;
        }

        if (tarHead->type == HARDLINK)
        {
            if (link(linkPath, srcPath))
            {
                perror("hardLink error");
                free(linkPath);
                free(srcPath);
                free(tarHead);
                return 1;
            }
            continue;
        }

        mode_t fileMode = (((tarHead->mode[3] - '0') * 8 + (tarHead->mode[4] - '0')) * 8 + (tarHead->mode[5] - '0')) * 8 + (tarHead->mode[6] - '0');

        if (tarHead->type == FIFO)
        {
            if (mkfifo(srcPath, fileMode))
            {
                perror("mkfifo error");
                if (linkPath) free(linkPath);
                free(srcPath);
                free(tarHead);
                return 1;
            }
            continue;
        }

        if (tarHead->type == BLOCK || tarHead->type == CHAR)
        {
            int  major = charToNumber(tarHead->major);
            int minor = charToNumber(tarHead->minor);
            if (mknod(srcPath, fileMode, MKDEV(major, minor)))
            {
                perror("mknod error");
                if (linkPath) free(linkPath);
                free(srcPath);
                free(tarHead);
                return 1;
            }
            continue;
        }

        FILE* fout = fopen(srcPath, "wb");
        if (!fout)
        {
            perror("open file error");
            if (linkPath) free(linkPath);
            free(srcPath);
            free(tarHead);
            return 1;
        }
        u_int64_t fileSize = charToNumber(tarHead->size);
        int fileBlock = (fileSize + 511) / 512;

        for (u_int64_t i = 0; i < 512 * fileBlock; i++)
        {
            int ch;
            if ((ch = fgetc(fin)) != EOF)
            {
                if (i < fileSize) fprintf(fout, "%c", ch);
            }
            else
            {
                perror("tar file shunhuai");
                free(linkPath);
                free(srcPath);
                free(tarHead);
                return 1;
            }

        }

        fclose(fout);

        chmod(srcPath, fileMode);

        u_int64_t uid = charToNumber(tarHead->uid);
        u_int64_t gid = charToNumber(tarHead->gid);

        chown(srcPath, uid, gid);

        u_int64_t mTime = charToNumber(tarHead->mtime);
        struct utimbuf time;
        time.actime = mTime;
        time.modtime = mTime;

        utime(srcPath, &time);

    }
    return 0;
}

int huffman()
{
    for (int i = 0; i < 256; i++)
    {
        if (Frequency[i])
        {
            huffmanNode* tempHuffmanNode = (huffmanNode*)mallocAndReset(sizeof(huffmanNode), 0);
            tempHuffmanNode->ch = i;
            linkNode* tempLinkNode = (linkNode*)mallocAndReset(sizeof(linkNode), 0);
            tempLinkNode->frequency = Frequency[i];
            tempLinkNode->node = tempHuffmanNode;
            addLinkNode(tempLinkNode);
        }
    }

    while (linkNodeHead.frequency > 1)
    {
        linkNode* left = linkNodeHead.next;
        linkNode* right = left->next;
        linkNodeHead.next = right->next;
        huffmanNode* tempHuffmanNode = (huffmanNode*)mallocAndReset(sizeof(huffmanNode), 0);
        tempHuffmanNode->ch = -1;
        tempHuffmanNode->left = left->node;
        tempHuffmanNode->right = right->node;
        linkNode* tempLinkNode = (linkNode*)mallocAndReset(sizeof(linkNode), 0);
        tempLinkNode->frequency = left->frequency + right->frequency;
        tempLinkNode->node = tempHuffmanNode;
        free(left);
        free(right);
        linkNodeHead.frequency = linkNodeHead.frequency - 2;
        addLinkNode(tempLinkNode);
    }

    linkNodeHead.node = linkNodeHead.next->node;
    free(linkNodeHead.next);
    linkNodeHead.next = NULL;
    linkNodeHead.frequency = 0;

    return 0;
}

int freeHuffman(huffmanNode* node)
{
    if (node)
    {
        freeHuffman(node->left);
        freeHuffman(node->right);
        free(node);
    }
    return 0;
}

int compress(FILE* fin, FILE* fout)
{
    huffman();

    char* huffmanCode = (char*)mallocAndReset(1, 0);
    if (generateHuffmanCode(linkNodeHead.node, 0, huffmanCode)) free(huffmanCode);

    fprintf(fout, "%c", '\0');
    for (int i = 0; i < 256; i++)
    {
        char* temp = (char*)&(Frequency[i]);
        for (int j = 0; j < 8; j++) fprintf(fout, "%c", temp[j]);
    }
    int ch;
    char countLength = 0;
    unsigned char compressCode = 0;
    while ((ch = fgetc(fin)) != EOF)
    {
        for (int i = 0; i < huffmanTable[ch].length; i++)
        {
            if (huffmanTable[ch].huffmanCode[i] == '0') compressCode = compressCode << 1;
            else compressCode = (compressCode << 1) + 1;
            countLength++;
            if (countLength == 8)
            {
                fprintf(fout, "%c", compressCode);
                countLength = 0;
                compressCode = 0;
            }
        }
    }
    fprintf(fout, "%c", compressCode << (8 - countLength));
    fseek(fout, 0, SEEK_SET);
    fprintf(fout, "%c", countLength);

    freeHuffman(linkNodeHead.node);
    linkNodeHead.node = NULL;

    return 0;
}

int uncompress(FILE* fin, FILE* fout)
{
    int lastLength = fgetc(fin);
    int ch;
    for (int i = 0; i < 256; i++)
    {
        char* temp = (char*)&(Frequency[i]);
        for (int j = 0; j < 8; j++)
        {
            if ((ch = fgetc(fin)) != EOF) temp[j] = ch;
            else
            {
                perror("uncompress");
                return 0;
            }

        }
    }

    huffman();

    char temp;
    if ((ch = fgetc(fin)) != EOF) temp = ch;
    else
    {
        perror("uncompress read");
        return 0;
    }
    huffmanNode* p = linkNodeHead.node;
    while ((ch = fgetc(fin)) != EOF)
    {
        int length = 8;
        while (length)
        {
            if (p->ch == -1)
            {
                if (temp & 0x80) p = p->right;
                else p = p->left;
                length--;
                temp = temp << 1;
            }
            else
            {
                fprintf(fout, "%c", p->ch);
                p = linkNodeHead.node;
            }
        }
        temp = ch;
    }
    while (lastLength > -1)
    {
        if (p->ch == -1)
        {
            if (temp & 0x80) p = p->right;
            else p = p->left;
            lastLength--;
            temp = temp << 1;
        }
        else
        {
            fprintf(fout, "%c", p->ch);
            p = linkNodeHead.node;
        }
    }

    freeHuffman(linkNodeHead.node);
    linkNodeHead.node = NULL;

    return 0;
}

int main()
{
    memset(&iNodeHead, 0, sizeof(iNode));
    memset(&linkNodeHead, 0, sizeof(linkNodeHead));

    char path[] = "/home/ricksanchez/test";
    char tarPath[] = "/home/ricksanchez/tarTest/test.tar";
    char untarPath[] = "/home/ricksanchez/tarTest/test.tar";
    char compressPath[] = "/home/ricksanchez/tarTest/test.tar.hf";
    char uncompressPath[] = "/home/ricksanchez/tarTest/unhftest.tar";

    if (path[strlen(path) - 1] == '/' && strlen(path) > 1) path[strlen(path) - 1] = '\0'; // if path end of '/' and path is not "/" or "."

    FILE* fout = fopen(tarPath, "wb");
    if (!fout)
    {
        perror("fopen");
        return 1;
    }

    tar(path, fout);

    Record* lastRecord = (Record*)mallocAndReset(512, 0);
    for (int i = 0; i < 2; i++) printOneBlock(lastRecord, fout);
    free(lastRecord);

    fclose(fout);

    freeINode();

    FILE* untarFin = fopen(untarPath, "rb");
    untar(untarFin);
    fclose(untarFin);

    FILE* compressFin = fopen(tarPath, "rb");
    FILE* compressFout = fopen(compressPath, "wb");

    compress(compressFin, compressFout);

    fclose(compressFin);
    fclose(compressFout);

    FILE* uncompressFin = fopen(compressPath, "rb");
    FILE* uncompressFout = fopen(uncompressPath, "wb");

    uncompress(uncompressFin, uncompressFout);

    fclose(uncompressFin);
    fclose(uncompressFout);

    return 0;
}
