// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock buk_lock[NBUK];
  struct buf head[NBUK];
  int freenum[NBUK];
  struct buf buf[NBUF];
} bcache;

void
binit(void)
{
  struct buf *b= bcache.buf;

  for(int i=0;i<NBUK;i++){
    char*name="xxxxxxxxxx";
    snprintf(name,10,"bcache_%d",i);
    initlock(&bcache.buk_lock[i],name);

      // Create linked list of buffers
  bcache.head[i].prev = &bcache.head[i];
  bcache.head[i].next = &bcache.head[i];
  for(; b < bcache.buf+(i+1)*(NBUF/NBUK); b++){
    b->next = bcache.head[i].next;
    b->prev = &bcache.head[i];
    initsleeplock(&b->lock, "buffer");
    bcache.head[i].next->prev = b;
    bcache.head[i].next = b;
    bcache.freenum[i]++;
  }
  }
  for(;b<bcache.buf+NBUF;b++){
     b->next = bcache.head[0].next;
     b->prev = &bcache.head[0];
     bcache.head[0].next->prev = b;
     bcache.head[0].next = b;
     bcache.freenum[0]++;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b=0;

  int buk_idx=blockno%NBUK;
  acquire(&bcache.buk_lock[buk_idx]);

  // Is the block already cached?
  for(b = bcache.head[buk_idx].next; b != &bcache.head[buk_idx]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buk_lock[buk_idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buk_lock[buk_idx]);
  int next_buck=buk_idx;
  for(int i=0;i<NBUK;i++,next_buck++){
    if(next_buck==NBUK)next_buck=0;
     acquire(&bcache.buk_lock[next_buck]);
     if(bcache.freenum[next_buck]==0){
       release(&bcache.buk_lock[next_buck]);
       continue;
     }
    int find=0;
    for(b = bcache.head[next_buck].prev; b != &bcache.head[next_buck]; b = b->prev){
       if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->next->prev = b->prev;
      b->prev->next = b->next;
      bcache.freenum[next_buck]--;
      // printf("buck: %d freenum:%d\n",next_buck,bcache.freenum[next_buck]);
      find=1;
      break;
      } 
    }
    release(&bcache.buk_lock[next_buck]);
    if(find){
        acquire(&bcache.buk_lock[buk_idx]);
        b->next = bcache.head[buk_idx].next;
        b->prev = &bcache.head[buk_idx];
        bcache.head[buk_idx].next->prev = b;
        bcache.head[buk_idx].next = b;
        release(&bcache.buk_lock[buk_idx]);
        acquiresleep(&b->lock);
        return b;
    }
    // panic("bget: freenum error");
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int buk_idx=b->blockno%NBUK;
  acquire(&bcache.buk_lock[buk_idx]);
  b->refcnt--;
  if (b->refcnt == 0) {
    bcache.freenum[buk_idx]++;
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[buk_idx].next;
    b->prev = &bcache.head[buk_idx];
    bcache.head[buk_idx].next->prev = b;
    bcache.head[buk_idx].next = b;
  }
  release(&bcache.buk_lock[buk_idx]);
}

void
bpin(struct buf *b) {
  int buk_idx=b->blockno%NBUK;
  acquire(&bcache.buk_lock[buk_idx]);
  if(b->refcnt==0){
    bcache.freenum[buk_idx]--;
  }
  b->refcnt++;
  release(&bcache.buk_lock[buk_idx]);
}

void
bunpin(struct buf *b) {
  int buk_idx=b->blockno%NBUK;
  acquire(&bcache.buk_lock[buk_idx]);
  b->refcnt--;
  if(b->refcnt==0){
    bcache.freenum[buk_idx]++;
  }
  release(&bcache.buk_lock[buk_idx]);
}


