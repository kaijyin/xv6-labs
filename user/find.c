#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
#include "kernel/fs.h"

char *
fmtname(char *path)
{
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if (strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    buf[strlen(p)]=0;
    return buf;
}

int contain(const char *flname,const char *regix)
{
    int n1 = strlen(flname);
    int n2 = strlen(regix);
    for (int i = 0; i + n2 <= n1; i++)
    {
        int j;
        for (j = 0; j < n2; j++)
        {
            if (flname[i] != regix[j])
            {
                break;
            }
        }
        if (j == n2)
        {
            return 1;
        }
    }
    return -1;
}
void find(char *path, char *regix)
{
    int fd;
    char *p;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0)
    {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0)
    {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    switch (st.type)
    {
    case T_FILE:
        if (strcmp(fmtname(path), regix)==0)
        {
            printf("%s\n",path);
        }
        break;
    case T_DIR:
        p = path+strlen(path);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0||contain(de.name,".")>0)
                continue;
            memcpy(p, de.name,strlen(de.name));
            p[strlen(de.name)] = 0;
            find(path, regix);
        }
        break;
    }
    close(fd);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(2, "invalid args\n");
        exit(1);
    }
    char path[MAXPATH];
    strcpy(path,argv[1]);
    find(path, argv[2]);
    exit(0);
}
