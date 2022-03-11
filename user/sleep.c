#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


const char*
getline(int fd){
   static char buf[512];
   char*p=buf;
   while(read(fd,p,1)==1&&*p!='\n')p++;
   return buf;
}
int
main(int argc, char *argv[])
{
  if(argc<=1){
      fprintf(2, "invalid args\n");
      exit(1);
  }
  int time=atoi(argv[1]);
  printf("(nothing happens for a little while)\n");
  sleep(time);
  exit(0);
}
