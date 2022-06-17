#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"


extern uint64 freemem(void);
extern uint64 getpron(void);
uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int old_sz;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  struct proc*p=myproc();
  old_sz = p->sz;
  if(PGROUNDUP(p->sz+n)>=PLIC){
     return -1;
  }
  p->sz+=n;
  //直接更新sz即可,如果是缩小内存,需要把多余的物理内存映射给取消掉.
  if(n<0){
    kvmgrow(p->kpagetable,p->pagetable,PGROUNDUP(old_sz),PGROUNDUP(p->sz));
    uvmdealloc(p->pagetable,PGROUNDUP(old_sz),PGROUNDDOWN(p->sz));
  }
  return old_sz;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;
  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  backtrace();
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
   uint64 mask;
   if(argaddr(0, &mask) < 0)
    return -1;
   myproc()->traceflg=mask;
   return 0;
}
uint64
sys_sysinfo(void)
{
   struct sysinfo info;
   uint64 addr;
   if(argaddr(0, &addr) < 0)
    return -1;
  struct proc *p=myproc();
  info.freemem=freemem();
  info.nproc=getpron();
  if(copyout(p->pagetable, addr, (char *)&info, sizeof(info)) < 0)
        return -1;
  return 0;
}
int 
sys_sigalarm(void){
   int tick;
   uint64 addr;
   if(argint(0,&tick)<0||argaddr(1,&addr)<0){
       return -1;
   }
   struct proc*p=myproc();
   p->tick=tick;
   p->lasttick=0;
   p->hander_pc=addr;
   return 0;
} 

uint64 sys_sigreturn(void){
   struct proc*p=myproc();
   memmove((void*)p->trapframe,(void*)p->hander_trapframe,PGSIZE);
   p->hander_working=0;
   return 0;
}