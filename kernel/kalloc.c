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

/*
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
*/

struct {
  struct spinlock lock;
  struct run *freelist;
  uint32 freenum;
} kmem[NCPU];

#define LOCKNAME(i) "kmem_" #i
static char *lockname[NCPU] = {
  LOCKNAME(0), LOCKNAME(1), LOCKNAME(2), LOCKNAME(3),
  LOCKNAME(4), LOCKNAME(5), LOCKNAME(6), LOCKNAME(7)
};

/*
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
};
*/
void
kinit() {
  for (int i = 0; i < NCPU; i++) {
    initlock(&(kmem[i].lock), lockname[i]);
    kmem[i].freenum = 0;
    kmem[i].freelist = 0;
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

// Free the page of physical memory pointed at by pa,
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
  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  kmem[id].freenum++;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  if (!kmem[id].freelist) {
    for (int i = 1; i < NCPU; i++) {
      int next = (id + i) % NCPU;
      acquire(&kmem[next].lock);
      if (kmem[next].freenum >= 2) {
        uint32 half = kmem[next].freenum / 2;
        struct run* tmp = kmem[next].freelist;
        for (int j = 0; j < half - 1; j++)
          tmp = tmp->next;
        kmem[id].freelist = tmp->next;
        kmem[id].freenum += (kmem[next].freenum - half);
        tmp->next = 0;
        kmem[next].freenum = half;
        //release(&kmem[next].lock);
        //if (!kmem[id].freelist) printf("1\n");
        //break;
      }
      release(&kmem[next].lock);
      if (kmem[id].freelist) break;
    }
  }
  r = kmem[id].freelist;
  if(r) {
    kmem[id].freelist = r->next;
    kmem[id].freenum--;
  }
  release(&kmem[id].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
