/*
** 2008 July 24
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file contains an alternative memory allocation system for SQLite.
** This system is implemented as a wrapper around the default memory
** allocation system (usually the one found in mem1.c - system malloc).
**
** This system differentiates between requests for "small" allocations 
** (by default those of 128 bytes or less) and "large" allocations (all
** others). The 256 byte threshhold is configurable at runtime.
**
** All requests for large allocations are passed through to the
** default memory allocation system.
**
** Requests for small allocations are met by allocating space within
** one or more larger "chunks" of memory obtained from the default
** memory allocation system. Chunks of memory are usually 64KB or 
** larger. The algorithm used to manage space within each chunk is
** the same as that used by mem5.c. 
**
** This strategy is designed to prevent the default memory allocation
** system (usually the system malloc) from suffering from heap 
** fragmentation. On some systems, heap fragmentation can cause a 
** significant real-time slowdown.
**
** $Id$
*/

#ifdef SQLITE_ENABLE_MEMSYS6

#include "sqliteInt.h"

/*
** Maximum size of any "small" allocation is ((1<<LOGMAX)*Mem6Chunk.nAtom).
** Mem6Chunk.nAtom is always at least 8, so this is not a practical
** limitation
*/
#define LOGMAX 30

/*
** Default value for the "small" allocation size threshold.
*/
#define SMALL_MALLOC_DEFAULT_THRESHOLD 256

/*
** Minimum size for a memory chunk.
*/
#define MIN_CHUNKSIZE (1<<16)


typedef struct Mem6Chunk Mem6Chunk;
typedef struct Mem6Link Mem6Link;

/*
** A minimum allocation is an instance of the following structure.
** Larger allocations are an array of these structures where the
** size of the array is a power of 2.
*/
struct Mem6Link {
  int next;       /* Index of next free chunk */
  int prev;       /* Index of previous free chunk */
};

/*
** Masks used for mem5.aCtrl[] elements.
*/
#define CTRL_LOGSIZE  0x1f    /* Log2 Size of this block relative to POW2_MIN */
#define CTRL_FREE     0x20    /* True if not checked out */

struct Mem6Chunk {
  Mem6Chunk *pNext;

  /*
  ** Lists of free blocks of various sizes.
  */
  int aiFreelist[LOGMAX+1];

  int nCheckedOut; /* Number of currently outstanding allocations */

  /*
  ** Space for tracking which blocks are checked out and the size
  ** of each block. One byte per block.
  */
  u8 *aCtrl;

  /*
  ** Memory available for allocation
  */
  int nAtom;       /* Smallest possible allocation in bytes */
  int nBlock;      /* Number of nAtom sized blocks in zPool */
  u8 *zPool;       /* Pointer to memory chunk from which allocations are made */
};

#define MEM6LINK(idx) ((Mem6Link *)(&pChunk->zPool[(idx)*pChunk->nAtom]))

/*
** Unlink the chunk at pChunk->aPool[i] from list it is currently
** on.  It should be found on pChunk->aiFreelist[iLogsize].
*/
static void memsys6Unlink(Mem6Chunk *pChunk, int i, int iLogsize){
  int next, prev;
  assert( i>=0 && i<pChunk->nBlock );
  assert( iLogsize>=0 && iLogsize<=LOGMAX );
  assert( (pChunk->aCtrl[i] & CTRL_LOGSIZE)==iLogsize );

  next = MEM6LINK(i)->next;
  prev = MEM6LINK(i)->prev;
  if( prev<0 ){
    pChunk->aiFreelist[iLogsize] = next;
  }else{
    MEM6LINK(prev)->next = next;
  }
  if( next>=0 ){
    MEM6LINK(next)->prev = prev;
  }
}

/*
** Link the chunk at mem5.aPool[i] so that is on the iLogsize
** free list.
*/
static void memsys6Link(Mem6Chunk *pChunk, int i, int iLogsize){
  int x;
  assert( i>=0 && i<pChunk->nBlock );
  assert( iLogsize>=0 && iLogsize<=LOGMAX );
  assert( (pChunk->aCtrl[i] & CTRL_LOGSIZE)==iLogsize );

  x = MEM6LINK(i)->next = pChunk->aiFreelist[iLogsize];
  MEM6LINK(i)->prev = -1;
  if( x>=0 ){
    assert( x<pChunk->nBlock );
    MEM6LINK(x)->prev = i;
  }
  pChunk->aiFreelist[iLogsize] = i;
}


/*
** Find the first entry on the freelist iLogsize.  Unlink that
** entry and return its index. 
*/
static int memsys6UnlinkFirst(Mem6Chunk *pChunk, int iLogsize){
  int i;
  int iFirst;

  assert( iLogsize>=0 && iLogsize<=LOGMAX );
  i = iFirst = pChunk->aiFreelist[iLogsize];
  assert( iFirst>=0 );
  while( i>0 ){
    if( i<iFirst ) iFirst = i;
    i = MEM6LINK(i)->next;
  }
  memsys6Unlink(pChunk, iFirst, iLogsize);
  return iFirst;
}

/*
** Allocate and return a block of nByte bytes from chunk pChunk. If the
** allocation request cannot be satisfied, return 0.
*/
static void *chunkMalloc(Mem6Chunk *pChunk, int nByte){
  int i;           /* Index of a mem5.aPool[] slot */
  int iBin;        /* Index into mem5.aiFreelist[] */
  int iFullSz;     /* Size of allocation rounded up to power of 2 */
  int iLogsize;    /* Log2 of iFullSz/POW2_MIN */

  /* Round nByte up to the next valid power of two */
  if( nByte>(pChunk->nBlock*pChunk->nAtom) ) return 0;
  for(iFullSz=pChunk->nAtom, iLogsize=0; iFullSz<nByte; iFullSz *= 2, iLogsize++){}

  /* Make sure mem5.aiFreelist[iLogsize] contains at least one free
  ** block.  If not, then split a block of the next larger power of
  ** two in order to create a new free block of size iLogsize.
  */
  for(iBin=iLogsize; pChunk->aiFreelist[iBin]<0 && iBin<=LOGMAX; iBin++){}
  if( iBin>LOGMAX ) return 0;
  i = memsys6UnlinkFirst(pChunk, iBin);
  while( iBin>iLogsize ){
    int newSize;

    iBin--;
    newSize = 1 << iBin;
    pChunk->aCtrl[i+newSize] = CTRL_FREE | iBin;
    memsys6Link(pChunk, i+newSize, iBin);
  }
  pChunk->aCtrl[i] = iLogsize;

  /* Return a pointer to the allocated memory. */
  pChunk->nCheckedOut++;
  return (void*)&pChunk->zPool[i*pChunk->nAtom];
}

/*
** Free the allocation pointed to by p, which is guaranteed to be non-zero
** and a part of chunk object pChunk.
*/
static void chunkFree(Mem6Chunk *pChunk, void *pOld){
  u32 size, iLogsize;
  int iBlock;             

  /* Set iBlock to the index of the block pointed to by pOld in 
  ** the array of pChunk->nAtom byte blocks pointed to by pChunk->zPool.
  */
  iBlock = ((u8 *)pOld-pChunk->zPool)/pChunk->nAtom;

  /* Check that the pointer pOld points to a valid, non-free block. */
  assert( iBlock>=0 && iBlock<pChunk->nBlock );
  assert( ((u8 *)pOld-pChunk->zPool)%pChunk->nAtom==0 );
  assert( (pChunk->aCtrl[iBlock] & CTRL_FREE)==0 );

  iLogsize = pChunk->aCtrl[iBlock] & CTRL_LOGSIZE;
  size = 1<<iLogsize;
  assert( iBlock+size-1<pChunk->nBlock );

  pChunk->aCtrl[iBlock] |= CTRL_FREE;
  pChunk->aCtrl[iBlock+size-1] |= CTRL_FREE;

  pChunk->aCtrl[iBlock] = CTRL_FREE | iLogsize;
  while( iLogsize<LOGMAX ){
    int iBuddy;
    if( (iBlock>>iLogsize) & 1 ){
      iBuddy = iBlock - size;
    }else{
      iBuddy = iBlock + size;
    }
    assert( iBuddy>=0 );
    if( (iBuddy+(1<<iLogsize))>pChunk->nBlock ) break;
    if( pChunk->aCtrl[iBuddy]!=(CTRL_FREE | iLogsize) ) break;
    memsys6Unlink(pChunk, iBuddy, iLogsize);
    iLogsize++;
    if( iBuddy<iBlock ){
      pChunk->aCtrl[iBuddy] = CTRL_FREE | iLogsize;
      pChunk->aCtrl[iBlock] = 0;
      iBlock = iBuddy;
    }else{
      pChunk->aCtrl[iBlock] = CTRL_FREE | iLogsize;
      pChunk->aCtrl[iBuddy] = 0;
    }
    size *= 2;
  }
  pChunk->nCheckedOut--;
  memsys6Link(pChunk, iBlock, iLogsize);
}

/*
** Return the actual size of the block pointed to by p, which is guaranteed
** to have been allocated from chunk pChunk.
*/
static int chunkSize(Mem6Chunk *pChunk, void *p){
  int iSize = 0;
  if( p ){
    int i = ((u8 *)p-pChunk->zPool)/pChunk->nAtom;
    assert( i>=0 && i<pChunk->nBlock );
    iSize = pChunk->nAtom * (1 << (pChunk->aCtrl[i]&CTRL_LOGSIZE));
  }
  return iSize;
}

/*
** Return true if there are currently no outstanding allocations.
*/
static int chunkIsEmpty(Mem6Chunk *pChunk){
  return (pChunk->nCheckedOut==0);
}

/*
** Initialize the buffer zChunk, which is nChunk bytes in size, as
** an Mem6Chunk object. Return a copy of the zChunk pointer.
*/
static Mem6Chunk *chunkInit(u8 *zChunk, int nChunk, int nMinAlloc){
  int ii;
  int iOffset;
  Mem6Chunk *pChunk = (Mem6Chunk *)zChunk;

  assert( nChunk>sizeof(Mem6Chunk) );
  assert( nMinAlloc>sizeof(Mem6Link) );

  memset(pChunk, 0, sizeof(Mem6Chunk));
  pChunk->nAtom = nMinAlloc;
  pChunk->nBlock = ((nChunk-sizeof(Mem6Chunk)) / (pChunk->nAtom+sizeof(u8)));

  pChunk->zPool = (u8 *)&pChunk[1];
  pChunk->aCtrl = &pChunk->zPool[pChunk->nBlock*pChunk->nAtom];

  for(ii=0; ii<=LOGMAX; ii++){
    pChunk->aiFreelist[ii] = -1;
  }

  iOffset = 0;
  for(ii=LOGMAX; ii>=0; ii--){
    int nAlloc = (1<<ii);
    if( (iOffset+nAlloc)<=pChunk->nBlock ){
      pChunk->aCtrl[iOffset] = ii | CTRL_FREE;
      memsys6Link(pChunk, iOffset, ii);
      iOffset += nAlloc;
    }
    assert((iOffset+nAlloc)>pChunk->nBlock);
  }

  return pChunk;
}

struct Mem6Global {
  sqlite3_mem_methods parent;     /* Used to allocate chunks */
  int nMinAlloc;                  /* Minimum allowed allocation size */
  int nThreshold;                 /* Allocs larger than this go to parent */
  sqlite3_mutex *mutex;
  Mem6Chunk *pChunk;              /* Singly linked list of all memory chunks */
} mem6;


static void mem6Enter(void){
  sqlite3_mutex_enter(mem6.mutex);
}

static void mem6Leave(void){
  sqlite3_mutex_leave(mem6.mutex);
}

/*
** Based on the number and size of the currently allocated chunks, return
** the size of the next chunk to allocate, in bytes.
*/
static int nextChunkSize(void){
  int iTotal = 0;
  Mem6Chunk *p;
  for(p=mem6.pChunk; p; p=p->pNext){
    iTotal += mem6.parent.xSize((void *)p);
  }
  if( iTotal==0 ){
    iTotal = MIN_CHUNKSIZE;
  }
  return iTotal;
}

/*
** The argument is a pointer that may or may not have been allocated from
** one of the Mem6Chunk objects managed within mem6. If it is, return
** a pointer to the owner chunk. If not, return 0.
*/
static Mem6Chunk *findChunk(u8 *p){
  Mem6Chunk *pChunk;
  for(pChunk=mem6.pChunk; pChunk; pChunk=pChunk->pNext){
    if( p>=pChunk->zPool && p<=&pChunk->zPool[pChunk->nBlock*pChunk->nAtom] ){
      return pChunk;
    }
  }
  return 0;
}

static void freeChunk(Mem6Chunk *pChunk){
  Mem6Chunk **pp = &mem6.pChunk;
  for( pp=&mem6.pChunk; *pp!=pChunk; pp = &(*pp)->pNext );
  *pp = (*pp)->pNext;
  mem6.parent.xFree(pChunk);
}

static void *memsys6Malloc(int nByte){
  Mem6Chunk *pChunk;
  void *p = 0;

  mem6Enter();
  if( nByte>mem6.nThreshold ){
    p = mem6.parent.xMalloc(nByte);
  }else{
    for(pChunk=mem6.pChunk; !p && pChunk; pChunk=pChunk->pNext){
      p = chunkMalloc(pChunk, nByte);
    }
  
    if( !p ){
      int iSize = nextChunkSize();
      p = mem6.parent.xMalloc(iSize);
      if( p ){
        pChunk = chunkInit((u8 *)p, iSize, mem6.nMinAlloc);
        pChunk->pNext = mem6.pChunk;
        mem6.pChunk = pChunk;
        p = chunkMalloc(pChunk, nByte);
        assert(p);
      }
    }
  }
  mem6Leave();

  return p;
}

static int memsys6Size(void *p){
  Mem6Chunk *pChunk;
  int iSize;
  mem6Enter();
  pChunk = findChunk(p);
  iSize = (pChunk ? chunkSize(pChunk, p) : mem6.parent.xSize(p));
  mem6Leave();
  return iSize;
}

static void memsys6Free(void *p){
  Mem6Chunk *pChunk;

  mem6Enter();
  pChunk = findChunk(p);
  if( pChunk ){
    chunkFree(pChunk, p);
    if( chunkIsEmpty(pChunk) ){
      freeChunk(pChunk);
    }
  }else{
    mem6.parent.xFree(p);
  }
  mem6Leave();
}

static void *memsys6Realloc(void *p, int nByte){
  void *p2;

  if( p && nByte<=memsys6Size(p) ){
    p2 = p;
  }else{
    p2 = memsys6Malloc(nByte);
    if( p && p2 ){
      memcpy(p2, p, memsys6Size(p));
      memsys6Free(p);
    }
  }

  return p2;
}


static int memsys6Roundup(int n){
  int iFullSz;
  for(iFullSz=mem6.nMinAlloc; iFullSz<n; iFullSz *= 2);
  return iFullSz;
}

static int memsys6Init(void *pCtx){
  u8 bMemstat = sqlite3Config.bMemstat;
  mem6.parent = *sqlite3MemGetDefault();
  mem6.nMinAlloc = 16;
  mem6.pChunk = 0;
  mem6.nThreshold = sqlite3Config.nSmall;
  if( mem6.nThreshold<=0 ){
    mem6.nThreshold = SMALL_MALLOC_DEFAULT_THRESHOLD;
  }
  if( !bMemstat ){
    mem6.mutex = sqlite3MutexAlloc(SQLITE_MUTEX_STATIC_MEM);
  }

  /* Initialize the parent allocator. */
#ifdef SQLITE_MEMDEBUG
  sqlite3Config.bMemstat = 1;
#endif
  mem6.parent.xInit(mem6.parent.pAppData);
#ifdef SQLITE_MEMDEBUG
  sqlite3Config.bMemstat = bMemstat;
#endif

  return SQLITE_OK;
}

static void memsys6Shutdown(void *pCtx){
  if( mem6.parent.xShutdown ){
    mem6.parent.xShutdown(mem6.parent.pAppData);
  }
  memset(&mem6, 0, sizeof(mem6));
}

/*
** This routine is the only routine in this file with external 
** linkage. It returns a pointer to a static sqlite3_mem_methods
** struct populated with the memsys6 methods.
*/
const sqlite3_mem_methods *sqlite3MemGetMemsys6(void){
  static const sqlite3_mem_methods memsys6Methods = {
     memsys6Malloc,
     memsys6Free,
     memsys6Realloc,
     memsys6Size,
     memsys6Roundup,
     memsys6Init,
     memsys6Shutdown,
     0
  };
  return &memsys6Methods;
}

#endif
