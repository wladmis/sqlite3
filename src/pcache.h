/*
** 2008 August 05
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This header file defines the interface that the sqlite page cache
** subsystem. 
**
** @(#) $Id$
*/

#ifndef _PCACHE_H_

typedef struct PgHdr PgHdr;
typedef struct PCache PCache;

/*
** Every page in the cache is controlled by an instance of the following
** structure.
*/
struct PgHdr {
  u32 flags;                     /* PGHDR flags defined below */
  void *pData;                   /* Content of this page */
  void *pExtra;                  /* Extra content */
  PgHdr *pDirty;                 /* Transient list of dirty pages */
  Pgno pgno;                     /* Page number for this page */
  Pager *pPager;
#ifdef SQLITE_CHECK_PAGES
  u32 pageHash;
#endif
  /*** Public data is above. All that follows is private to pcache.c ***/
  PCache *pCache;                /* Cache that owns this page */
  PgHdr *pNextHash, *pPrevHash;  /* Hash collision chain for PgHdr.pgno */
  PgHdr *pNext, *pPrev;          /* List of clean or dirty pages */
  PgHdr *pNextLru, *pPrevLru;    /* Part of global LRU list */
  int nRef;                      /* Number of users of this page */
  void *apSave[2];               /* Journal entries for in-memory databases */
};

/* Bit values for PgHdr.flags */
#define PGHDR_IN_JOURNAL        0x001  /* Page is in rollback journal */
#define PGHDR_IN_STMTJRNL       0x002  /* Page is in the statement journal */
#define PGHDR_DIRTY             0x004  /* Page has changed */
#define PGHDR_NEED_SYNC         0x008  /* Peed to fsync this page */
#define PGHDR_ALWAYS_ROLLBACK   0x010  /* Force writing to journal */
#define PGHDR_NEED_READ         0x020  /* Content is unread */
#define PGHDR_IS_INIT           0x040  /* pData is initialized */
#define PGHDR_REUSE_UNLIKELY    0x080  /* Hint: Reuse is unlikely */


/* Initialize and shutdown the page cache subsystem */
int sqlite3PcacheInitialize(void);
void sqlite3PcacheShutdown(void);

/* Page cache buffer management:
** These routines implement SQLITE_CONFIG_PAGECACHE.
*/
void sqlite3PCacheBufferSetup(void *, int sz, int n);
void *sqlite3PCacheMalloc(int sz);
void sqlite3PCacheFree(void*);

/* Create a new pager cache.
** Under memory stress, invoke xStress to try to make pages clean.
** Only clean and unpinned pages can be reclaimed.
*/
void sqlite3PcacheOpen(
  int szPage,                    /* Size of every page */
  int szExtra,                   /* Extra space associated with each page */
  int bPurgeable,                /* True if pages are on backing store */
  void (*xDestroy)(PgHdr *),     /* Called to destroy a page */
  int (*xStress)(void*, PgHdr*), /* Call to try to make pages clean */
  void *pStress,                 /* Argument to xStress */
  PCache *pToInit                /* Preallocated space for the PCache */
);

/* Modify the page-size after the cache has been created. */
void sqlite3PcacheSetPageSize(PCache *, int);

/* Return the size in bytes of a PCache object.  Used to preallocate
** storage space.
*/
int sqlite3PcacheSize(void);

/* One release per successful fetch.  Page is pinned until released.
** Reference counted. 
*/
int sqlite3PcacheFetch(PCache*, Pgno, int createFlag, PgHdr**);
void sqlite3PcacheRelease(PgHdr*);

void sqlite3PcacheDrop(PgHdr*);         /* Remove page from cache */
void sqlite3PcacheMakeDirty(PgHdr*);    /* Make sure page is marked dirty */
void sqlite3PcacheMakeClean(PgHdr*);    /* Mark a single page as clean */
void sqlite3PcacheCleanAll(PCache*);    /* Mark all dirty list pages as clean */

/* Change a page number.  Used by incr-vacuum. */
void sqlite3PcacheMove(PgHdr*, Pgno);

/* Set a global maximum page count for all page caches. 
** If the sum of individual cache maxes exceed the global max, the
** individuals are scaled down proportionally. 
*/
void sqlite3PcacheGlobalMax(int N);

/* Remove all pages with pgno>x.  Reset the cache if x==0 */
void sqlite3PcacheTruncate(PCache*, Pgno x);

/* Routines used to implement transactions on memory-only databases. */
int sqlite3PcachePreserve(PgHdr*, int);    /* Preserve current page content */
void sqlite3PcacheCommit(PCache*, int);    /* Drop preserved copy */
void sqlite3PcacheRollback(PCache*, int);  /* Rollback to preserved copy */

/* Get a list of all dirty pages in the cache, sorted by page number */
PgHdr *sqlite3PcacheDirtyList(PCache*);

/* Reset and close the cache object */
void sqlite3PcacheClose(PCache*);

/* Set flags on all pages in the page cache */
void sqlite3PcacheSetFlags(PCache*, int andMask, int orMask);

/* Assert flags settings on all pages.  Debugging only */
void sqlite3PcacheAssertFlags(PCache*, int trueMask, int falseMask);

/* Return true if the number of dirty pages is 0 or 1 */
int sqlite3PcacheZeroOrOneDirtyPages(PCache*);

/* Discard the contents of the cache */
int sqlite3PcacheClear(PCache*);

/* Return the total number of outstanding page references */
int sqlite3PcacheRefCount(PCache*);

/* Increment the reference count of an existing page */
void sqlite3PcacheRef(PgHdr*);

/* Return the total number of pages stored in the cache */
int sqlite3PcachePagecount(PCache*);

/* Iterate through all pages currently stored in the cache. This interface
** is only available if SQLITE_CHECK_PAGES is defined when the library is 
** built.
*/
void sqlite3PcacheIterate(PCache *pCache, void (*xIter)(PgHdr *));

/* Set and get the suggested cache-size for the specified pager-cache. If
** a global maximum on the number of pages cached by the system is 
** configured via the sqlite3PcacheGlobalMax() API, then the suggested
** cache-sizes are not used at all.
**
** If no global maximum is configured, then the system attempts to limit
** the total number of pages cached by purgeable pager-caches to the sum
** of the suggested cache-sizes.
*/
int sqlite3PcacheGetCachesize(PCache *);
void sqlite3PcacheSetCachesize(PCache *, int);

/* Lock and unlock a pager-cache object. The PcacheLock() function may 
** block if the lock is temporarily available. While a pager-cache is locked,
** the system guarantees that any configured xStress() callback will not
** be invoked by any thread other than the one holding the lock.
*/
void sqlite3PcacheLock(PCache *);
void sqlite3PcacheUnlock(PCache *);

int sqlite3PcacheReleaseMemory(int);

#endif /* _PCACHE_H_ */
