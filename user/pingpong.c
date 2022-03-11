#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
   int fds1[2], fds2[2];
   if (pipe(fds1) < 0 || pipe(fds2) < 0)
   {
      fprintf(2, "create pipe fail\n");
      exit(1);
   }
   if (fork() == 0)
   {
      close(fds1[1]);
      close(fds2[0]);
      char p;
      read(fds1[0], &p, 1);
      fprintf(1, "%d: received ping\n", getpid());
      write(fds2[1], &p, 1);
      exit(0);
   }
   close(fds1[0]);
   close(fds2[1]);
   char p = ' ';
   write(fds1[1], &p, 1);
   read(fds2[0], &p, 1);
   fprintf(1, "%d: received pong\n", getpid());
   exit(0);
}
