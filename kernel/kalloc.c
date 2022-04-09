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
  struct run *freelist;
  int numpage;
} kmem[NCPU];
char name[NCPU][10];
void
kinit()
{
  for(int i=0;i<NCPU;i++){
    snprintf(name[i],10,"mem_%d",i);
    initlock(&kmem[i].lock,name[i]);
    kmem[i].numpage=0;
  }
  freerange(end, (void*)PHYSTOP);
}

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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  int id=cpuid();
  acquire(&kmem[id].lock);
  kmem[id].numpage++;
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  int id=cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r){
     kmem[id].freelist = r->next;
     kmem[id].numpage--;
     release(&kmem[id].lock);
  }else {
     release(&kmem[id].lock);
     int nid=id+1,steal=0;
     struct run*head=0,*tail=0;
     for(int k=1;k<NCPU;k++){//问其他的CPU要页面
         if(nid==NCPU)nid=0;
         acquire(&kmem[nid].lock);
         if(kmem[nid].numpage>0){
            steal=(kmem[nid].numpage+1)/2;
            kmem[nid].numpage-=steal;
            head=tail=kmem[nid].freelist;
            for(int i=1;i<steal;i++){
              tail=tail->next;
            }
            kmem[nid].freelist=tail->next;
            tail->next=0;
            release(&kmem[nid].lock);
            break;
         }
         release(&kmem[nid].lock);
         nid++;
       }
       if(steal){
           acquire(&kmem[id].lock);
           kmem[id].numpage+=steal-1;
           tail->next=kmem[id].freelist;
           kmem[id].freelist = head->next;
           r=head;
           release(&kmem[id].lock);
      }
  }
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
