// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct spinlock ref_lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kmem.ref_lock,"kmem_ref");
  freerange(end, (void*)PHYSTOP);
}

uint8 refnums[(PHYSTOP-KERNBASE)/PGSIZE];

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  int pos=((uint64)pa-KERNBASE)/PGSIZE;
  acquire(&kmem.ref_lock);
  if(refnums[pos]>=1)refnums[pos]--;
  int num=refnums[pos];
  release(&kmem.ref_lock);
  acquire(&kmem.lock);
  if(num==0){
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    r = (struct run*)pa;
    r->next = kmem.freelist;
    kmem.freelist = r;
  } 
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    add_refnum((uint64)r);//刚拿出来就不需要加锁
  }
  // add_refnum((uint64)r);//刚拿出来就不需要加锁
    //要r存在才设,究极细节,没有页面则不能+1,因为这样+1,可能是另外一个有效地址的页,然而人家并没有分配,就错了!
  release(&kmem.lock);
  if(r)memset((char*)r, 5, PGSIZE);// fill with junk
  return (void*)r;
}

uint64 freemem(void){
  uint64 mem=0;
  struct run *r;
  acquire(&kmem.lock);
  r = kmem.freelist;
  while(r){
    r = r->next;
    mem+=PGSIZE;
  }
  release(&kmem.lock);
  return mem;
}
void lock_kalloc(){
   acquire(&kmem.ref_lock);
}
void unlock_kalloc(){
  release(&kmem.ref_lock);
}
uint8 refnum(uint64 pa){
  return refnums[(pa-KERNBASE)/PGSIZE];
}
void add_refnum(uint64 pa){
   refnums[(pa-KERNBASE)/PGSIZE]++;
}
void deal_refnum(uint64 pa){
   refnums[(pa-KERNBASE)/PGSIZE]--;
}