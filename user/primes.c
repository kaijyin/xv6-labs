#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void deal(int fd)
{
    int me=0;
    read(fd,&me, 4);
    fprintf(1, "prime %d\n",me);
    int cur=0;
    int cur_fds[2] = {0, 0};
    while (read(fd,&cur, 4) != 0){
        if(cur % me == 0)continue;
        if (cur_fds[0] == 0){
            if (pipe(cur_fds) < 0)
            {
                fprintf(2, "create pipe fail\n");
                exit(1);
            }
            if (fork() == 0){
                close(cur_fds[1]);
                deal(cur_fds[0]);
            }
            close(cur_fds[0]);
        }
        write(cur_fds[1], &cur, 4);
    }
    close(fd);
    if (cur_fds[0] != 0)
        close(cur_fds[1]);
    int stat;
    while(wait(&stat)!=-1);
    exit(0);
}
int main(int argc, char *argv[])
{
    int fds[2];
    if (pipe(fds) < 0)
    {
        fprintf(2, "create pipe fail\n");
        exit(1);
    }
    if (fork() == 0){
        close(fds[1]);
        deal(fds[0]);
    }
    close(fds[0]);
    for(int i=2;i<=35;i++){
        write(fds[1],&i,4);
    }
    close(fds[1]);
    int stat;
    while(wait(&stat)!=-1);
    exit(0);
}
