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
  struct spinlock lock;
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
} bcache;

#define HASHTABLE_LEN 13
#define BUCKETNAME(i) "bcache_" #i
static char *bucketname[HASHTABLE_LEN] = {
  BUCKETNAME(0), BUCKETNAME(1), BUCKETNAME(2), BUCKETNAME(3),
  BUCKETNAME(4), BUCKETNAME(5), BUCKETNAME(6), BUCKETNAME(7),
  BUCKETNAME(8), BUCKETNAME(9), BUCKETNAME(10), BUCKETNAME(11),
  BUCKETNAME(12)
};
struct bucket {
  struct spinlock lock;
  struct buf head;
} hashtable[HASHTABLE_LEN];

/*
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}
*/


void
binit(void)
{
  struct buf *b;
  int even = NBUF / HASHTABLE_LEN;

  initlock(&bcache.lock, "bcache");
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = 0;
    initsleeplock(&b->lock, "buffer");
  }

  b = bcache.buf;
  for (int i = 0; i < HASHTABLE_LEN; i++) {
    if (hashtable[i].head.next != 0) panic("binit: hashtable[i].head.next != 0");
    for (int j = 0; j < even; j++) {
      b->next = hashtable[i].head.next;
      hashtable[i].head.next = b;
      b++;
    }
    initlock(&hashtable[i].lock, bucketname[i]);
  }
  for (int i = 0; i < NBUF % HASHTABLE_LEN; i++) {
    b->next = hashtable[i].head.next;
    hashtable[i].head.next = b;
    b++;
  }
  if (b != bcache.buf + NBUF) panic("binit: b != bcache.buf + NBUF");
}

/*
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}
*/

static struct buf*
bget(uint dev, uint blockno) {
  int index = blockno % HASHTABLE_LEN;
  acquire(&hashtable[index].lock);
  struct buf* b = hashtable[index].head.next;
  while (b != 0) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&hashtable[index].lock);
      acquiresleep(&b->lock);
      return b;
    }
    b = b->next;
  }
  release(&hashtable[index].lock);
  acquire(&bcache.lock); 
  acquire(&hashtable[index].lock);
  b = hashtable[index].head.next;
  while (b != 0) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&hashtable[index].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
    b = b->next;
  }
  release(&hashtable[index].lock);
  struct buf *prev;
  for (int j = 0; j < HASHTABLE_LEN; j++) {
    int i = (index + j) % HASHTABLE_LEN;
    acquire(&hashtable[i].lock);
    b = hashtable[i].head.next;
    prev = &hashtable[i].head;
    while (b != 0) {
      if (b->refcnt == 0) {
        if (i != index) {
          prev->next = b->next;
          b->next = 0;
          release(&hashtable[i].lock);
          acquire(&hashtable[index].lock);
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;
          b->next = hashtable[index].head.next;
          hashtable[index].head.next = b;
          release(&hashtable[index].lock);
          release(&bcache.lock);
          acquiresleep(&b->lock);
          return b;
        } else {
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;
          release(&hashtable[i].lock);
          release(&bcache.lock);
          acquiresleep(&b->lock);
          return b;
        }
      }
      b = b->next;
      prev = prev->next;
    }
    release(&hashtable[i].lock);
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

/*
// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}
*/

void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  if (b->refcnt == 0) panic("brelse_0:b->refcnt == 0");
  releasesleep(&b->lock);
  int index = b->blockno % HASHTABLE_LEN;
  if (b->refcnt == 0) panic("brelse_1:b->refcnt == 0");
  acquire(&hashtable[index].lock);
  if (b->refcnt == 0) panic("brelse_2:b->refcnt == 0");
  b->refcnt--;
  if (hashtable[index].head.next == 0) panic("brelse: buf == 0\n");
  release(&hashtable[index].lock);
}

/*
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}
*/
void
bpin(struct buf *b) {
  int index = b->blockno % HASHTABLE_LEN;
  acquire(&hashtable[index].lock);
  if (b->refcnt == 0) panic("bpin: refcnt == 0\n");
  b->refcnt++;
  release(&hashtable[index].lock);
}

/*
void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}
*/

void
bunpin(struct buf *b) {
  int index = b->blockno % HASHTABLE_LEN;
  acquire(&hashtable[index].lock);
  if (b->refcnt == 0) panic("bunpin: refcnt == 0\n");
  b->refcnt--;
  release(&hashtable[index].lock);
}