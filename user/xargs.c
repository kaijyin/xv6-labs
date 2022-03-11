#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    char arvgc[MAXARG][24];
    int argn = argc - 1;
    for (int i = 0; i < argn; i++){
        memcpy(arvgc[i], argv[i+1], strlen(argv[i + 1]));
    }
    char *p = arvgc[argn];
    int now=argn;
    while (read(0, p, 1) == 1)
    {
        if (*p == '\n')
        {
            *p=0;
            char* args[MAXARG];
            for(int i=0;i<=now;i++){
                args[i]=arvgc[i];
            }
            args[now+1]=0;
            if (fork() == 0 && exec(arvgc[0],args) < 0)
            {
                fprintf(2, "exe:%s fail", arvgc[0]);
                exit(1);
            }
            now=argn;
            p=arvgc[argn];
            continue;
        }else if(*p==' '){
            *p=0;
            now++;
            p=arvgc[now];
            continue;
        }
        p++;
    }
    int son;
    while(wait(&son)!=-1);
    exit(0);
}
