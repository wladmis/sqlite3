/*
** Copyright (c) 2001 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License as published by the Free Software Foundation; either
** version 2 of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
** 
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*************************************************************************
** $Id$
*/
#include "sqliteInt.h"
#include "pager.h"
#include "btree.h"
#include <assert.h>

/*
** The maximum number of database entries that can be held in a single
** page of the database. 
*/
#define MX_CELL ((SQLITE_PAGE_SIZE-sizeof(PageHdr))/sizeof(Cell))

/*
** The maximum amount of data (in bytes) that can be stored locally for a
** database entry.  If the entry contains more data than this, the
** extra goes onto overflow pages.
*/
#define MX_LOCAL_PAYLOAD \
  ((SQLITE_PAGE_SIZE-sizeof(PageHdr)-4*(sizeof(Cell)+sizeof(Pgno)))/4)

/*
** The in-memory image of a disk page has the auxiliary information appended
** to the end.  EXTRA_SIZE is the number of bytes of space needed to hold
** that extra information.
*/
#define EXTRA_SIZE (sizeof(MemPage)-SQLITE_PAGE_SIZE)

/*
** Number of bytes on a single overflow page.
*/
#define OVERFLOW_SIZE (SQLITE_PAGE_SIZE-sizeof(Pgno))

/*
** Primitive data types.  u32 must be 4 bytes and u16 must be 2 bytes.
** Change these typedefs when porting to new architectures.
*/
typedef unsigned int u32;
typedef unsigned short int u16;

/*
** Forward declarations of structures used only in this file.
*/
typedef struct Page1Header Page1Header;
typedef struct MemPage MemPage;
typedef struct PageHdr PageHdr;
typedef struct Cell Cell;
typedef struct FreeBlk FreeBlk;
typedef struct OverflowPage OverflowPage;

/*
** All structures on a database page are aligned to 4-byte boundries.
** This routine rounds up a number of bytes to the next multiple of 4.
**
** This might need to change for computer architectures that require
** and 8-byte alignment boundry for structures.
*/
#define ROUNDUP(X)  ((X+3) & ~3)

/*
** The first pages of the database file contains some additional
** information used for housekeeping and sanity checking.  Otherwise,
** the first page is just like any other.  The additional information
** found on the first page is described by the following structure.
*/
struct Page1Header {
  u32 magic1;       /* A magic number for sanity checking */
  u32 magic2;       /* A second magic number for sanity checking */
  Pgno firstList;   /* First free page in a list of all free pages */
};
#define MAGIC_1  0x7264dc61
#define MAGIC_2  0x54e55d9e

/*
** Each database page has a header as follows:
**
**      page1_header          Optional instance of Page1Header structure
**      rightmost_pgno        Page number of the right-most child page
**      first_cell            Index into MemPage.aPage of first cell
**      first_free            Index of first free block
**
** MemPage.pStart always points to the rightmost_pgno.  First_free is
** 0 if there is no free space on this page.  Otherwise, first_free is
** the index in MemPage.aPage[] of a FreeBlk structure that describes
** the first block of free space.  All free space is defined by a linked
** list of FreeBlk structures.
**
** Data is stored in a linked list of Cell structures.  First_cell is
** the index into MemPage.aPage[] of the first cell on the page.  The
** Cells are kept in sorted order.
*/
struct PageHdr {
  Pgno pgno;      /* Child page that comes after all cells on this page */
  u16 firstCell;  /* Index in MemPage.aPage[] of the first cell */
  u16 firstFree;  /* Index in MemPage.aPage[] of the first free block */
};

/*
** Data on a database page is stored as a linked list of Cell structures.
** Both the key and the data are stored in aData[].  The key always comes
** first.  The aData[] field grows as necessary to hold the key and data,
** up to a maximum of MX_LOCAL_PAYLOAD bytes.  If the size of the key and
** data combined exceeds MX_LOCAL_PAYLOAD bytes, then the 4 bytes beginning
** at Cell.aData[MX_LOCAL_PAYLOAD] are the page number of the first overflow
** page.
*/
struct Cell {
  Pgno pgno;      /* Child page that comes before this cell */
  u16 nKey;       /* Number of bytes in the key */
  u16 iNext;      /* Index in MemPage.aPage[] of next cell in sorted order */
  u32 nData;      /* Number of bytes of data */
  char aData[4];  /* Key and data */
};

/*
** Free space on a page is remembered using a linked list of the FreeBlk
** structures.  Space on a database page is allocated in increments of
** at least 4 bytes and is always aligned to a 4-byte boundry.  The
** linked list of freeblocks is always kept in order by address.
*/
struct FreeBlk {
  u16 iSize;      /* Number of bytes in this block of free space */
  u16 iNext;      /* Index in MemPage.aPage[] of the next free block */
};

/*
** When the key and data for a single entry in the BTree will not fit in
** the MX_LOACAL_PAYLOAD bytes of space available on the database page,
** then all extra data is written to a linked list of overflow pages.
** Each overflow page is an instance of the following structure.
**
** Unused pages in the database are also represented by instances of
** the OverflowPage structure.  The Page1Header.freeList field is the
** page number of the first page in a linked list of unused database
** pages.
*/
struct OverflowPage {
  Pgno next;
  char aData[OVERFLOW_SIZE];
};

/*
** For every page in the database file, an instance of the following structure
** is stored in memory.  The aPage[] array contains the data obtained from
** the disk.  The rest is auxiliary data that held in memory only.  The
** auxiliary data is only valid for regular database pages - the auxiliary
** data is meaningless for overflow pages and pages on the freelist.
**
** Of particular interest in the auxiliary data is the aCell[] entry.  Each
** aCell[] entry is a pointer to a Cell structure in aPage[].  The cells are
** put in this array so that they can be accessed in constant time, rather
** than in linear time which would be needed if we walked the linked list.
**
** The pParent field points back to the parent page.  This allows us to
** walk up the BTree from any leaf to the root.  Care must be taken to
** unref() the parent page pointer when this page is no longer referenced.
** The pageDestructor() routine handles that.
*/
struct MemPage {
  char aPage[SQLITE_PAGE_SIZE];  /* Page data stored on disk */
  unsigned char isInit;          /* True if auxiliary data is initialized */
  unsigned char validLeft;       /* True if MemPage.left is valid */
  unsigned char validRight;      /* True if MemPage.right is valid */
  MemPage *pParent;              /* The parent of this page.  NULL for root */
  Pgno left;                     /* Left sibling page.  0==none */
  Pgno right;                    /* Right sibling page.  0==none */
  int idxStart;                  /* Index in aPage[] of real data */
  PageHdr *pStart;               /* Points to aPage[idxStart] */
  int nFree;                     /* Number of free bytes in aPage[] */
  int nCell;                     /* Number of entries on this page */
  Cell *aCell[MX_CELL];          /* All data entires in sorted order */
}

/*
** Everything we need to know about an open database
*/
struct Btree {
  Pager *pPager;        /* The page cache */
  BtCursor *pCursor;    /* A list of all open cursors */
  MemPage *page1;       /* First page of the database */
  int inTrans;          /* True if a transaction is in progress */
};
typedef Btree Bt;

/*
** A cursor is a pointer to a particular entry in the BTree.
** The entry is identified by its MemPage and the index in
** MemPage.aCell[] of the entry.
*/
struct BtCursor {
  Btree *pBt;                     /* The pointer back to the BTree */
  BtCursor *pPrev, *pNext;        /* List of all cursors */
  MemPage *pPage;                 /* Page that contains the entry */
  int idx;                        /* Index of the entry in pPage->aCell[] */
  int skip_incr;                  /* */
};

/*
** Defragment the page given.  All Cells are moved to the
** beginning of the page and all free space is collected 
** into one big FreeBlk at the end of the page.
*/
static void defragmentPage(MemPage *pPage){
  int pc;
  int i, n;
  FreeBlk *pFBlk;
  char newPage[SQLITE_PAGE_SIZE];

  pc = ROUNDUP(pPage->idxStart + sizeof(PageHdr));
  pPage->pStart->firstCell = pc;
  memcpy(newPage, pPage->aPage, pc);
  for(i=0; i<pPage->nCell; i++){
    Cell *pCell = &pPage->aCell[i];
    n = pCell->nKey + pCell->nData;
    if( n>MAX_LOCAL_PAYLOAD ) n = MAX_LOCAL_PAYLOAD + sizeof(Pgno);
    n = ROUNDUP(n);
    n += sizeof(Cell) - sizeof(pCell->aData);
    pCell->iNext = i<pPage->nCell ? pc + n : 0;
    memcpy(&newPage[pc], pCell, n);
    pPage->aCell[i] = (Cell*)&pPage->aPage[pc];
    pc += n;
  }
  assert( pPage->nFree==SQLITE_PAGE_SIZE-pc );
  memcpy(pPage->aPage, newPage, pc);
  pFBlk = &pPage->aPage[pc];
  pFBlk->iSize = SQLITE_PAGE_SIZE - pc;
  pFBlk->iNext = 0;
  pPage->pStart->firstFree = pc;
  memset(&pFBlk[1], 0, SQLITE_PAGE_SIZE - pc - sizeof(FreeBlk));
}

/*
** Allocate space on a page.  The space needs to be at least
** nByte bytes in size.  (Actually, all allocations are rounded
** up to the next even multiple of 4.)  Return the index into
** pPage->aPage[] of the first byte of the new allocation.
** Or return 0 if there is not enough free space on the page to
** satisfy the allocation request.
**
** If the page contains nBytes of free space but does not contain
** nBytes of contiguous free space, then defragementPage() is
** called to consolidate all free space before allocating the
** new chunk.
*/
static int allocSpace(MemPage *pPage, int nByte){
  FreeBlk *p;
  u16 *pIdx;
  int start;

  nByte = ROUNDUP(nByte);
  if( pPage->nFree<nByte ) return 0;
  pIdx = &pPage->pStart->firstFree;
  p = (FreeBlk*)&pPage->aPage[*pIdx];
  while( p->iSize<nByte ){
    if( p->iNext==0 ){
      defragmentPage(pPage);
      pIdx = &pPage->pStart->firstFree;
    }else{
      pIdx = &p->iNext;
    }
    p = (FreeBlk*)&pPage->aPage[*pIdx];
  }
  if( p->iSize==nByte ){
    start = *pIdx;
    *pIdx = p->iNext;
  }else{
    start = *pIdx;
    FreeBlk *pNew = (FreeBlk*)&pPage->aPage[start + nByte];
    pNew->iNext = p->iNext;
    pNew->iSize = p->iSize - nByte;
    *pIdx = start + nByte;
  }
  pPage->nFree -= nByte;
  return start;
}

/*
** Return a section of the MemPage.aPage[] to the freelist.
** The first byte of the new free block is pPage->aPage[start]
** and the size of the block is "size".
**
** Most of the effort here is involved in coalesing adjacent
** free blocks into a single big free block.
*/
static void freeSpace(MemPage *pPage, int start, int size){
  int end = start + size;
  u16 *pIdx, idx;
  FreeBlk *pFBlk;
  FreeBlk *pNew;
  FreeBlk *pNext;

  assert( size == ROUNDUP(size) );
  assert( start == ROUNDUP(start) );
  pIdx = &pPage->pStart->firstFree;
  idx = *pIdx;
  while( idx!=0 && idx<start ){
    pFBlk = (FreeBlk*)&pPage->aPage[idx];
    if( idx + pFBlk->iSize == start ){
      pFBlk->iSize += size;
      if( idx + pFBlk->iSize == pFBlk->iNext ){
        pNext = (FreeBlk*)&pPage->aPage[pFblk->iNext];
        pFBlk->iSize += pNext->iSize;
        pFBlk->iNext = pNext->iNext;
      }
      pPage->nFree += size;
      return;
    }
    pIdx = &pFBlk->iNext;
    idx = *pIdx;
  }
  pNew = (FreeBlk*)&pPage->aPage[start];
  if( idx != end ){
    pNew->iSize = size;
    pNew->iNext = idx;
  }else{
    pNext = (FreeBlk*)&pPage->aPage[idx];
    pNew->iSize = size + pNext->iSize;
    pNew->iNext = pNext->iNext;
  }
  *pIdx = start;
  pPage->nFree += size;
}

/*
** Initialize the auxiliary information for a disk block.
**
** Return SQLITE_OK on success.  If we see that the page does
** not contained a well-formed database page, then return 
** SQLITE_CORRUPT.  Note that a return of SQLITE_OK does not
** guarantee that the page is well-formed.  It only shows that
** we failed to detect any corruption.
*/
static int initPage(MemPage *pPage, Pgno pgnoThis, MemPage *pParent){
  int idx;
  Cell *pCell;
  FreeBlk *pFBlk;

  pPage->idxStart = (pgnoThis==1) ? sizeof(Page1Header) : 0;
  pPage->pStart = (PageHdr*)&pPage->aPage[pPage->idxStart];
  pPage->isInit = 1;
  assert( pPage->pParent==0 );
  pPage->pParent = pParent;
  if( pParent ) sqlitepager_ref(pParent);
  pPage->nCell = 0;
  idx = pPage->pStart->firstCell;
  while( idx!=0 ){
    if( idx>SQLITE_PAGE_SIZE-sizeof(Cell) ) goto page_format_error;
    if( idx<pPage->idxStart + sizeof(PageHeader) ) goto page_format_error;
    pCell = (Cell*)&pPage->aPage[idx];
    pPage->aCell[pPage->nCell++] = pCell;
    idx = pCell->iNext;
  }
  pPage->nFree = 0;
  idx = pPage->pStart->firstFree;
  while( idx!=0 ){
    if( idx>SQLITE_PAGE_SIZE-sizeof(FreeBlk) ) goto page_format_error;
    if( idx<pPage->idxStart + sizeof(PageHeader) ) goto page_format_error;
    pFBlk = (FreeBlk*)&pPage->aPage[idx];
    pPage->nFree += pFBlk->iSize;
    if( pFBlk->iNext <= idx ) goto page_format_error;
    idx = pFBlk->iNext;
  }
  return SQLITE_OK;

page_format_error:
  return SQLITE_CORRUPT;
}

/*
** This routine is called when the reference count for a page
** reaches zero.  We need to unref the pParent pointer when that
** happens.
*/
static void pageDestructor(void *pData){
  MemPage *pPage = (MemPage*)pData;
  if( pPage->pParent ){
    MemPage *pParent = pPage->pParent;
    pPage->pParent = 0;
    sqlitepager_unref(pParent);
  }
}

/*
** Open a new database.
**
** Actually, this routine just sets up the internal data structures
** for accessing the database.  We do not open the database file 
** until the first page is loaded.
*/
int sqliteBtreeOpen(const char *zFilename, int mode, Btree **ppBtree){
  Btree *pBt;

  pBt = sqliteMalloc( sizeof(*pBt) );
  if( pBt==0 ){
    **ppBtree = 0;
    return SQLITE_NOMEM;
  }
  rc = sqlitepager_open(&pBt->pPager, zFilename, 100, EXTRA_SPACE);
  if( rc!=SQLITE_OK ){
    if( pBt->pPager ) sqlitepager_close(pBt->pPager);
    sqliteFree(pBt);
    *ppBtree = 0;
    return rc;
  }
  sqlitepager_set_destructor(pBt->pPager, pageDestructor);
  pBt->pCursor = 0;
  pBt->page1 = 0;
  *ppBtree = pBt;
  return SQLITE_OK;
}

/*
** Close an open database and invalidate all cursors.
*/
int sqliteBtreeClose(Btree *pBt){
  while( pBt->pCursor ){
    sqliteBtreeCloseCursor(pBt->pCursor);
  }
  sqlitepager_close(pBt->pPager);
  sqliteFree(pBt);
  return SQLITE_OK;
}

/*
** Get a reference to page1 of the database file.  This will
** also acquire a readlock on that file.
**
** SQLITE_OK is returned on success.  If the file is not a
** well-formed database file, then SQLITE_CORRUPT is returned.
** SQLITE_BUSY is returned if the database is locked.  SQLITE_NOMEM
** is returned if we run out of memory.  SQLITE_PROTOCOL is returned
** if there is a locking protocol violation.
*/
static int lockBtree(Btree *pBt){
  int rc;
  if( pBt->page1 ) return SQLITE_OK;
  rc = sqlitepager_get(pBt->pPager, 1, &pBt->page1);
  if( rc!=SQLITE_OK ) return rc;
  rc = initPage(pBt->page1, 1, 0);
  if( rc!=SQLITE_OK ) goto page1_init_failed;

  /* Do some checking to help insure the file we opened really is
  ** a valid database file. 
  */
  if( sqlitepager_pagecount(pBt->pPager)>0 ){
    Page1Header *pP1 = (Page1Header*)pBt->page1;
    if( pP1->magic1!=MAGIC_1 || pP1->magic2!=MAGIC_2 ){
      rc = SQLITE_CORRUPT;
      goto page1_init_failed;
    }
  }
  return rc;

page1_init_failed:
  sqlitepager_unref(pBt->page1);
  pBt->page1 = 0;
  return rc;
}

/*
** Attempt to start a new transaction.
*/
int sqliteBtreeBeginTrans(Btree *pBt){
  int rc;
  Page1Header *pP1;
  if( pBt->inTrans ) return SQLITE_ERROR;
  if( pBt->page1==0 ){
    rc = lockBtree(pBt);
    if( rc!=SQLITE_OK ) return rc;
  }
  rc = sqlitepager_write(pBt->page1);
  if( rc==SQLITE_OK ){
    pBt->inTrans = 1;
  }
  pP1 = (Page1Header*)pBt->page1;
  if( pP1->magic1==0 ){
    pP1->magic1 = MAGIC_1;
    pP1->magic2 = MAGIC_2;
  }
  return rc;
}

/*
** Remove the last reference to the database file.  This will
** remove the read lock.
*/
static void unlockBtree(Btree *pBt){
  if( pBt->pCursor==0 && pBt->page1!=0 ){
    sqlitepager_unref(pBt->page1);
    pBt->page1 = 0;
    pBt->inTrans = 0;
  }
}

/*
** Commit the transaction currently in progress.  All cursors
** must be closed before this routine is called.
*/
int sqliteBtreeCommit(Btree *pBt){
  int rc;
  if( pBt->pCursor!=0 ) return SQLITE_ERROR;
  rc = sqlitepager_commit(pBt->pPager);
  unlockBtree(pBt);
  return rc;
}

/*
** Rollback the transaction in progress.  All cursors must be
** closed before this routine is called.
*/
int sqliteBtreeRollback(Btree *pBt){
  int rc;
  if( pBt->pCursor!=0 ) return SQLITE_ERROR;
  rc = sqlitepager_rollback(pBt->pPager);
  unlockBtree(pBt);
  return rc;
}

/*
** Create a new cursor.  The act of acquiring a cursor
** gets a read lock on the database file.
*/
int sqliteBtreeCursor(Btree *pBt, BtCursor **ppCur){
  int rc;
  BtCursor *pCur;
  if( pBt->page1==0 ){
    rc = lockBtree(pBt);
    if( rc!=SQLITE_OK ){
      *ppCur = 0;
      return rc;
    }
  }
  pCur = sqliteMalloc( sizeof(*pCur) );
  if( pCur==0 ){
    *ppCur = 0;
    unlockBtree(pBt);
    return SQLITE_NOMEM;
  }
  pCur->pPrev = 0;
  pCur->pNext = pBt->pCursor;
  if( pCur->pNext ){
    pCur->pNext->pPrev = pCur;
  }
  pBt->pCursor = pCur;
  pCur->pBt = pBt;
  rc = sqlitepager_get(pBt->pPager, 1, &pCur->pPage);
  if( rc!=SQLITE_OK ){
    sqliteFree(pCur);
    *ppCur = 0;
    return rc;
  }
  if( !pCur->pPage->isInit ){
    initPage(pCur->pPage, 1, 0);
  }
  pCur->idx = 0;
  pCur->depth = 0;
  pCur->aPage[0] = pCur->pPage;
  *ppCur = pCur;
  return SQLITE_OK;
}

/*
** Close a cursor. 
*/
int sqliteBtreeCloseCursor(BtCursor *pCur){
  Btree *pBt = pCur->pBt;
  int i;
  if( pCur->pPrev ){
    pCur->pPrev->pNext = pCur->pNext;
  }else{
    pBt->pCursor = pCur->pNext;
  }
  if( pCur->pNext ){
    pCur->pNext->pPrev = pCur->pPrev;
  }
  sqlitepager_unref(pCur->pPage);
  if( pBt->pCursor==0 && pBt->inTrans==0 ){
    unlockBtree(pBt);
  }
  sqliteFree(pCur);
}

/*
** Write the number of bytes of key for the entry the cursor is
** pointing to into *pSize.  Return SQLITE_OK.  Failure is not
** possible.
*/
int sqliteBtreeKeySize(BtCursor *pCur, int *pSize){
  Cell *pCell;
  MemPage *pPage;

  pPage = pCur->pPage;
  assert( pPage!=0 );
  if( pCur->idx >= pPage->nCell ){
    *pSize = 0;
  }else{
    pCell = pPage->aCell[pCur->idx];
    *psize = pCell->nKey;
  }
  return SQLITE_OK;
}

/*
** Read payload information from the entry that the pCur cursor is
** pointing to.  Begin reading the payload at "offset" and read
** a total of "amt" bytes.  Put the result in zBuf.
**
** This routine does not make a distinction between key and data.
** It just reads bytes from the payload area.
*/
static int getPayload(BtCursor *pCur, int offset, int amt, char *zBuf){
  char *aData;
  Pgno nextPage;
  assert( pCur!=0 && pCur->pPage!=0 );
  assert( pCur->idx>=0 && pCur->idx<pCur->nCell );
  aData = pCur->pPage->aCell[pCur->idx].aData;
  if( offset<MX_LOCAL_PAYLOAD ){
    int a = amt;
    if( a+offset>MX_LOCAL_PAYLOAD ){
      a = MX_LOCAL_PAYLOAD - offset;
    }
    memcpy(zBuf, &aData[offset], a);
    if( a==amt ){
      return SQLITE_OK;
    }
    offset += a;
    zBuf += a;
    amt -= a;
    if( amt>0 ){
      assert( a==ROUNDUP(a) );
      nextPage = *(Pgno*)&aData[a];
    }
  }
  while( amt>0 && nextPage ){
    OverflowPage *pOvfl;
    rc = sqlitepager_get(pCur->pBt->pPager, nextPage, &pOvfl);
    if( rc!=0 ){
      return rc;
    }
    nextPage = pOvfl->next;
    if( offset<OVERFLOW_SIZE ){
      int a = amt;
      if( a + offset > OVERFLOW_SIZE ){
        a = OVERFLOW_SIZE - offset;
      }
      memcpy(zBuf, &pOvfl->aData[offset], a);
      offset += a;
      amt -= a;
      zBuf += a;
    }
    sqlitepager_unref(pOvfl);
  }
  return amt==0 ? SQLITE_OK : SQLITE_CORRUPT;
}

/*
** Read part of the key associated with cursor pCur.  A total
** of "amt" bytes will be transfered into zBuf[].  The transfer
** begins at "offset".  If the key does not contain enough data
** to satisfy the request, no data is fetched and this routine
** returns SQLITE_ERROR.
*/
int sqliteBtreeKey(BtCursor *pCur, int offset, int amt, char *zBuf){
  Cell *pCell;
  MemPage *pPage;

  if( amt<0 ) return SQLITE_ERROR;
  if( offset<0 ) return SQLITE_ERROR;
  if( amt==0 ) return SQLITE_OK;
  pPage = pCur->pPage;
  assert( pPage!=0 );
  if( pCur->idx >= pPage->nCell ){
    return SQLITE_ERROR;
  }
  pCell = pPage->aCell[pCur->idx];
  if( amt+offset > pCell->nKey ){
  return getPayload(pCur, offset, amt, zBuf);
}

/*
** Write the number of bytes of data on the entry that the cursor
** is pointing to into *pSize.  Return SQLITE_OK.  Failure is
** not possible.
*/
int sqliteBtreeDataSize(BtCursor *pCur, int *pSize){
  Cell *pCell;
  MemPage *pPage;

  pPage = pCur->pPage;
  assert( pPage!=0 );
  if( pCur->idx >= pPage->nCell ){
    *pSize = 0;
  }else{
    pCell = pPage->aCell[pCur->idx];
    *pSize = pCell->nData;
  }
  return SQLITE_OK;
}

/*
** Read part of the data associated with cursor pCur.  A total
** of "amt" bytes will be transfered into zBuf[].  The transfer
** begins at "offset".  If the size of the data in the record
** is insufficent to satisfy this request then no data is read
** and this routine returns SQLITE_ERROR.
*/
int sqliteBtreeData(BtCursor *pCur, int offset, int amt, char *zBuf){
  Cell *pCell;
  MemPage *pPage;

  if( amt<0 ) return SQLITE_ERROR;
  if( offset<0 ) return SQLITE_ERROR;
  if( amt==0 ) return SQLITE_OK;
  pPage = pCur->pPage;
  assert( pPage!=0 );
  if( pCur->idx >= pPage->nCell ){
    return SQLITE_ERROR;
  }
  pCell = pPage->aCell[pCur->idx];
  if( amt+offset > pCell->nKey ){
  return getPayload(pCur, offset + pCell->nKey, amt, zBuf);
}

/*
** Compare the key for the entry that pCur points to against the 
** given key (pKey,nKeyOrig).  Put the comparison result in *pResult.
** The result is negative if pCur<pKey, zero if they are equal and
** positive if pCur>pKey.
**
** SQLITE_OK is returned on success.  If part of the cursor key
** is on overflow pages and we are unable to access those overflow
** pages, then some other value might be returned to indicate the
** reason for the error.
*/
static int compareKey(BtCursor *pCur, char *pKey, int nKeyOrig, int *pResult){
  Pgno nextPage;
  int nKey = nKeyOrig;
  int n;
  Cell *pCell;

  assert( pCur->pPage );
  assert( pCur->idx>=0 && pCur->idx<pCur->pPage->nCell );
  pCell = &pCur->pPage->aCell[pCur->idx];
  if( nKey > pCell->nKey ){
    nKey = pCell->nKey;
  }
  n = nKey;
  if( n>MX_LOCAL_PAYLOAD ){
    n = MX_LOCAL_PAYLOAD;
  }
  c = memcmp(pCell->aData, pKey, n);
  if( c!=0 ){
    *pResult = c;
    return SQLITE_OK;
  }
  pKey += n;
  nKey -= n;
  nextPage = *(Pgno*)&pCell->aData[MX_LOCAL_PAYLOAD];
  while( nKey>0 ){
    OverflowPage *pOvfl;
    if( nextPage==0 ){
      return SQLITE_CORRUPT;
    }
    rc = sqlitepager_get(pCur->pBt->pPager, nextPage, &pOvfl);
    if( rc ){
      return rc;
    }
    nextPage = pOvfl->next;
    n = nKey;
    if( n>OVERFLOW_SIZE ){
      n = OVERFLOW_SIZE;
    }
    c = memcmp(pOvfl->aData, pKey, n);
    sqlitepager_unref(pOvfl);
    if( c!=0 ){
      *pResult = c;
      return SQLITE_OK;
    }
    nKey -= n;
    pKey += n;
  }
  c = pCell->nKey - nKeyOrig;
  *pResult = c;
  return SQLITE_OK;
}

/*
** Move the cursor down to a new child page.
*/
static int childPage(BtCursor *pCur, int newPgno){
  int rc;
  MemPage *pNewPage;

  rc = sqlitepager_get(pCur->pBt->pPager, newPgno, &pNewPage);
  if( rc ){
    return rc;
  }
  if( !pNewPage->isInit ){
    initPage(pNewPage, newPgno, pCur->pPage);
  }
  sqlitepager_unref(pCur->pPage);
  pCur->pPage = pNewPage;
  pCur->idx = 0;
  return SQLITE_OK;
}

/*
** Move the cursor up to the parent page
*/
static int parentPage(BtCursor *pCur){
  Pgno oldPgno;
  MemPage *pParent;

  pParent = pCur->pPage->pParent;
  oldPgno = sqlitepager_pagenumber(pCur->pPage);
  if( pParent==0 ){
    return SQLITE_INTERNAL;
  }
  sqlitepager_ref(pParent);
  sqlitepager_unref(pCur->pPage);
  pCur->pPage = pParent;
  pCur->idx = pPage->nCell;
  for(i=0; i<pPage->nCell; i++){
    if( pPage->aCell[i].pgno==oldPgno ){
      pCur->idx = i;
      break;
    }
  }
}

/*
** Move the cursor to the root page
*/
static int rootPage(BtCursor *pCur){
  MemPage *pNew;
  pNew = pCur->pBt->page1;
  sqlitepager_ref(pNew);
  sqlitepager_unref(pCur->pPage);
  pCur->pPage = pNew;
  pCur->idx = 0;
  return SQLITE_OK;
}

/* Move the cursor so that it points to an entry near pKey.
** Return a success code.
**
** If pRes!=NULL, then *pRes is written with an integer code to
** describe the results.  *pRes is set to 0 if the cursor is left 
** pointing at an entry that exactly matches pKey.  *pRes is made
** negative if the cursor is on the largest entry less than pKey.
** *pRes is set positive if the cursor is on the smallest entry
** greater than pKey.  *pRes is not changed if the return value
** is something other than SQLITE_OK;
*/
int sqliteBtreeMoveto(BtCursor *pCur, void *pKey, int nKey, int *pRes){
  int rc;
  rc = rootPage(pCur);
  if( rc ) return rc;
  for(;;){
    int lwr, upr;
    Pgno chldPg;
    MemPage *pPage = pCur->pPage;
    lwr = 0;
    upr = pPage->nCell-1;
    while( lwr<=upr ){
      int c;
      pCur->idx = (lwr+upr)/2;
      rc = compareKey(pCur, pKey, nKey, &c);
      if( rc ) return rc;
      if( c==0 ){
        if( pRes ) *pRes = 0;
        return SQLITE_OK;
      }
      if( c<0 ){
        lwr = pCur->idx+1;
      }else{
        upr = pCur->idx-1;
      }
    }
    assert( lwr==upr+1 );
    if( lwr>=pPage->nCell ){
      chldPg = pPage->pStart->pgno;
    }else{
      chldPg = pPage->aCell[lwr].pgno;
    }
    if( chldPg==0 ){
      if( pRes ) *pRes = c;
      return SQLITE_OK;
    }
    rc = childPage(pCur, chldPg);
    if( rc ) return rc;
  }
}

/*
** Advance the cursor to the next entry in the database.  If pRes!=NULL
** then set *pRes=0 on success and set *pRes=1 if the cursor was
** pointing to the last entry in the database.
*/
int sqliteBtreeNext(BtCursor *pCur, int *pRes){
  MemPage *pPage;
  int rc;
  int moved = 0;
  if( pCur->skip_next ){
    pCur->skip_next = 0;
    if( pRes ) *pRes = 0;
    return SQLITE_OK;
  }
  pPage = pCur->pPage;
  pCur->idx++;
  while( pCur->idx>=pPage->nCell ){
    if( pCur->depth==0 ){
      if( pRes ) *pRes = 1;
      return SQLITE_OK;
    }
    rc = parentPage(pCur);
    if( rc ) return rc;
    moved = 1;
    pPage = pCur->pPage;
  }
  if( moved ){
    if( pRes ) *pRes = 0;
    return SQLITE_OK;
  }
  while( pCur->idx<pPage->nCell && pPage->aCell[pCur->idx].pgno>0 ){
    rc = childPage(pCur, pPage->aCell[pCur->idx].pgno);
    if( rc ) return rc;
    pPage = pCur->pPage;
  }
  if( pRes ) *pRes = 0;
  return SQLITE_OK;
}

int sqliteBtreeInsert(BtCursor*, void *pKey, int nKey, void *pData, int nData);
int sqliteBtreeDelete(BtCursor*);
