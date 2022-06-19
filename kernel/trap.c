#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  // 判断是否是用户态的中断/系统调用,即管理权限标志位SSP为0
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  //虽然设置了内核态页表,但是还是没有标识为内核态,需要发一个中断才正式进入内核态,也就是设置SSP
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){//判断是那种中断,系统调用,还是设备中断(软件/硬件)
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  }else {
    //为page fault定位,为va对应的页面映射物理内存.
    uint64 va=r_stval();
    if((r_scause()==13||r_scause()==15)&&va<p->sz){
     pte_t *pte=walk(p->pagetable,va,0);
     if(pte&&(*pte & PTE_V)&&(*pte & PTE_U)&&(*pte & PTE_COW)){
     uint flags=PTE_FLAGS(*pte);
     flags&=~PTE_COW;
     flags|=PTE_W;
     uint64 pa=PTE2PA(*pte);
     lock_kalloc();//加上全局锁,读取和更新 PTE_W和PTE_COW需要同步
        if(refnum(pa)==1){//如果该物理页只有一个引用,说明就他在使用,加上写标志即可
          *pte=PA2PTE(pa)|flags;
          kvmgrow(p->kpagetable,p->pagetable,PGROUNDDOWN(va),PGROUNDDOWN(va)+PGSIZE);
          unlock_kalloc();
          goto ahead;
        }
        uint64 newpa;
        if((newpa=(uint64)kalloc())!=0){
          deal_refnum(pa);
          *pte=PA2PTE(newpa)|flags;
          kvmgrow(p->kpagetable,p->pagetable,PGROUNDDOWN(va),PGROUNDDOWN(va)+PGSIZE);
          unlock_kalloc();
          memmove((void*)newpa,(void*)pa,PGSIZE);
          goto ahead;
        }
     unlock_kalloc();
    }else if((pte == 0||(*pte & PTE_V) == 0)&&(va>p->stackbase||va<=p->stackbase-PGSIZE)&&(uvmalloc(p->pagetable,PGROUNDDOWN(va),PGROUNDDOWN(va)+PGSIZE)!=0)){
        kvmgrow(p->kpagetable,p->pagetable,PGROUNDDOWN(va),PGROUNDDOWN(va)+PGSIZE);
        goto ahead;
    }}
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

ahead:

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  //如果是时钟中断,在内核态下放弃cpu,等待下次调度,再回到用户态
  if(which_dev == 2){
     p->lasttick++;
     if(p->tick&&p->hander_working!=-1&&p->lasttick%p->tick==0){
          memmove((void*)p->hander_trapframe,(void*)p->trapframe,PGSIZE);
          p->trapframe->epc=p->hander_pc;
          p->hander_working=-1;
     }
     yield();
  }
  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  //关闭中断,避免刚设置当前为用户态中断,就有个中断到了,但是此时我们还没设置好寄存器
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  //设置当前为用户态了,中断的话就调用的是tarpoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);//找到userret函数位置,并调用,回到用户态
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  //检查进入中断前的状态
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer 如果是时钟中断,执行调度
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()//设备中断
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){//外设中断
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){//键盘
      uartintr();
    } else if(irq == VIRTIO0_IRQ){//磁盘
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.清空irq标志位,表示中断已经处理,重新打开该设备的中断请求
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){//定时器中断
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){//只让一个核去做clock++的操作,不会有锁争用
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.清零sip,表示时钟中断已经被处理了
    w_sip(r_sip() & ~2); 

    return 2;
  } else {
    return 0;
  }
}

