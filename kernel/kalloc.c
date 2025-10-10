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
} kmem;

uint32 ref_count[(PHYSTOP - KERNBASE) / PGSIZE] = {0};
struct spinlock ref_lock;  // 保护 ref_count 数组的锁

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref_lock, "refcount");  // 初始化引用计数锁
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
  if (ref_count[((uint64)pa - KERNBASE) / PGSIZE] != 0) return;
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
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
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&ref_lock);
    ref_count[((uint64)r - KERNBASE) / PGSIZE] = 1;
    release(&ref_lock);
  }
  return (void*)r;
}

// 增加物理页的引用计数（原子操作）
void
ref_inc(uint64 pa)
{
  if(pa < (uint64)end || pa >= PHYSTOP)
    panic("ref_inc: pa");
  
  acquire(&ref_lock);
  uint32 index = (pa - KERNBASE) / PGSIZE;
  ref_count[index]++;
  release(&ref_lock);
}

// 减少物理页的引用计数（原子操作）
// 返回递减后的值
int
ref_dec(uint64 pa)
{
  if(pa < (uint64)end || pa >= PHYSTOP)
    panic("ref_def: pa");
  
  acquire(&ref_lock);
  uint32 index = (pa - KERNBASE) / PGSIZE;
  if(ref_count[index] == 0)
    panic("ref_dec: ref_count is zero");
  ref_count[index]--;
  int count = ref_count[index];
  release(&ref_lock);
  
  return count;
}