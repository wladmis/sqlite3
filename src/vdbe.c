/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** The code in this file implements the Virtual Database Engine (VDBE)
**
** The SQL parser generates a program which is then executed by
** the VDBE to do the work of the SQL statement.  VDBE programs are 
** similar in form to assembly language.  The program consists of
** a linear sequence of operations.  Each operation has an opcode 
** and 3 operands.  Operands P1 and P2 are integers.  Operand P3 
** is a null-terminated string.   The P2 operand must be non-negative.
** Opcodes will typically ignore one or more operands.  Many opcodes
** ignore all three operands.
**
** Computation results are stored on a stack.  Each entry on the
** stack is either an integer, a null-terminated string, a floating point
** number, or the SQL "NULL" value.  An inplicit conversion from one
** type to the other occurs as necessary.
** 
** Most of the code in this file is taken up by the sqliteVdbeExec()
** function which does the work of interpreting a VDBE program.
** But other routines are also provided to help in building up
** a program instruction by instruction.
**
** $Id$
*/
#include "sqliteInt.h"
#include <ctype.h>

/*
** SQL is translated into a sequence of instructions to be
** executed by a virtual machine.  Each instruction is an instance
** of the following structure.
*/
typedef struct VdbeOp Op;

/*
** Boolean values
*/
typedef unsigned char Bool;

/*
** A cursor is a pointer into a database file.  The database file
** can represent either an SQL table or an SQL index.  Each file is
** a bag of key/data pairs.  The cursor can loop over all key/data
** pairs (in an arbitrary order) or it can retrieve a particular
** key/data pair given a copy of the key.
** 
** Every cursor that the virtual machine has open is represented by an
** instance of the following structure.
*/
struct Cursor {
  BtCursor *pCursor;    /* The cursor structure of the backend */
  int lastRecno;        /* Last recno from a Next or NextIdx operation */
  Bool recnoIsValid;    /* True if lastRecno is valid */
  Bool keyAsData;       /* The OP_Column command works on key instead of data */
  Bool atFirst;         /* True if pointing to first entry */
  Btree *pBt;           /* Separate file holding temporary table */
  char *zKey;           /* Key used in BeginIdx and NextIdx operators */
  int nKey;             /* Number of bytes in zKey[] */
};
typedef struct Cursor Cursor;

/*
** A sorter builds a list of elements to be sorted.  Each element of
** the list is an instance of the following structure.
*/
typedef struct Sorter Sorter;
struct Sorter {
  int nKey;           /* Number of bytes in the key */
  char *zKey;         /* The key by which we will sort */
  int nData;          /* Number of bytes in the data */
  char *pData;        /* The data associated with this key */
  Sorter *pNext;      /* Next in the list */
};

/* 
** Number of buckets used for merge-sort.  
*/
#define NSORT 30

/*
** A single level of the stack is an instance of the following
** structure.  Except, string values are stored on a separate
** list of of pointers to character.  The reason for storing
** strings separately is so that they can be easily passed
** to the callback function.
*/
struct Stack {
  int i;         /* Integer value */
  int n;         /* Number of characters in string value, including '\0' */
  int flags;     /* Some combination of STK_Null, STK_Str, STK_Dyn, etc. */
  double r;      /* Real value */
};
typedef struct Stack Stack;

/*
** Memory cells use the same structure as the stack except that space
** for an arbitrary string is added.
*/
struct Mem {
  Stack s;       /* All values of the memory cell besides string */
  char *z;       /* String value for this memory cell */
};
typedef struct Mem Mem;

/*
** Allowed values for Stack.flags
*/
#define STK_Null      0x0001   /* Value is NULL */
#define STK_Str       0x0002   /* Value is a string */
#define STK_Int       0x0004   /* Value is an integer */
#define STK_Real      0x0008   /* Value is a real number */
#define STK_Dyn       0x0010   /* Need to call sqliteFree() on zStack[*] */

/*
** An Agg structure describes an Aggregator.  Each Agg consists of
** zero or more Aggregator elements (AggElem).  Each AggElem contains
** a key and one or more values.  The values are used in processing
** aggregate functions in a SELECT.  The key is used to implement
** the GROUP BY clause of a select.
*/
typedef struct Agg Agg;
typedef struct AggElem AggElem;
struct Agg {
  int nMem;            /* Number of values stored in each AggElem */
  AggElem *pCurrent;   /* The AggElem currently in focus */
  HashElem *pSearch;   /* The hash element for pCurrent */
  Hash hash;           /* Hash table of all aggregate elements */
};
struct AggElem {
  char *zKey;          /* The key to this AggElem */
  int nKey;            /* Number of bytes in the key, including '\0' at end */
  Mem aMem[1];         /* The values for this AggElem */
};

/*
** A Set structure is used for quick testing to see if a value
** is part of a small set.  Sets are used to implement code like
** this:
**            x.y IN ('hi','hoo','hum')
*/
typedef struct Set Set;
struct Set {
  Hash hash;             /* A set is just a hash table */
};

/*
** A Keylist is a bunch of keys into a table.  The keylist can
** grow without bound.  The keylist stores the keys of database
** records that need to be deleted.
*/
typedef struct Keylist Keylist;
struct Keylist {
  int nKey;         /* Number of slots in aKey[] */
  int nUsed;        /* Next unwritten slot in aKey[] */
  int nRead;        /* Next unread slot in aKey[] */
  Keylist *pNext;   /* Next block of keys */
  int aKey[1];      /* One or more keys.  Extra space allocated as needed */
};

/*
** An instance of the virtual machine
*/
struct Vdbe {
  sqlite *db;         /* The whole database */
  Btree *pBt;         /* Opaque context structure used by DB backend */
  FILE *trace;        /* Write an execution trace here, if not NULL */
  int nOp;            /* Number of instructions in the program */
  int nOpAlloc;       /* Number of slots allocated for aOp[] */
  Op *aOp;            /* Space to hold the virtual machine's program */
  int nLabel;         /* Number of labels used */
  int nLabelAlloc;    /* Number of slots allocated in aLabel[] */
  int *aLabel;        /* Space to hold the labels */
  int tos;            /* Index of top of stack */
  int nStackAlloc;    /* Size of the stack */
  Stack *aStack;      /* The operand stack, except string values */
  char **zStack;      /* Text or binary values of the stack */
  char **azColName;   /* Becomes the 4th parameter to callbacks */
  int nCursor;        /* Number of slots in aCsr[] */
  Cursor *aCsr;       /* On element of this array for each open cursor */
  int nList;          /* Number of slots in apList[] */
  Keylist **apList;   /* For each Keylist */
  int nSort;          /* Number of slots in apSort[] */
  Sorter **apSort;    /* An open sorter list */
  FILE *pFile;        /* At most one open file handler */
  int nField;         /* Number of file fields */
  char **azField;     /* Data for each file field */
  char *zLine;        /* A single line from the input file */
  int nLineAlloc;     /* Number of spaces allocated for zLine */
  int nMem;           /* Number of memory locations currently allocated */
  Mem *aMem;          /* The memory locations */
  Agg agg;            /* Aggregate information */
  int nSet;           /* Number of sets allocated */
  Set *aSet;          /* An array of sets */
  int nFetch;         /* Number of OP_Fetch instructions executed */
};

/*
** Create a new virtual database engine.
*/
Vdbe *sqliteVdbeCreate(sqlite *db){
  Vdbe *p;
  p = sqliteMalloc( sizeof(Vdbe) );
  if( p==0 ) return 0;
  p->pBt = db->pBe;
  p->db = db;
  return p;
}

/*
** Turn tracing on or off
*/
void sqliteVdbeTrace(Vdbe *p, FILE *trace){
  p->trace = trace;
}

/*
** Add a new instruction to the list of instructions current in the
** VDBE.  Return the address of the new instruction.
**
** Parameters:
**
**    p               Pointer to the VDBE
**
**    op              The opcode for this instruction
**
**    p1, p2, p3      Three operands.
**
**    lbl             A symbolic label for this instruction.
**
** Symbolic labels are negative numbers that stand for the address
** of instructions that have yet to be coded.  When the instruction
** is coded, its real address is substituted in the p2 field of
** prior and subsequent instructions that have the lbl value in
** their p2 fields.
*/
int sqliteVdbeAddOp(Vdbe *p, int op, int p1, int p2, const char *p3, int lbl){
  int i, j;

  i = p->nOp;
  p->nOp++;
  if( i>=p->nOpAlloc ){
    int oldSize = p->nOpAlloc;
    p->nOpAlloc = p->nOpAlloc*2 + 10;
    p->aOp = sqliteRealloc(p->aOp, p->nOpAlloc*sizeof(Op));
    if( p->aOp==0 ){
      p->nOp = 0;
      p->nOpAlloc = 0;
      return 0;
    }
    memset(&p->aOp[oldSize], 0, (p->nOpAlloc-oldSize)*sizeof(Op));
  }
  p->aOp[i].opcode = op;
  p->aOp[i].p1 = p1;
  if( p2<0 && (-1-p2)<p->nLabel && p->aLabel[-1-p2]>=0 ){
    p2 = p->aLabel[-1-p2];
  }
  p->aOp[i].p2 = p2;
  if( p3 && p3[0] ){
    p->aOp[i].p3 = sqliteStrDup(p3);
    p->aOp[i].p3dyn = 1;
  }else{
    p->aOp[i].p3 = 0;
  }
  if( lbl<0 && (-lbl)<=p->nLabel ){
    p->aLabel[-1-lbl] = i;
    for(j=0; j<i; j++){
      if( p->aOp[j].p2==lbl ) p->aOp[j].p2 = i;
    }
  }
  return i;
}

/*
** Resolve label "x" to be the address of the next instruction to
** be inserted.
*/
void sqliteVdbeResolveLabel(Vdbe *p, int x){
  int j;
  if( x<0 && (-x)<=p->nLabel ){
    p->aLabel[-1-x] = p->nOp;
    for(j=0; j<p->nOp; j++){
      if( p->aOp[j].p2==x ) p->aOp[j].p2 = p->nOp;
    }
  }
}

/*
** Return the address of the next instruction to be inserted.
*/
int sqliteVdbeCurrentAddr(Vdbe *p){
  return p->nOp;
}

/*
** Add a whole list of operations to the operation stack.  Return the
** address of the first operation added.
*/
int sqliteVdbeAddOpList(Vdbe *p, int nOp, VdbeOp const *aOp){
  int addr;
  if( p->nOp + nOp >= p->nOpAlloc ){
    int oldSize = p->nOpAlloc;
    p->nOpAlloc = p->nOpAlloc*2 + nOp + 10;
    p->aOp = sqliteRealloc(p->aOp, p->nOpAlloc*sizeof(Op));
    if( p->aOp==0 ){
      p->nOp = 0;
      p->nOpAlloc = 0;
      return 0;
    }
    memset(&p->aOp[oldSize], 0, (p->nOpAlloc-oldSize)*sizeof(Op));
  }
  addr = p->nOp;
  if( nOp>0 ){
    int i;
    for(i=0; i<nOp; i++){
      int p2 = aOp[i].p2;
      if( p2<0 ) p2 = addr + ADDR(p2);
      sqliteVdbeAddOp(p, aOp[i].opcode, aOp[i].p1, p2, aOp[i].p3, 0);
    }
  }
  return addr;
}

/*
** Change the value of the P1 operand for a specific instruction.
** This routine is useful when a large program is loaded from a
** static array using sqliteVdbeAddOpList but we want to make a
** few minor changes to the program.
*/
void sqliteVdbeChangeP1(Vdbe *p, int addr, int val){
  if( p && addr>=0 && p->nOp>addr ){
    p->aOp[addr].p1 = val;
  }
}

/*
** Change the value of the P3 operand for a specific instruction.
** This routine is useful when a large program is loaded from a
** static array using sqliteVdbeAddOpList but we want to make a
** few minor changes to the program.
**
** If n>=0 then the P3 operand is dynamic, meaning that a copy of
** the string is made into memory obtained from sqliteMalloc().
** A value of n==0 means copy bytes of zP3 up to and including the
** first null byte.  If n>0 then copy n+1 bytes of zP3.
**
** If n<0 then zP3 is assumed to be a pointer to a static string.
** P3 is made to point directly to this string without any copying.
*/
void sqliteVdbeChangeP3(Vdbe *p, int addr, char *zP3, int n){
  if( p && addr>=0 && p->nOp>addr && zP3 ){
    Op *pOp = &p->aOp[addr];
    if( pOp->p3 && pOp->p3dyn ){
      sqliteFree(pOp->p3);
    }
    if( n<0 ){
      pOp->p3 = zP3;
      pOp->p3dyn = 0;
    }else{
      sqliteSetNString(&p->aOp[addr].p3, zP3, n, 0);
      pOp->p3dyn = 1;
    }
  }
}

/*
** If the P3 operand to the specified instruction appears
** to be a quoted string token, then this procedure removes 
** the quotes.
**
** The quoting operator can be either a grave ascent (ASCII 0x27)
** or a double quote character (ASCII 0x22).  Two quotes in a row
** resolve to be a single actual quote character within the string.
*/
void sqliteVdbeDequoteP3(Vdbe *p, int addr){
  Op *pOp;
  if( addr<0 || addr>=p->nOp ) return;
  pOp = &p->aOp[addr];
  if( pOp->p3==0 || pOp->p3[0]==0 ) return;
  if( !pOp->p3dyn ){
    pOp->p3 = sqliteStrDup(pOp->p3);
    pOp->p3dyn = 1;
  }
  if( pOp->p3 ) sqliteDequote(pOp->p3);
}

/*
** On the P3 argument of the given instruction, change all
** strings of whitespace characters into a single space and
** delete leading and trailing whitespace.
*/
void sqliteVdbeCompressSpace(Vdbe *p, int addr){
  char *z;
  int i, j;
  Op *pOp;
  if( addr<0 || addr>=p->nOp ) return;
  pOp = &p->aOp[addr];
  if( !pOp->p3dyn ){
    pOp->p3 = sqliteStrDup(pOp->p3);
    pOp->p3dyn = 1;
  }
  z = pOp->p3;
  if( z==0 ) return;
  i = j = 0;
  while( isspace(z[i]) ){ i++; }
  while( z[i] ){
    if( isspace(z[i]) ){
      z[j++] = ' ';
      while( isspace(z[++i]) ){}
    }else{
      z[j++] = z[i++];
    }
  }
  while( i>0 && isspace(z[i-1]) ){
    z[i-1] = 0;
    i--;
  }
}

/*
** Create a new symbolic label for an instruction that has yet to be
** coded.  The symbolic label is really just a negative number.  The
** label can be used as the P2 value of an operation.  Later, when
** the label is resolved to a specific address, the VDBE will scan
** through its operation list and change all values of P2 which match
** the label into the resolved address.
**
** The VDBE knows that a P2 value is a label because labels are
** always negative and P2 values are suppose to be non-negative.
** Hence, a negative P2 value is a label that has yet to be resolved.
*/
int sqliteVdbeMakeLabel(Vdbe *p){
  int i;
  i = p->nLabel++;
  if( i>=p->nLabelAlloc ){
    p->nLabelAlloc = p->nLabelAlloc*2 + 10;
    p->aLabel = sqliteRealloc( p->aLabel, p->nLabelAlloc*sizeof(p->aLabel[0]));
  }
  if( p->aLabel==0 ){
    p->nLabel = 0;
    p->nLabelAlloc = 0;
    return 0;
  }
  p->aLabel[i] = -1;
  return -1-i;
}

/*
** Reset an Agg structure.  Delete all its contents.
*/
static void AggReset(Agg *pAgg){
  int i;
  HashElem *p;
  for(p = sqliteHashFirst(&pAgg->hash); p; p = sqliteHashNext(p)){
    AggElem *pElem = sqliteHashData(p);
    for(i=0; i<pAgg->nMem; i++){
      if( pElem->aMem[i].s.flags & STK_Dyn ){
        sqliteFree(pElem->aMem[i].z);
      }
    }
    sqliteFree(pElem);
  }
  sqliteHashClear(&pAgg->hash);
  pAgg->pCurrent = 0;
  pAgg->pSearch = 0;
  pAgg->nMem = 0;
}

/*
** Insert a new element and make it the current element.  
**
** Return 0 on success and 1 if memory is exhausted.
*/
static int AggInsert(Agg *p, char *zKey, int nKey){
  AggElem *pElem;
  int i;
  pElem = sqliteMalloc( sizeof(AggElem) + nKey +
                        (p->nMem-1)*sizeof(pElem->aMem[0]) );
  if( pElem==0 ) return 1;
  pElem->zKey = (char*)&pElem->aMem[p->nMem];
  memcpy(pElem->zKey, zKey, nKey);
  pElem->nKey = nKey;
  sqliteHashInsert(&p->hash, pElem->zKey, pElem->nKey, pElem);
  for(i=0; i<p->nMem; i++){
    pElem->aMem[i].s.flags = STK_Null;
  }
  p->pCurrent = pElem;
  return 0;
}

/*
** Get the AggElem currently in focus
*/
#define AggInFocus(P)   ((P).pCurrent ? (P).pCurrent : _AggInFocus(&(P)))
static AggElem *_AggInFocus(Agg *p){
  HashElem *pElem = sqliteHashFirst(&p->hash);
  if( pElem==0 ){
    AggInsert(p,"",1);
    pElem = sqliteHashFirst(&p->hash);
  }
  return pElem ? sqliteHashData(pElem) : 0;
}

/*
** Convert the given stack entity into a string if it isn't one
** already.  Return non-zero if we run out of memory.
**
** NULLs are converted into an empty string.
*/
#define Stringify(P,I) \
   ((P->aStack[I].flags & STK_Str)==0 ? hardStringify(P,I) : 0)
static int hardStringify(Vdbe *p, int i){
  Stack *pStack = &p->aStack[i];
  char **pzStack = &p->zStack[i];
  char zBuf[30];
  int fg = pStack->flags;
  if( fg & STK_Real ){
    sprintf(zBuf,"%.15g",pStack->r);
  }else if( fg & STK_Int ){
    sprintf(zBuf,"%d",pStack->i);
  }else{
    p->zStack[i] = "";
    pStack->n = 1;
    pStack->flags |= STK_Str;
    return 0;
  }
  *pzStack = sqliteStrDup(zBuf);
  if( *pzStack==0 ) return 1;
  pStack->n = strlen(*pzStack)+1;
  pStack->flags |= STK_Str|STK_Dyn;
  return 0;
}

/*
** Release the memory associated with the given stack level
*/
#define Release(P,I)  if((P)->aStack[I].flags&STK_Dyn){ hardRelease(P,I); }
static void hardRelease(Vdbe *p, int i){
  sqliteFree(p->zStack[i]);
  p->zStack[i] = 0;
  p->aStack[i].flags &= ~(STK_Str|STK_Dyn);
}

/*
** Convert the given stack entity into a integer if it isn't one
** already.
**
** Any prior string or real representation is invalidated.  
** NULLs are converted into 0.
*/
#define Integerify(P,I) \
    if(((P)->aStack[(I)].flags&STK_Int)==0){ hardIntegerify(P,I); }
static void hardIntegerify(Vdbe *p, int i){
  if( p->aStack[i].flags & STK_Real ){
    p->aStack[i].i = p->aStack[i].r;
    Release(p, i);
  }else if( p->aStack[i].flags & STK_Str ){
    p->aStack[i].i = atoi(p->zStack[i]);
    Release(p, i);
  }else{
    p->aStack[i].i = 0;
  }
  p->aStack[i].flags = STK_Int;
}

/*
** Get a valid Real representation for the given stack element.
**
** Any prior string or integer representation is retained.
** NULLs are converted into 0.0.
*/
#define Realify(P,I) \
    if(((P)->aStack[(I)].flags&STK_Real)==0){ hardRealify(P,I); }
static void hardRealify(Vdbe *p, int i){
  if( p->aStack[i].flags & STK_Str ){
    p->aStack[i].r = atof(p->zStack[i]);
  }else if( p->aStack[i].flags & STK_Int ){
    p->aStack[i].r = p->aStack[i].i;
  }else{
    p->aStack[i].r = 0.0;
  }
  p->aStack[i].flags |= STK_Real;
}

/*
** Pop the stack N times.  Free any memory associated with the
** popped stack elements.
*/
static void PopStack(Vdbe *p, int N){
  char **pzStack;
  Stack *pStack;
  if( p->zStack==0 ) return;
  pStack = &p->aStack[p->tos];
  pzStack = &p->zStack[p->tos];
  p->tos -= N;
  while( N-- > 0 ){
    if( pStack->flags & STK_Dyn ){
      sqliteFree(*pzStack);
    }
    pStack->flags = 0;
    *pzStack = 0;
    pStack--;
    pzStack--;
  }
}

/*
** Here is a macro to handle the common case of popping the stack
** once.  This macro only works from within the sqliteVdbeExec()
** function.
*/
#define POPSTACK \
 if( aStack[p->tos].flags & STK_Dyn ) sqliteFree(zStack[p->tos]); \
 p->tos--;

/*
** Make sure space has been allocated to hold at least N
** stack elements.  Allocate additional stack space if
** necessary.
**
** Return 0 on success and non-zero if there are memory
** allocation errors.
*/
#define NeedStack(P,N) (((P)->nStackAlloc<=(N)) ? hardNeedStack(P,N) : 0)
static int hardNeedStack(Vdbe *p, int N){
  int oldAlloc;
  int i;
  if( N>=p->nStackAlloc ){
    oldAlloc = p->nStackAlloc;
    p->nStackAlloc = N + 20;
    p->aStack = sqliteRealloc(p->aStack, p->nStackAlloc*sizeof(p->aStack[0]));
    p->zStack = sqliteRealloc(p->zStack, p->nStackAlloc*sizeof(char*));
    if( p->aStack==0 || p->zStack==0 ){
      sqliteFree(p->aStack);
      sqliteFree(p->zStack);
      p->aStack = 0;
      p->zStack = 0;
      p->nStackAlloc = 0;
      return 1;
    }
    for(i=oldAlloc; i<p->nStackAlloc; i++){
      p->zStack[i] = 0;
      p->aStack[i].flags = 0;
    }
  }
  return 0;
}

/*
** Delete a keylist
*/
static void KeylistFree(Keylist *p){
  while( p ){
    Keylist *pNext = p->pNext;
    sqliteFree(p);
    p = pNext;
  }
}

/*
** Close a cursor and release all the resources that cursor happens
** to hold.
*/
static void cleanupCursor(Cursor *pCx){
  if( pCx->pCursor ){
    sqliteBtreeCloseCursor(pCx->pCursor);
  }
  if( pCx->zKey ){
    sqliteFree(pCx->zKey);
  }
  if( pCx->pBt ){
    sqliteBtreeClose(pCx->pBt);
  }
  memset(pCx, 0, sizeof(Cursor));
}

/*
** Close all cursors
*/
static void closeAllCursors(Vdbe *p){
  int i;
  for(i=0; i<p->nCursor; i++){
    cleanupCursor(&p->aCsr[i]);
  }
  sqliteFree(p->aCsr);
  p->aCsr = 0;
  p->nCursor = 0;
}

/*
** Clean up the VM after execution.
**
** This routine will automatically close any cursors, lists, and/or
** sorters that were left open.
*/
static void Cleanup(Vdbe *p){
  int i;
  PopStack(p, p->tos+1);
  sqliteFree(p->azColName);
  p->azColName = 0;
  closeAllCursors(p);
  for(i=0; i<p->nMem; i++){
    if( p->aMem[i].s.flags & STK_Dyn ){
      sqliteFree(p->aMem[i].z);
    }
  }
  sqliteFree(p->aMem);
  p->aMem = 0;
  p->nMem = 0;
  for(i=0; i<p->nList; i++){
    KeylistFree(p->apList[i]);
    p->apList[i] = 0;
  }
  sqliteFree(p->apList);
  p->apList = 0;
  p->nList = 0;
  for(i=0; i<p->nSort; i++){
    Sorter *pSorter;
    while( (pSorter = p->apSort[i])!=0 ){
      p->apSort[i] = pSorter->pNext;
      sqliteFree(pSorter->zKey);
      sqliteFree(pSorter->pData);
      sqliteFree(pSorter);
    }
  }
  sqliteFree(p->apSort);
  p->apSort = 0;
  p->nSort = 0;
  if( p->pFile ){
    if( p->pFile!=stdin ) fclose(p->pFile);
    p->pFile = 0;
  }
  if( p->azField ){
    sqliteFree(p->azField);
    p->azField = 0;
  }
  p->nField = 0;
  if( p->zLine ){
    sqliteFree(p->zLine);
    p->zLine = 0;
  }
  p->nLineAlloc = 0;
  AggReset(&p->agg);
  for(i=0; i<p->nSet; i++){
    sqliteHashClear(&p->aSet[i].hash);
  }
  sqliteFree(p->aSet);
  p->aSet = 0;
  p->nSet = 0;
}

/*
** Delete an entire VDBE.
*/
void sqliteVdbeDelete(Vdbe *p){
  int i;
  if( p==0 ) return;
  Cleanup(p);
  if( p->nOpAlloc==0 ){
    p->aOp = 0;
    p->nOp = 0;
  }
  for(i=0; i<p->nOp; i++){
    if( p->aOp[i].p3dyn ){
      sqliteFree(p->aOp[i].p3);
    }
  }
  sqliteFree(p->aOp);
  sqliteFree(p->aLabel);
  sqliteFree(p->aStack);
  sqliteFree(p->zStack);
  sqliteFree(p);
}

/*
** A translation from opcode numbers to opcode names.  Used for testing
** and debugging only.
**
** If any of the numeric OP_ values for opcodes defined in sqliteVdbe.h
** change, be sure to change this array to match.  You can use the
** "opNames.awk" awk script which is part of the source tree to regenerate
** this array, then copy and paste it into this file, if you want.
*/
static char *zOpName[] = { 0,
  "Transaction",       "Commit",            "Rollback",          "ReadCookie",
  "SetCookie",         "VerifyCookie",      "Open",              "OpenTemp",
  "OpenWrite",         "OpenAux",           "OpenWrAux",         "Close",
  "MoveTo",            "Fcnt",              "NewRecno",          "Put",
  "Distinct",          "Found",             "NotFound",          "Delete",
  "Column",            "KeyAsData",         "Recno",             "FullKey",
  "Rewind",            "Next",              "Destroy",           "Clear",
  "CreateIndex",       "CreateTable",       "Reorganize",        "BeginIdx",
  "NextIdx",           "PutIdx",            "DeleteIdx",         "MemLoad",
  "MemStore",          "ListOpen",          "ListWrite",         "ListRewind",
  "ListRead",          "ListClose",         "SortOpen",          "SortPut",
  "SortMakeRec",       "SortMakeKey",       "Sort",              "SortNext",
  "SortKey",           "SortCallback",      "SortClose",         "FileOpen",
  "FileRead",          "FileColumn",        "FileClose",         "AggReset",
  "AggFocus",          "AggIncr",           "AggNext",           "AggSet",
  "AggGet",            "SetInsert",         "SetFound",          "SetNotFound",
  "SetClear",          "MakeRecord",        "MakeKey",           "MakeIdxKey",
  "Goto",              "If",                "Halt",              "ColumnCount",
  "ColumnName",        "Callback",          "Integer",           "String",
  "Null",              "Pop",               "Dup",               "Pull",
  "Add",               "AddImm",            "Subtract",          "Multiply",
  "Divide",            "Min",               "Max",               "Like",
  "Glob",              "Eq",                "Ne",                "Lt",
  "Le",                "Gt",                "Ge",                "IsNull",
  "NotNull",           "Negative",          "And",               "Or",
  "Not",               "Concat",            "Noop",              "Strlen",
  "Substr",          
};

/*
** Given the name of an opcode, return its number.  Return 0 if
** there is no match.
**
** This routine is used for testing and debugging.
*/
int sqliteVdbeOpcode(const char *zName){
  int i;
  for(i=1; i<=OP_MAX; i++){
    if( sqliteStrICmp(zName, zOpName[i])==0 ) return i;
  }
  return 0;
}

/*
** Give a listing of the program in the virtual machine.
**
** The interface is the same as sqliteVdbeExec().  But instead of
** running the code, it invokes the callback once for each instruction.
** This feature is used to implement "EXPLAIN".
*/
int sqliteVdbeList(
  Vdbe *p,                   /* The VDBE */
  sqlite_callback xCallback, /* The callback */
  void *pArg,                /* 1st argument to callback */
  char **pzErrMsg            /* Error msg written here */
){
  int i, rc;
  char *azValue[6];
  char zAddr[20];
  char zP1[20];
  char zP2[20];
  static char *azColumnNames[] = {
     "addr", "opcode", "p1", "p2", "p3", 0
  };

  if( xCallback==0 ) return 0;
  azValue[0] = zAddr;
  azValue[2] = zP1;
  azValue[3] = zP2;
  azValue[5] = 0;
  rc = SQLITE_OK;
  /* if( pzErrMsg ){ *pzErrMsg = 0; } */
  for(i=0; rc==SQLITE_OK && i<p->nOp; i++){
    if( p->db->flags & SQLITE_Interrupt ){
      p->db->flags &= ~SQLITE_Interrupt;
      sqliteSetString(pzErrMsg, "interrupted", 0);
      rc = SQLITE_INTERRUPT;
      break;
    }
    sprintf(zAddr,"%d",i);
    sprintf(zP1,"%d", p->aOp[i].p1);
    sprintf(zP2,"%d", p->aOp[i].p2);
    azValue[4] = p->aOp[i].p3;
    azValue[1] = zOpName[p->aOp[i].opcode];
    if( xCallback(pArg, 5, azValue, azColumnNames) ){
      rc = SQLITE_ABORT;
    }
  }
  return rc;
}

/*
** The parameters are pointers to the head of two sorted lists
** of Sorter structures.  Merge these two lists together and return
** a single sorted list.  This routine forms the core of the merge-sort
** algorithm.
**
** In the case of a tie, left sorts in front of right.
*/
static Sorter *Merge(Sorter *pLeft, Sorter *pRight){
  Sorter sHead;
  Sorter *pTail;
  pTail = &sHead;
  pTail->pNext = 0;
  while( pLeft && pRight ){
    int c = sqliteSortCompare(pLeft->zKey, pRight->zKey);
    if( c<=0 ){
      pTail->pNext = pLeft;
      pLeft = pLeft->pNext;
    }else{
      pTail->pNext = pRight;
      pRight = pRight->pNext;
    }
    pTail = pTail->pNext;
  }
  if( pLeft ){
    pTail->pNext = pLeft;
  }else if( pRight ){
    pTail->pNext = pRight;
  }
  return sHead.pNext;
}

/*
** Convert an integer into a big-endian integer.  In other words,
** make sure the most significant byte comes first.
*/
static int bigEndian(int x){
  union {
     char zBuf[sizeof(int)];
     int i;
  } ux;
  ux.zBuf[3] = x&0xff;
  ux.zBuf[2] = (x>>8)&0xff;
  ux.zBuf[1] = (x>>16)&0xff;
  ux.zBuf[0] = (x>>24)&0xff;
  return ux.i;
}

/*
** Code contained within the VERIFY() macro is not needed for correct
** execution.  It is there only to catch errors.  So when we compile
** with NDEBUG=1, the VERIFY() code is omitted.
*/
#ifdef NDEBUG
# define VERIFY(X)
#else
# define VERIFY(X) X
#endif

/*
** Execute the program in the VDBE.
**
** If an error occurs, an error message is written to memory obtained
** from sqliteMalloc() and *pzErrMsg is made to point to that memory.
** The return parameter is the number of errors.
**
** If the callback every returns non-zero, then the program exits
** immediately.  No error message but the function does return SQLITE_ABORT.
**
** A memory allocation error causes this routine to return SQLITE_NOMEM
** and abandon furture processing.
**
** Other fatal errors return SQLITE_ERROR.
**
** If a database file could not be opened because it is locked by
** another database instance, then the xBusy() callback is invoked
** with pBusyArg as its first argument, the name of the table as the
** second argument, and the number of times the open has been attempted
** as the third argument.  The xBusy() callback will typically wait
** for the database file to be openable, then return.  If xBusy()
** returns non-zero, another attempt is made to open the file.  If
** xBusy() returns zero, or if xBusy is NULL, then execution halts
** and this routine returns SQLITE_BUSY.
*/
int sqliteVdbeExec(
  Vdbe *p,                   /* The VDBE */
  sqlite_callback xCallback, /* The callback */
  void *pArg,                /* 1st argument to callback */
  char **pzErrMsg,           /* Error msg written here */
  void *pBusyArg,            /* 1st argument to the busy callback */
  int (*xBusy)(void*,const char*,int)  /* Called when a file is busy */
){
  int pc;                    /* The program counter */
  Op *pOp;                   /* Current operation */
  int rc;                    /* Value to return */
  Btree *pBt = p->pBt;       /* The backend driver */
  sqlite *db = p->db;        /* The database */
  char **zStack;             /* Text stack */
  Stack *aStack;             /* Additional stack information */
  char zBuf[100];            /* Space to sprintf() an integer */


  /* No instruction ever pushes more than a single element onto the
  ** stack.  And the stack never grows on successive executions of the
  ** same loop.  So the total number of instructions is an upper bound
  ** on the maximum stack depth required.
  **
  ** Allocation all the stack space we will ever need.
  */
  NeedStack(p, p->nOp);
  zStack = p->zStack;
  aStack = p->aStack;
  p->tos = -1;

  /* Initialize the aggregrate hash table.
  */
  sqliteHashInit(&p->agg.hash, SQLITE_HASH_BINARY, 0);
  p->agg.pSearch = 0;

  rc = SQLITE_OK;
#ifdef MEMORY_DEBUG
  if( access("vdbe_trace",0)==0 ){
    p->trace = stdout;
  }
#endif
  /* if( pzErrMsg ){ *pzErrMsg = 0; } */
  if( sqlite_malloc_failed ) rc = SQLITE_NOMEM;
  for(pc=0; rc==SQLITE_OK && pc<p->nOp VERIFY(&& pc>=0); pc++){
    pOp = &p->aOp[pc];

    /* Interrupt processing if requested.
    */
    if( db->flags & SQLITE_Interrupt ){
      db->flags &= ~SQLITE_Interrupt;
      rc = SQLITE_INTERRUPT;
      sqliteSetString(pzErrMsg, "interrupted", 0);
      break;
    }

    /* Only allow tracing if NDEBUG is not defined.
    */
#ifndef NDEBUG
    if( p->trace ){
      fprintf(p->trace,"%4d %-12s %4d %4d %s\n",
        pc, zOpName[pOp->opcode], pOp->p1, pOp->p2,
           pOp->p3 ? pOp->p3 : "");
      fflush(p->trace);
    }
#endif

    switch( pOp->opcode ){

/*****************************************************************************
** What follows is a massive switch statement where each case implements a
** separate instruction in the virtual machine.  If we follow the usual
** indentation conventions, each case should be indented by 6 spaces.  But
** that is a lot of wasted space on the left margin.  So the code within
** the switch statement will break with convention and be flush-left. Another
** big comment (similar to this one) will mark the point in the code where
** we transition back to normal indentation.
*****************************************************************************/

/* Opcode:  Goto P2 * *
**
** An unconditional jump to address P2.
** The next instruction executed will be 
** the one at index P2 from the beginning of
** the program.
*/
case OP_Goto: {
  pc = pOp->p2 - 1;
  break;
}

/* Opcode:  Halt * * *
**
** Exit immediately.  All open cursors, Lists, Sorts, etc are closed
** automatically.
**
** There is an implied Halt instruction inserted at the very end of
** every program.  So a jump past the last instruction of the program
** is the same as executing Halt.
*/
case OP_Halt: {
  pc = p->nOp-1;
  break;
}

/* Opcode: Integer P1 * *
**
** The integer value P1 is pushed onto the stack.
*/
case OP_Integer: {
  int i = ++p->tos;
  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  aStack[i].i = pOp->p1;
  aStack[i].flags = STK_Int;
  break;
}

/* Opcode: String * * P3
**
** The string value P3 is pushed onto the stack.  If P3==0 then a
** NULL is pushed onto the stack.
*/
case OP_String: {
  int i = ++p->tos;
  char *z;
  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  z = pOp->p3;
  if( z==0 ){
    zStack[i] = 0;
    aStack[i].flags = STK_Null;
  }else{
    zStack[i] = z;
    aStack[i].n = strlen(z) + 1;
    aStack[i].flags = STK_Str;
  }
  break;
}

#if 0 /* NOT USED */
/* Opcode: Null * * *
**
** Push a NULL value onto the stack.
*/
case OP_Null: {
  int i = ++p->tos;
  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  zStack[i] = 0;
  aStack[i].flags = STK_Null;
  break;
}
#endif

/* Opcode: Pop P1 * *
**
** P1 elements are popped off of the top of stack and discarded.
*/
case OP_Pop: {
  PopStack(p, pOp->p1);
  break;
}

/* Opcode: Dup P1 * *
**
** A copy of the P1-th element of the stack 
** is made and pushed onto the top of the stack.
** The top of the stack is element 0.  So the
** instruction "Dup 0 0 0" will make a copy of the
** top of the stack.
**
** Also see the Pull instruction.
*/
case OP_Dup: {
  int i = p->tos - pOp->p1;
  int j = ++p->tos;
  VERIFY( if( i<0 ) goto not_enough_stack; )
  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  aStack[j] = aStack[i];
  if( aStack[i].flags & STK_Dyn ){
    zStack[j] = sqliteMalloc( aStack[j].n );
    if( zStack[j]==0 ) goto no_mem;
    memcpy(zStack[j], zStack[i], aStack[j].n);
  }else{
    zStack[j] = zStack[i];
  }
  break;
}

/* Opcode: Pull P1 * *
**
** The P1-th element is removed from its current location on 
** the stack and pushed back on top of the stack.  The
** top of the stack is element 0, so "Pull 0 0 0" is
** a no-op.  "Pull 1 0 0" swaps the top two elements of
** the stack.
**
** See also the Dup instruction.
*/
case OP_Pull: {
  int from = p->tos - pOp->p1;
  int to = p->tos;
  int i;
  Stack ts;
  char *tz;
  VERIFY( if( from<0 ) goto not_enough_stack; )
  ts = aStack[from];
  tz = zStack[from];
  for(i=from; i<to; i++){
    aStack[i] = aStack[i+1];
    zStack[i] = zStack[i+1];
  }
  aStack[to] = ts;
  zStack[to] = tz;
  break;
}

/* Opcode: ColumnCount P1 * *
**
** Specify the number of column values that will appear in the
** array passed as the 4th parameter to the callback.  No checking
** is done.  If this value is wrong, a coredump can result.
*/
case OP_ColumnCount: {
  p->azColName = sqliteRealloc(p->azColName, (pOp->p1+1)*sizeof(char*));
  if( p->azColName==0 ) goto no_mem;
  p->azColName[pOp->p1] = 0;
  break;
}

/* Opcode: ColumnName P1 * P3
**
** P3 becomes the P1-th column name (first is 0).  An array of pointers
** to all column names is passed as the 4th parameter to the callback.
** The ColumnCount opcode must be executed first to allocate space to
** hold the column names.  Failure to do this will likely result in
** a coredump.
*/
case OP_ColumnName: {
  p->azColName[pOp->p1] = pOp->p3 ? pOp->p3 : "";
  break;
}

/* Opcode: Callback P1 * *
**
** Pop P1 values off the stack and form them into an array.  Then
** invoke the callback function using the newly formed array as the
** 3rd parameter.
*/
case OP_Callback: {
  int i = p->tos - pOp->p1 + 1;
  int j;
  VERIFY( if( i<0 ) goto not_enough_stack; )
  VERIFY( if( NeedStack(p, p->tos+2) ) goto no_mem; )
  for(j=i; j<=p->tos; j++){
    if( (aStack[j].flags & STK_Null)==0 ){
      if( Stringify(p, j) ) goto no_mem;
    }
  }
  zStack[p->tos+1] = 0;
  if( xCallback!=0 ){
    if( xCallback(pArg, pOp->p1, &zStack[i], p->azColName)!=0 ){
      rc = SQLITE_ABORT;
    }
  }
  PopStack(p, pOp->p1);
  break;
}

/* Opcode: Concat P1 P2 P3
**
** Look at the first P1 elements of the stack.  Append them all 
** together with the lowest element first.  Use P3 as a separator.  
** Put the result on the top of the stack.  The original P1 elements
** are popped from the stack if P2==0 and retained if P2==1.
**
** If P3 is NULL, then use no separator.  When P1==1, this routine
** makes a copy of the top stack element into memory obtained
** from sqliteMalloc().
*/
case OP_Concat: {
  char *zNew;
  int nByte;
  int nField;
  int i, j;
  char *zSep;
  int nSep;

  nField = pOp->p1;
  zSep = pOp->p3;
  if( zSep==0 ) zSep = "";
  nSep = strlen(zSep);
  VERIFY( if( p->tos+1<nField ) goto not_enough_stack; )
  nByte = 1 - nSep;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( aStack[i].flags & STK_Null ){
      nByte += nSep;
    }else{
      if( Stringify(p, i) ) goto no_mem;
      nByte += aStack[i].n - 1 + nSep;
    }
  }
  zNew = sqliteMalloc( nByte );
  if( zNew==0 ) goto no_mem;
  j = 0;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( (aStack[i].flags & STK_Null)==0 ){
      memcpy(&zNew[j], zStack[i], aStack[i].n-1);
      j += aStack[i].n-1;
    }
    if( nSep>0 && i<p->tos ){
      memcpy(&zNew[j], zSep, nSep);
      j += nSep;
    }
  }
  zNew[j] = 0;
  if( pOp->p2==0 ) PopStack(p, nField);
  VERIFY( NeedStack(p, p->tos+1); )
  p->tos++;
  aStack[p->tos].n = nByte;
  aStack[p->tos].flags = STK_Str|STK_Dyn;
  zStack[p->tos] = zNew;
  break;
}

/* Opcode: Add * * *
**
** Pop the top two elements from the stack, add them together,
** and push the result back onto the stack.  If either element
** is a string then it is converted to a double using the atof()
** function before the addition.
*/
/* Opcode: Multiply * * *
**
** Pop the top two elements from the stack, multiply them together,
** and push the result back onto the stack.  If either element
** is a string then it is converted to a double using the atof()
** function before the multiplication.
*/
/* Opcode: Subtract * * *
**
** Pop the top two elements from the stack, subtract the
** first (what was on top of the stack) from the second (the
** next on stack)
** and push the result back onto the stack.  If either element
** is a string then it is converted to a double using the atof()
** function before the subtraction.
*/
/* Opcode: Divide * * *
**
** Pop the top two elements from the stack, divide the
** first (what was on top of the stack) from the second (the
** next on stack)
** and push the result back onto the stack.  If either element
** is a string then it is converted to a double using the atof()
** function before the division.  Division by zero returns NULL.
*/
case OP_Add:
case OP_Subtract:
case OP_Multiply:
case OP_Divide: {
  int tos = p->tos;
  int nos = tos - 1;
  VERIFY( if( nos<0 ) goto not_enough_stack; )
  if( (aStack[tos].flags & aStack[nos].flags & STK_Int)==STK_Int ){
    int a, b;
    a = aStack[tos].i;
    b = aStack[nos].i;
    switch( pOp->opcode ){
      case OP_Add:         b += a;       break;
      case OP_Subtract:    b -= a;       break;
      case OP_Multiply:    b *= a;       break;
      default: {
        if( a==0 ) goto divide_by_zero;
        b /= a;
        break;
      }
    }
    POPSTACK;
    Release(p, nos);
    aStack[nos].i = b;
    aStack[nos].flags = STK_Int;
  }else{
    double a, b;
    Realify(p, tos);
    Realify(p, nos);
    a = aStack[tos].r;
    b = aStack[nos].r;
    switch( pOp->opcode ){
      case OP_Add:         b += a;       break;
      case OP_Subtract:    b -= a;       break;
      case OP_Multiply:    b *= a;       break;
      default: {
        if( a==0.0 ) goto divide_by_zero;
        b /= a;
        break;
      }
    }
    POPSTACK;
    Release(p, nos);
    aStack[nos].r = b;
    aStack[nos].flags = STK_Real;
  }
  break;

divide_by_zero:
  PopStack(p, 2);
  p->tos = nos;
  aStack[nos].flags = STK_Null;
  break;
}

/* Opcode: Max * * *
**
** Pop the top two elements from the stack then push back the
** largest of the two.
*/
case OP_Max: {
  int tos = p->tos;
  int nos = tos - 1;
  int ft, fn;
  int copy = 0;
  VERIFY( if( nos<0 ) goto not_enough_stack; )
  ft = aStack[tos].flags;
  fn = aStack[nos].flags;
  if( fn & STK_Null ){
    copy = 1;
  }else if( (ft & fn & STK_Int)==STK_Int ){
    copy = aStack[nos].i<aStack[tos].i;
  }else if( ( (ft|fn) & (STK_Int|STK_Real) ) !=0 ){
    Realify(p, tos);
    Realify(p, nos);
    copy = aStack[tos].r>aStack[nos].r;
  }else{
    if( Stringify(p, tos) || Stringify(p, nos) ) goto no_mem;
    copy = sqliteCompare(zStack[tos],zStack[nos])>0;
  }
  if( copy ){
    Release(p, nos);
    aStack[nos] = aStack[tos];
    zStack[nos] = zStack[tos];
    zStack[tos] = 0;
    aStack[tos].flags = 0;
  }else{
    Release(p, tos);
  }
  p->tos = nos;
  break;
}

/* Opcode: Min * * *
**
** Pop the top two elements from the stack then push back the
** smaller of the two. 
*/
case OP_Min: {
  int tos = p->tos;
  int nos = tos - 1;
  int ft, fn;
  int copy = 0;
  VERIFY( if( nos<0 ) goto not_enough_stack; )
  ft = aStack[tos].flags;
  fn = aStack[nos].flags;
  if( fn & STK_Null ){
    copy = 1;
  }else if( ft & STK_Null ){
    copy = 0;
  }else if( (ft & fn & STK_Int)==STK_Int ){
    copy = aStack[nos].i>aStack[tos].i;
  }else if( ( (ft|fn) & (STK_Int|STK_Real) ) !=0 ){
    Realify(p, tos);
    Realify(p, nos);
    copy = aStack[tos].r<aStack[nos].r;
  }else{
    if( Stringify(p, tos) || Stringify(p, nos) ) goto no_mem;
    copy = sqliteCompare(zStack[tos],zStack[nos])<0;
  }
  if( copy ){
    Release(p, nos);
    aStack[nos] = aStack[tos];
    zStack[nos] = zStack[tos];
    zStack[tos] = 0;
    aStack[tos].flags = 0;
  }else{
    Release(p, tos);
  }
  p->tos = nos;
  break;
}

/* Opcode: AddImm  P1 * *
** 
** Add the value P1 to whatever is on top of the stack.
*/
case OP_AddImm: {
  int tos = p->tos;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  Integerify(p, tos);
  aStack[tos].i += pOp->p1;
  break;
}

/* Opcode: Eq * P2 *
**
** Pop the top two elements from the stack.  If they are equal, then
** jump to instruction P2.  Otherwise, continue to the next instruction.
*/
/* Opcode: Ne * P2 *
**
** Pop the top two elements from the stack.  If they are not equal, then
** jump to instruction P2.  Otherwise, continue to the next instruction.
*/
/* Opcode: Lt * P2 *
**
** Pop the top two elements from the stack.  If second element (the
** next on stack) is less than the first (the top of stack), then
** jump to instruction P2.  Otherwise, continue to the next instruction.
** In other words, jump if NOS<TOS.
*/
/* Opcode: Le * P2 *
**
** Pop the top two elements from the stack.  If second element (the
** next on stack) is less than or equal to the first (the top of stack),
** then jump to instruction P2. In other words, jump if NOS<=TOS.
*/
/* Opcode: Gt * P2 *
**
** Pop the top two elements from the stack.  If second element (the
** next on stack) is greater than the first (the top of stack),
** then jump to instruction P2. In other words, jump if NOS>TOS.
*/
/* Opcode: Ge * P2 *
**
** Pop the top two elements from the stack.  If second element (the next
** on stack) is greater than or equal to the first (the top of stack),
** then jump to instruction P2. In other words, jump if NOS>=TOS.
*/
case OP_Eq:
case OP_Ne:
case OP_Lt:
case OP_Le:
case OP_Gt:
case OP_Ge: {
  int tos = p->tos;
  int nos = tos - 1;
  int c;
  int ft, fn;
  VERIFY( if( nos<0 ) goto not_enough_stack; )
  ft = aStack[tos].flags;
  fn = aStack[nos].flags;
  if( (ft & fn)==STK_Int ){
    c = aStack[nos].i - aStack[tos].i;
  }else{
    if( Stringify(p, tos) || Stringify(p, nos) ) goto no_mem;
    c = sqliteCompare(zStack[nos], zStack[tos]);
  }
  switch( pOp->opcode ){
    case OP_Eq:    c = c==0;     break;
    case OP_Ne:    c = c!=0;     break;
    case OP_Lt:    c = c<0;      break;
    case OP_Le:    c = c<=0;     break;
    case OP_Gt:    c = c>0;      break;
    default:       c = c>=0;     break;
  }
  POPSTACK;
  POPSTACK;
  if( c ) pc = pOp->p2-1;
  break;
}

/* Opcode: Like P1 P2 *
**
** Pop the top two elements from the stack.  The top-most is a
** "like" pattern -- the right operand of the SQL "LIKE" operator.
** The lower element is the string to compare against the like
** pattern.  Jump to P2 if the two compare, and fall through without
** jumping if they do not.  The '%' in the top-most element matches
** any sequence of zero or more characters in the lower element.  The
** '_' character in the topmost matches any single character of the
** lower element.  Case is ignored for this comparison.
**
** If P1 is not zero, the sense of the test is inverted and we
** have a "NOT LIKE" operator.  The jump is made if the two values
** are different.
*/
case OP_Like: {
  int tos = p->tos;
  int nos = tos - 1;
  int c;
  VERIFY( if( nos<0 ) goto not_enough_stack; )
  if( Stringify(p, tos) || Stringify(p, nos) ) goto no_mem;
  c = sqliteLikeCompare(zStack[tos], zStack[nos]);
  POPSTACK;
  POPSTACK;
  if( pOp->p1 ) c = !c;
  if( c ) pc = pOp->p2-1;
  break;
}

/* Opcode: Glob P1 P2 *
**
** Pop the top two elements from the stack.  The top-most is a
** "glob" pattern.  The lower element is the string to compare 
** against the glob pattern.
**
** Jump to P2 if the two compare, and fall through without
** jumping if they do not.  The '*' in the top-most element matches
** any sequence of zero or more characters in the lower element.  The
** '?' character in the topmost matches any single character of the
** lower element.  [...] matches a range of characters.  [^...]
** matches any character not in the range.  Case is significant
** for globs.
**
** If P1 is not zero, the sense of the test is inverted and we
** have a "NOT GLOB" operator.  The jump is made if the two values
** are different.
*/
case OP_Glob: {
  int tos = p->tos;
  int nos = tos - 1;
  int c;
  VERIFY( if( nos<0 ) goto not_enough_stack; )
  if( Stringify(p, tos) || Stringify(p, nos) ) goto no_mem;
  c = sqliteGlobCompare(zStack[tos], zStack[nos]);
  POPSTACK;
  POPSTACK;
  if( pOp->p1 ) c = !c;
  if( c ) pc = pOp->p2-1;
  break;
}

/* Opcode: And * * *
**
** Pop two values off the stack.  Take the logical AND of the
** two values and push the resulting boolean value back onto the
** stack. 
*/
/* Opcode: Or * * *
**
** Pop two values off the stack.  Take the logical OR of the
** two values and push the resulting boolean value back onto the
** stack. 
*/
case OP_And:
case OP_Or: {
  int tos = p->tos;
  int nos = tos - 1;
  int c;
  VERIFY( if( nos<0 ) goto not_enough_stack; )
  Integerify(p, tos);
  Integerify(p, nos);
  if( pOp->opcode==OP_And ){
    c = aStack[tos].i && aStack[nos].i;
  }else{
    c = aStack[tos].i || aStack[nos].i;
  }
  POPSTACK;
  Release(p, nos);     
  aStack[nos].i = c;
  aStack[nos].flags = STK_Int;
  break;
}

/* Opcode: Negative * * *
**
** Treat the top of the stack as a numeric quantity.  Replace it
** with its additive inverse.
*/
case OP_Negative: {
  int tos = p->tos;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( aStack[tos].flags & STK_Real ){
    Release(p, tos);
    aStack[tos].r = -aStack[tos].r;
    aStack[tos].flags = STK_Real;
  }else if( aStack[tos].flags & STK_Int ){
    Release(p, tos);
    aStack[tos].i = -aStack[tos].i;
    aStack[tos].flags = STK_Int;
  }else{
    Realify(p, tos);
    Release(p, tos);
    aStack[tos].r = -aStack[tos].r;
    aStack[tos].flags = STK_Real;
  }
  break;
}

/* Opcode: Not * * *
**
** Interpret the top of the stack as a boolean value.  Replace it
** with its complement.
*/
case OP_Not: {
  int tos = p->tos;
  VERIFY( if( p->tos<0 ) goto not_enough_stack; )
  Integerify(p, tos);
  Release(p, tos);
  aStack[tos].i = !aStack[tos].i;
  aStack[tos].flags = STK_Int;
  break;
}

/* Opcode: Noop * * *
**
** Do nothing.  This instruction is often useful as a jump
** destination.
*/
case OP_Noop: {
  break;
}

/* Opcode: If * P2 *
**
** Pop a single boolean from the stack.  If the boolean popped is
** true, then jump to p2.  Otherwise continue to the next instruction.
** An integer is false if zero and true otherwise.  A string is
** false if it has zero length and true otherwise.
*/
case OP_If: {
  int c;
  VERIFY( if( p->tos<0 ) goto not_enough_stack; )
  Integerify(p, p->tos);
  c = aStack[p->tos].i;
  POPSTACK;
  if( c ) pc = pOp->p2-1;
  break;
}

/* Opcode: IsNull * P2 *
**
** Pop a single value from the stack.  If the value popped is NULL
** then jump to p2.  Otherwise continue to the next 
** instruction.
*/
case OP_IsNull: {
  int c;
  VERIFY( if( p->tos<0 ) goto not_enough_stack; )
  c = (aStack[p->tos].flags & STK_Null)!=0;
  POPSTACK;
  if( c ) pc = pOp->p2-1;
  break;
}

/* Opcode: NotNull * P2 *
**
** Pop a single value from the stack.  If the value popped is not an
** empty string, then jump to p2.  Otherwise continue to the next 
** instruction.
*/
case OP_NotNull: {
  int c;
  VERIFY( if( p->tos<0 ) goto not_enough_stack; )
  c = (aStack[p->tos].flags & STK_Null)==0;
  POPSTACK;
  if( c ) pc = pOp->p2-1;
  break;
}

/* Opcode: MakeRecord P1 * *
**
** Convert the top P1 entries of the stack into a single entry
** suitable for use as a data record in a database table.  To do this
** all entries (except NULLs) are converted to strings and 
** concatenated.  The null-terminators are included on all string
** except for NULL columns which are represented by zero bytes.
** The lowest entry
** on the stack is the first in the concatenation and the top of
** the stack is the last.  After all columns are concatenated, an
** index header is added.  The index header consists of P1 16-bit integers
** which hold the offset of the beginning of each column data from the
** beginning of the completed record including the header.
**
** The Column opcode is used to unpack a record manufactured with
** the opcode.
*/
case OP_MakeRecord: {
  char *zNewRecord;
  int nByte;
  int nField;
  int i, j;
  u16 addr;

  nField = pOp->p1;
  VERIFY( if( p->tos+1<nField ) goto not_enough_stack; )
  nByte = 0;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( (aStack[i].flags & STK_Null)==0 ){
      if( Stringify(p, i) ) goto no_mem;
      nByte += aStack[i].n;
    }
  }
  nByte += sizeof(addr)*nField;
  if( nByte>MAX_BYTES_PER_ROW ){
    rc = SQLITE_TOOBIG;
    goto abort_due_to_error;
  }
  zNewRecord = sqliteMalloc( nByte );
  if( zNewRecord==0 ) goto no_mem;
  j = 0;
  addr = sizeof(addr)*nField;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    memcpy(&zNewRecord[j], (char*)&addr, sizeof(addr));
    j += sizeof(addr);
    if( (aStack[i].flags & STK_Null)==0 ){
      addr += aStack[i].n;
    }
  }
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( (aStack[i].flags & STK_Null)==0 ){
      memcpy(&zNewRecord[j], zStack[i], aStack[i].n);
      j += aStack[i].n;
    }
  }
  PopStack(p, nField);
  VERIFY( NeedStack(p, p->tos+1); )
  p->tos++;
  aStack[p->tos].n = nByte;
  aStack[p->tos].flags = STK_Str | STK_Dyn;
  zStack[p->tos] = zNewRecord;
  break;
}

/* Opcode: MakeKey P1 P2 *
**
** Convert the top P1 entries of the stack into a single entry suitable
** for use as the key in an index.  The top P1 records are
** converted to strings and merged.  The null-terminators 
** are retained and used as separators.
** The lowest entry in the stack is the first field and the top of the
** stack becomes the last.
**
** If P2 is not zero, then the original entries remain on the stack
** and the new key is pushed on top.  If P2 is zero, the original
** data is popped off the stack first then the new key is pushed
** back in its place.
**
** See also: MakeIdxKey, SortMakeKey
*/
case OP_MakeKey: {
  char *zNewKey;
  int nByte;
  int nField;
  int i, j;

  nField = pOp->p1;
  VERIFY( if( p->tos+1<nField ) goto not_enough_stack; )
  nByte = 0;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( aStack[i].flags & STK_Null ){
      nByte++;
    }else{
      if( Stringify(p, i) ) goto no_mem;
      nByte += aStack[i].n;
    }
  }
  if( nByte+sizeof(u32)>MAX_BYTES_PER_ROW ){
    rc = SQLITE_TOOBIG;
    goto abort_due_to_error;
  }
  zNewKey = sqliteMalloc( nByte );
  if( zNewKey==0 ) goto no_mem;
  j = 0;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( aStack[i].flags & STK_Null ){
      zNewKey[j++] = 0;
    }else{
      memcpy(&zNewKey[j], zStack[i], aStack[i].n);
      j += aStack[i].n;
    }
  }
  if( pOp->p2==0 ) PopStack(p, nField);
  VERIFY( NeedStack(p, p->tos+1); )
  p->tos++;
  aStack[p->tos].n = nByte;
  aStack[p->tos].flags = STK_Str|STK_Dyn;
  zStack[p->tos] = zNewKey;
  break;
}

/* Opcode: MakeIdxKey P1 * *
**
** Convert the top P1 entries of the stack into a single entry suitable
** for use as the key in an index.  In addition, take one additional integer
** off of the stack, treat that integer as a four-byte record number, and
** append the four bytes to the key.  Thus a total of P1+1 entries are
** popped from the stack for this instruction and a single entry is pushed
** back.  The first P1 entries that are popped are strings and the last
** entry (the lowest on the stack) is an integer record number.
**
** The converstion of the first P1 string entries occurs just like in
** MakeKey.  Each entry is separated from the others by a null.
** The entire concatenation is null-terminated.  The lowest entry
** in the stack is the first field and the top of the stack becomes the
** last.
**
** See also:  MakeKey, SortMakeKey
*/
case OP_MakeIdxKey: {
  char *zNewKey;
  int nByte;
  int nField;
  int i, j;
  u32 iKey;

  nField = pOp->p1;
  VERIFY( if( p->tos+1<nField ) goto not_enough_stack; )
  nByte = sizeof(u32);
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( aStack[i].flags & STK_Null ){
      nByte++;
    }else{
      if( Stringify(p, i) ) goto no_mem;
      nByte += aStack[i].n;
    }
  }
  if( nByte>MAX_BYTES_PER_ROW ){
    rc = SQLITE_TOOBIG;
    goto abort_due_to_error;
  }
  zNewKey = sqliteMalloc( nByte );
  if( zNewKey==0 ) goto no_mem;
  j = 0;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( aStack[i].flags & STK_Null ){
      zNewKey[j++] = 0;
    }else{
      memcpy(&zNewKey[j], zStack[i], aStack[i].n);
      j += aStack[i].n;
    }
  }
  Integerify(p, p->tos-nField);
  iKey = bigEndian(aStack[p->tos-nField].i);
  memcpy(&zNewKey[j], &iKey, sizeof(u32));
  PopStack(p, nField+1);
  VERIFY( NeedStack(p, p->tos+1); )
  p->tos++;
  aStack[p->tos].n = nByte;
  aStack[p->tos].flags = STK_Str|STK_Dyn;
  zStack[p->tos] = zNewKey;
  break;
}

/* Opcode: Transaction * * *
**
** Begin a transaction.  The transaction ends when a Commit or Rollback
** opcode is encountered or whenever there is an execution error that causes
** a script to abort.  A transaction is not ended by a Halt.
**
** A write lock is obtained on the database file when a transaction is
** started.  No other process can read or write the file while the
** transaction is underway.  Starting a transaction also creates a
** rollback journal.
** A transaction must be started before any changes can be made to the
** database.
*/
case OP_Transaction: {
  int busy = 0;
  if( db->pBeTemp ){
    rc = sqliteBtreeBeginTrans(db->pBeTemp);
    if( rc!=SQLITE_OK ){
      goto abort_due_to_error;
    }
  }
  do{
    rc = sqliteBtreeBeginTrans(pBt);
    switch( rc ){
      case SQLITE_BUSY: {
        if( xBusy==0 || (*xBusy)(pBusyArg, "", ++busy)==0 ){
          sqliteSetString(pzErrMsg, sqliteErrStr(rc), 0);
          busy = 0;
        }
        break;
      }
      case SQLITE_OK: {
        busy = 0;
        break;
      }
      default: {
        goto abort_due_to_error;
      }
    }
  }while( busy );
  break;
}

/* Opcode: Commit * * *
**
** Cause all modifications to the database that have been made since the
** last Transaction to actually take effect.  No additional modifications
** are allowed until another transaction is started.  The Commit instruction
** deletes the journal file and releases the write lock on the database.
** A read lock continues to be held if there are still cursors open.
*/
case OP_Commit: {
  if( db->pBeTemp==0 || (rc = sqliteBtreeCommit(db->pBeTemp))==SQLITE_OK ){
    rc = sqliteBtreeCommit(pBt);
  }
  if( rc==SQLITE_OK ){
    sqliteCommitInternalChanges(db);
  }else{
    sqliteRollbackInternalChanges(db);
  }
  break;
}

/* Opcode: Rollback * * *
**
** Cause all modifications to the database that have been made since the
** last Transaction to be undone. The database is restored to its state
** before the Transaction opcode was executed.  No additional modifications
** are allowed until another transaction is started.
**
** This instruction automatically closes all cursors and releases both
** the read and write locks on the database.
*/
case OP_Rollback: {
  if( db->pBeTemp ){
    sqliteBtreeRollback(db->pBeTemp);
  }
  rc = sqliteBtreeRollback(pBt);
  sqliteRollbackInternalChanges(db);
  break;
}

/* Opcode: ReadCookie * * *
**
** Read the schema cookie from the database file and push it onto the
** stack.  The schema cookie is an integer that is used like a version
** number for the database schema.  Everytime the schema changes, the
** cookie changes to a new random value.  This opcode is used during
** initialization to read the initial cookie value so that subsequent
** database accesses can verify that the cookie has not changed.
**
** There must be a read-lock on the database (either a transaction
** must be started or there must be an open cursor) before
** executing this instruction.
*/
case OP_ReadCookie: {
  int i = ++p->tos;
  int aMeta[SQLITE_N_BTREE_META];
  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  rc = sqliteBtreeGetMeta(pBt, aMeta);
  aStack[i].i = aMeta[1];
  aStack[i].flags = STK_Int;
  break;
}

/* Opcode: SetCookie P1 * *
**
** This operation changes the value of the schema cookie on the database.
** The new value is P1.
**
** The schema cookie changes its value whenever the database schema changes.
** That way, other processes can recognize when the schema has changed
** and reread it.
**
** A transaction must be started before executing this opcode.
*/
case OP_SetCookie: {
  int aMeta[SQLITE_N_BTREE_META];
  rc = sqliteBtreeGetMeta(pBt, aMeta);
  if( rc==SQLITE_OK ){
    aMeta[1] = pOp->p1;
    rc = sqliteBtreeUpdateMeta(pBt, aMeta);
  }
  break;
}

/* Opcode: VerifyCookie P1 * *
**
** Check the current value of the schema cookie and make sure it is
** equal to P1.  If it is not, abort with an SQLITE_SCHEMA error.
**
** The cookie changes its value whenever the database schema changes.
** This operation is used to detect when that the cookie has changed
** and that the current process needs to reread the schema.
**
** Either a transaction needs to have been started or an OP_Open needs
** to be executed (to establish a read lock) before this opcode is
** invoked.
*/
case OP_VerifyCookie: {
  int aMeta[SQLITE_N_BTREE_META];
  rc = sqliteBtreeGetMeta(pBt, aMeta);
  if( rc==SQLITE_OK && aMeta[1]!=pOp->p1 ){
    sqliteSetString(pzErrMsg, "database schema has changed", 0);
    rc = SQLITE_SCHEMA;
  }
  break;
}

/* Opcode: Open P1 P2 P3
**
** Open a read-only cursor for the database table whose root page is
** P2 in the main database file.  Give the new cursor an identifier
** of P1.  The P1 values need not be contiguous but all P1 values
** should be small integers.  It is an error for P1 to be negative.
**
** If P2==0 then take the root page number from the top of the stack.
**
** There will be a read lock on the database whenever there is an
** open cursor.  If the database was unlocked prior to this instruction
** then a read lock is acquired as part of this instruction.  A read
** lock allows other processes to read the database but prohibits
** any other process from modifying the database.  The read lock is
** released when all cursors are closed.  If this instruction attempts
** to get a read lock but fails, the script terminates with an
** SQLITE_BUSY error code.
**
** The P3 value is the name of the table or index being opened.
** The P3 value is not actually used by this opcode and may be
** omitted.  But the code generator usually inserts the index or
** table name into P3 to make the code easier to read.
**
** See also OpenAux and OpenWrite.
*/
/* Opcode: OpenAux P1 P2 P3
**
** Open a read-only cursor in the auxiliary table set.  This opcode
** works exactly like OP_Open except that it opens the cursor on the
** auxiliary table set (the file used to store tables created using
** CREATE TEMPORARY TABLE) instead of in the main database file.
** See OP_Open for additional information.
*/
/* Opcode: OpenWrite P1 P2 P3
**
** Open a read/write cursor named P1 on the table or index whose root
** page is P2.  If P2==0 then take the root page number from the stack.
**
** This instruction works just like Open except that it opens the cursor
** in read/write mode.  For a given table, there can be one or more read-only
** cursors or a single read/write cursor but not both.
**
** See also OpWrAux.
*/
/* Opcode: OpenWrAux P1 P2 P3
**
** Open a read/write cursor in the auxiliary table set.  This opcode works
** just like OpenWrite except that the auxiliary table set (the file used
** to store tables created using CREATE TEMPORARY TABLE) is used in place
** of the main database file.
*/
case OP_OpenAux:
case OP_OpenWrAux:
case OP_OpenWrite:
case OP_Open: {
  int busy = 0;
  int i = pOp->p1;
  int tos = p->tos;
  int p2 = pOp->p2;
  int wrFlag;
  Btree *pX;
  switch( pOp->opcode ){
    case OP_Open:        wrFlag = 0;  pX = pBt;          break;
    case OP_OpenWrite:   wrFlag = 1;  pX = pBt;          break;
    case OP_OpenAux:     wrFlag = 0;  pX = db->pBeTemp;  break;
    case OP_OpenWrAux:   wrFlag = 1;  pX = db->pBeTemp;  break;
  }
  assert( pX!=0 );
  if( p2<=0 ){
    if( tos<0 ) goto not_enough_stack;
    Integerify(p, tos);
    p2 = p->aStack[tos].i;
    POPSTACK;
    if( p2<2 ){
      sqliteSetString(pzErrMsg, "root page number less than 2", 0);
      rc = SQLITE_INTERNAL;
      goto cleanup;
    }
  }
  VERIFY( if( i<0 ) goto bad_instruction; )
  if( i>=p->nCursor ){
    int j;
    p->aCsr = sqliteRealloc( p->aCsr, (i+1)*sizeof(Cursor) );
    if( p->aCsr==0 ){ p->nCursor = 0; goto no_mem; }
    for(j=p->nCursor; j<=i; j++){
      memset(&p->aCsr[j], 0, sizeof(Cursor));
    }
    p->nCursor = i+1;
  }
  cleanupCursor(&p->aCsr[i]);
  memset(&p->aCsr[i], 0, sizeof(Cursor));
  do{
    rc = sqliteBtreeCursor(pX, p2, wrFlag, &p->aCsr[i].pCursor);
    switch( rc ){
      case SQLITE_BUSY: {
        if( xBusy==0 || (*xBusy)(pBusyArg, pOp->p3, ++busy)==0 ){
          sqliteSetString(pzErrMsg, sqliteErrStr(rc), 0);
          busy = 0;
        }
        break;
      }
      case SQLITE_OK: {
        busy = 0;
        break;
      }
      default: {
        goto abort_due_to_error;
      }
    }
  }while( busy );
  break;
}

/* Opcode: OpenTemp P1 * *
**
** Open a new cursor that points to a table in a temporary database
** file.  The temporary file is opened read/write even if the main
** database is read-only.  The temporary file is deleted when the
** cursor is closed.
**
** This opcode is used for tables that exist for the duration of a single
** SQL statement only.  Tables created using CREATE TEMPORARY TABLE
** are opened using OP_OpenAux or OP_OpenWrAux.  "Temporary" in the
** context of this opcode means for the duration of a single SQL statement
** whereas "Temporary" in the context of CREATE TABLE means for the duration
** of the connection to the database.  Same word; different meanings.
*/
case OP_OpenTemp: {
  int i = pOp->p1;
  Cursor *pCx;
  VERIFY( if( i<0 ) goto bad_instruction; )
  if( i>=p->nCursor ){
    int j;
    p->aCsr = sqliteRealloc( p->aCsr, (i+1)*sizeof(Cursor) );
    if( p->aCsr==0 ){ p->nCursor = 0; goto no_mem; }
    for(j=p->nCursor; j<=i; j++){
      memset(&p->aCsr[j], 0, sizeof(Cursor));
    }
    p->nCursor = i+1;
  }
  pCx = &p->aCsr[i];
  cleanupCursor(pCx);
  memset(pCx, 0, sizeof(*pCx));
  rc = sqliteBtreeOpen(0, 0, TEMP_PAGES, &pCx->pBt);
  if( rc==SQLITE_OK ){
    rc = sqliteBtreeCursor(pCx->pBt, 2, 1, &pCx->pCursor);
  }
  if( rc==SQLITE_OK ){
    rc = sqliteBtreeBeginTrans(pCx->pBt);
  }
  break;
}

/* Opcode: Close P1 * *
**
** Close a cursor previously opened as P1.  If P1 is not
** currently open, this instruction is a no-op.
*/
case OP_Close: {
  int i = pOp->p1;
  if( i>=0 && i<p->nCursor && p->aCsr[i].pCursor ){
    cleanupCursor(&p->aCsr[i]);
  }
  break;
}

/* Opcode: MoveTo P1 * *
**
** Pop the top of the stack and use its value as a key.  Reposition
** cursor P1 so that it points to an entry with a matching key.  If
** the table contains no record with a matching key, then the cursor
** is left pointing at a nearby record.
**
** See also: Found, NotFound, Distinct
*/
case OP_MoveTo: {
  int i = pOp->p1;
  int tos = p->tos;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( i>=0 && i<p->nCursor && p->aCsr[i].pCursor ){
    int res;
    if( aStack[tos].flags & STK_Int ){
      int iKey = bigEndian(aStack[tos].i);
      sqliteBtreeMoveto(p->aCsr[i].pCursor, 
          (char*)&iKey, sizeof(int), &res);
      p->aCsr[i].lastRecno = aStack[tos].i;
      p->aCsr[i].recnoIsValid = 1;
    }else{
      if( Stringify(p, tos) ) goto no_mem;
      sqliteBtreeMoveto(p->aCsr[i].pCursor, zStack[tos], aStack[tos].n, &res);
      p->aCsr[i].recnoIsValid = 0;
    }
    p->nFetch++;
  }
  POPSTACK;
  break;
}

/* Opcode: Fcnt * * *
**
** Push an integer onto the stack which is the total number of
** MoveTo opcodes that have been executed by this virtual machine.
**
** This instruction is used to implement the special fcnt() function
** in the SQL dialect that SQLite understands.  fcnt() is used for
** testing purposes.
*/
case OP_Fcnt: {
  int i = ++p->tos;
  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  aStack[i].i = p->nFetch;
  aStack[i].flags = STK_Int;
  break;
}

/* Opcode: Distinct P1 P2 *
**
** Use the top of the stack as a key.  If a record with that key does
** not exist in the table of cursor P1, then jump to P2.  If the record
** does already exist, then fall thru.  The cursor is left pointing
** at the record if it exists. The key is not popped from the stack.
**
** This operation is similar to NotFound except that this operation
** does not pop the key from the stack.
**
** See also: Found, NotFound, MoveTo
*/
/* Opcode: Found P1 P2 *
**
** Use the top of the stack as a key.  If a record with that key
** does exist in table of P1, then jump to P2.  If the record
** does not exist, then fall thru.  The cursor is left pointing
** to the record if it exists.  The key is popped from the stack.
**
** See also: Distinct, NotFound, MoveTo
*/
/* Opcode: NotFound P1 P2 *
**
** Use the top of the stack as a key.  If a record with that key
** does not exist in table of P1, then jump to P2.  If the record
** does exist, then fall thru.  The cursor is left pointing to the
** record if it exists.  The key is popped from the stack.
**
** The difference between this operation and Distinct is that
** Distinct does not pop the key from the stack.
**
** See also: Distinct, Found, MoveTo
*/
case OP_Distinct:
case OP_NotFound:
case OP_Found: {
  int i = pOp->p1;
  int tos = p->tos;
  int alreadyExists = 0;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( VERIFY( i>=0 && i<p->nCursor && ) p->aCsr[i].pCursor ){
    int res, rx;
    if( aStack[tos].flags & STK_Int ){
      int iKey = bigEndian(aStack[tos].i);
      rx = sqliteBtreeMoveto(p->aCsr[i].pCursor, 
           (char*)&iKey, sizeof(int), &res);
    }else{
      if( Stringify(p, tos) ) goto no_mem;
      rx = sqliteBtreeMoveto(p->aCsr[i].pCursor,
         zStack[tos], aStack[tos].n, &res);
    }
    alreadyExists = rx==SQLITE_OK && res==0;
  }
  if( pOp->opcode==OP_Found ){
    if( alreadyExists ) pc = pOp->p2 - 1;
  }else{
    if( !alreadyExists ) pc = pOp->p2 - 1;
  }
  if( pOp->opcode!=OP_Distinct ){
    POPSTACK;
  }
  break;
}

/* Opcode: NewRecno P1 * *
**
** Get a new integer record number used as the key to a table.
** The record number is not previously used as a key in the database
** table that cursor P1 points to.  The new record number pushed 
** onto the stack.
*/
case OP_NewRecno: {
  int i = pOp->p1;
  int v = 0;
  if( VERIFY( i<0 || i>=p->nCursor || ) p->aCsr[i].pCursor==0 ){
    v = 0;
  }else{
    /* A probablistic algorithm is used to locate an unused rowid.
    ** We select a rowid at random and see if it exists in the table.
    ** If it does not exist, we have succeeded.  If the random rowid
    ** does exist, we select a new one and try again, up to 1000 times.
    **
    ** For a table with less than 2 billion entries, the probability
    ** of not finding a unused rowid is about 1.0e-300.  This is a 
    ** non-zero probability, but it is still vanishingly small and should
    ** never cause a problem.  You are much, much more likely to have a
    ** hardware failure than for this algorithm to fail.
    **
    ** To promote locality of reference for repetitive inserts, the
    ** first few attempts at chosing a rowid pick values just a little
    ** larger than the previous rowid.  This has been shown experimentally
    ** to double the speed of the COPY operation.
    */
    int res, rx, cnt, x;
    cnt = 0;
    v = db->nextRowid;
    do{
      if( cnt>5 ){
        v = sqliteRandomInteger();
      }else{
        v += sqliteRandomByte() + 1;
      }
      if( v==0 ) continue;
      x = bigEndian(v);
      rx = sqliteBtreeMoveto(p->aCsr[i].pCursor, &x, sizeof(int), &res);
      cnt++;
    }while( cnt<1000 && rx==SQLITE_OK && res==0 );
    db->nextRowid = v;
    if( rx==SQLITE_OK && res==0 ){
      rc = SQLITE_FULL;
      goto abort_due_to_error;
    }
  }
  VERIFY( NeedStack(p, p->tos+1); )
  p->tos++;
  aStack[p->tos].i = v;
  aStack[p->tos].flags = STK_Int;
  break;
}

/* Opcode: Put P1 * *
**
** Write an entry into the database file P1.  A new entry is
** created if it doesn't already exist or the data for an existing
** entry is overwritten.  The data is the value on the top of the
** stack.  The key is the next value down on the stack.  The stack
** is popped twice by this instruction.
*/
case OP_Put: {
  int tos = p->tos;
  int nos = p->tos-1;
  int i = pOp->p1;
  VERIFY( if( nos<0 ) goto not_enough_stack; )
  if( VERIFY( i>=0 && i<p->nCursor && ) p->aCsr[i].pCursor!=0 ){
    char *zKey;
    int nKey, iKey;
    if( (aStack[nos].flags & STK_Int)==0 ){
      if( Stringify(p, nos) ) goto no_mem;
      nKey = aStack[nos].n;
      zKey = zStack[nos];
    }else{
      nKey = sizeof(int);
      iKey = bigEndian(aStack[nos].i);
      zKey = (char*)&iKey;
    }
    rc = sqliteBtreeInsert(p->aCsr[i].pCursor, zKey, nKey,
                        zStack[tos], aStack[tos].n);
  }
  POPSTACK;
  POPSTACK;
  break;
}

/* Opcode: Delete P1 * *
**
** Delete the record at which the P1 cursor is currently pointing.
**
** The cursor will be left pointing at either the next or the previous
** record in the table. If it is left pointing at the next record, then
** the next Next instruction will be a no-op.  Hence it is OK to delete
** a record from within an Next loop.
*/
case OP_Delete: {
  int i = pOp->p1;
  if( VERIFY( i>=0 && i<p->nCursor && ) p->aCsr[i].pCursor!=0 ){
    rc = sqliteBtreeDelete(p->aCsr[i].pCursor);
  }
  break;
}

/* Opcode: KeyAsData P1 P2 *
**
** Turn the key-as-data mode for cursor P1 either on (if P2==1) or
** off (if P2==0).  In key-as-data mode, the Field opcode pulls
** data off of the key rather than the data.  This is useful for
** processing compound selects.
*/
case OP_KeyAsData: {
  int i = pOp->p1;
  if( VERIFY( i>=0 && i<p->nCursor && ) p->aCsr[i].pCursor!=0 ){
    p->aCsr[i].keyAsData = pOp->p2;
  }
  break;
}

/* Opcode: Column P1 P2 *
**
** Interpret the data that cursor P1 points to as
** a structure built using the MakeRecord instruction.
** (See the MakeRecord opcode for additional information about
** the format of the data.)
** Push onto the stack the value of the P2-th column contained
** in the data.
**
** If the KeyAsData opcode has previously executed on this cursor,
** then the field might be extracted from the key rather than the
** data.
*/
case OP_Column: {
  int amt, offset, nCol, payloadSize;
  u16 aHdr[10];
  static const int mxHdr = sizeof(aHdr)/sizeof(aHdr[0]);
  int i = pOp->p1;
  int p2 = pOp->p2;
  int tos = p->tos+1;
  BtCursor *pCrsr;
  char *z;

  VERIFY( if( NeedStack(p, tos+1) ) goto no_mem; )
  if( VERIFY( i>=0 && i<p->nCursor && ) (pCrsr = p->aCsr[i].pCursor)!=0 ){
    int (*xSize)(BtCursor*, int*);
    int (*xRead)(BtCursor*, int, int, char*);

    /* Use different access functions depending on whether the information
    ** is coming from the key or the data of the record.
    */
    if( p->aCsr[i].keyAsData ){
      xSize = sqliteBtreeKeySize;
      xRead = sqliteBtreeKey;
    }else{
      xSize = sqliteBtreeDataSize;
      xRead = sqliteBtreeData;
    }

    /* 
    ** The code is complicated by efforts to minimize the number
    ** of invocations of xRead() since that call can be expensive.
    ** For the common case where P2 is small, xRead() is invoked
    ** twice.  For larger values of P2, it has to be called
    ** three times.
    */
    (*xSize)(pCrsr, &payloadSize);
    if( payloadSize < sizeof(aHdr[0])*(p2+1) ){
      rc = SQLITE_CORRUPT;
      goto abort_due_to_error;
    }
    if( p2+1<mxHdr ){
      (*xRead)(pCrsr, 0, sizeof(aHdr[0])*(p2+2), (char*)aHdr);
      nCol = aHdr[0];
      nCol /= sizeof(aHdr[0]);
      offset = aHdr[p2];
      if( p2 == nCol-1 ){
        amt = payloadSize - offset;
      }else{
        amt = aHdr[p2+1] - offset;
      }
    }else{
      sqliteBtreeData(pCrsr, 0, sizeof(aHdr[0]), (char*)aHdr);
      nCol = aHdr[0]/sizeof(aHdr[0]);
      if( p2 == nCol-1 ){
        (*xRead)(pCrsr, sizeof(aHdr[0])*p2, sizeof(aHdr[0]), (char*)aHdr);
        offset = aHdr[0];
        amt = payloadSize - offset;
      }else{
        (*xRead)(pCrsr, sizeof(aHdr[0])*p2, sizeof(aHdr[0])*2, (char*)aHdr);
        offset = aHdr[0];
        amt = aHdr[1] - offset;
      }
    }
    if( payloadSize < nCol || amt<0 || offset<0 ){
      rc = SQLITE_CORRUPT;
      goto abort_due_to_error;
    }

    /* amt and offset now hold the offset to the start of data and the
    ** amount of data.  Go get the data and put it on the stack.
    */
    if( amt==0 ){
      aStack[tos].flags = STK_Null;
    }else{
      z = sqliteMalloc( amt );
      if( z==0 ) goto no_mem;
      (*xRead)(pCrsr, offset, amt, z);
      aStack[tos].flags = STK_Str | STK_Dyn;
      zStack[tos] = z;
      aStack[tos].n = amt;
    }
    p->tos = tos;
  }
  break;
}

/* Opcode: Recno P1 * *
**
** Push onto the stack an integer which is the first 4 bytes of the
** the key to the current entry in a sequential scan of the database
** file P1.  The sequential scan should have been started using the 
** Next opcode.
*/
case OP_Recno: {
  int i = pOp->p1;
  int tos = ++p->tos;
  BtCursor *pCrsr;

  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  if( VERIFY( i>=0 && i<p->nCursor && ) (pCrsr = p->aCsr[i].pCursor)!=0 ){
    int v;
    if( p->aCsr[i].recnoIsValid ){
      v = p->aCsr[i].lastRecno;
    }else{
      sqliteBtreeKey(pCrsr, 0, sizeof(u32), (char*)&v);
      v = bigEndian(v);
    }
    aStack[tos].i = v;
    aStack[tos].flags = STK_Int;
  }
  break;
}

/* Opcode: FullKey P1 * *
**
** Extract the complete key from the record that cursor P1 is currently
** pointing to and push the key onto the stack as a string.
**
** Compare this opcode to Recno.  The Recno opcode extracts the first
** 4 bytes of the key and pushes those bytes onto the stack as an
** integer.  This instruction pushes the entire key as a string.
*/
case OP_FullKey: {
  int i = pOp->p1;
  int tos = ++p->tos;
  BtCursor *pCrsr;

  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  VERIFY( if( !p->aCsr[i].keyAsData ) goto bad_instruction; )
  if( VERIFY( i>=0 && i<p->nCursor && ) (pCrsr = p->aCsr[i].pCursor)!=0 ){
    int amt;
    char *z;

    sqliteBtreeKeySize(pCrsr, &amt);
    if( amt<=0 ){
      rc = SQLITE_CORRUPT;
      goto abort_due_to_error;
    }
    z = sqliteMalloc( amt );
    sqliteBtreeKey(pCrsr, 0, amt, z);
    zStack[tos] = z;
    aStack[tos].flags = STK_Str | STK_Dyn;
    aStack[tos].n = amt;
  }
  break;
}

/* Opcode: Rewind P1 * *
**
** The next use of the Recno or Column or Next instruction for P1 
** will refer to the first entry in the database file.
*/
case OP_Rewind: {
  int i = pOp->p1;
  BtCursor *pCrsr;

  if( VERIFY( i>=0 && i<p->nCursor && ) (pCrsr = p->aCsr[i].pCursor)!=0 ){
    int res;
    sqliteBtreeFirst(pCrsr, &res);
    p->aCsr[i].atFirst = res==0;
  }
  break;
}

/* Opcode: Next P1 P2 *
**
** Advance cursor P1 so that it points to the next key/data pair in its
** table.  Or, if there are no more key/data pairs, jump to location P2.
*/
case OP_Next: {
  int i = pOp->p1;
  BtCursor *pCrsr;

  if( VERIFY( i>=0 && i<p->nCursor && ) (pCrsr = p->aCsr[i].pCursor)!=0 ){
    if( !p->aCsr[i].atFirst ){
      int res;
      sqliteBtreeNext(pCrsr, &res);
      if( res ){
        pc = pOp->p2 - 1;
      }else{
        p->nFetch++;
      }
    }
    p->aCsr[i].atFirst = 0;
    p->aCsr[i].recnoIsValid = 0;
  }
  break;
}

/* Opcode: BeginIdx P1 * *
**
** Begin searching an index for records with the key found on the
** top of the stack.  The key on the top of the stack should be built
** using the MakeKey opcode.  Subsequent calls to NextIdx will push
** record numbers onto the stack until all records with the same key
** have been returned.
**
** Note that the key for this opcode should be built using MakeKey
** but the key used for PutIdx and DeleteIdx should be built using
** MakeIdxKey.  The difference is that MakeIdxKey adds a 4-bytes
** record number to the end of the key in order to specify a particular
** entry in the index.  MakeKey omits the 4-byte record number.
** The search that this BeginIdx instruction initiates will span all
** entries in the index where the MakeKey generated key matches all
** but the last four bytes of the MakeIdxKey generated key.
*/
case OP_BeginIdx: {
  int i = pOp->p1;
  int tos = p->tos;
  int res, rx;
  Cursor *pCrsr;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( i>=0 && i<p->nCursor && (pCrsr = &p->aCsr[i])->pCursor!=0 ){
    if( Stringify(p, tos) ) goto no_mem;
    if( pCrsr->zKey ) sqliteFree(pCrsr->zKey);
    pCrsr->nKey = aStack[tos].n;
    pCrsr->zKey = sqliteMalloc( pCrsr->nKey+1 );
    if( pCrsr->zKey==0 ) goto no_mem;
    memcpy(pCrsr->zKey, zStack[tos], aStack[tos].n);
    pCrsr->zKey[aStack[tos].n] = 0;
    rx = sqliteBtreeMoveto(pCrsr->pCursor, zStack[tos], aStack[tos].n, &res);
    pCrsr->atFirst = rx==SQLITE_OK && res>0;
    pCrsr->recnoIsValid = 0;
  }
  POPSTACK;
  break;
}

/* Opcode: NextIdx P1 P2 *
**
** The P1 cursor points to an SQL index for which a BeginIdx operation
** has been issued.  This operation retrieves the next record from that
** cursor and verifies that the key on the record minus the last 4 bytes
** matches the key that was pulled from the stack by the BeginIdx instruction.
** If they match, then the last 4 bytes of the key on the record hold a record
** number and that record number is extracted and pushed on the stack.
** If the keys do not match, there is an immediate jump to instruction P2.
*/
case OP_NextIdx: {
  int i = pOp->p1;
  int tos = ++p->tos;
  Cursor *pCrsr;
  BtCursor *pCur;
  int rx, res, size;

  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  zStack[tos] = 0;
  if( VERIFY( i>=0 && i<p->nCursor && ) (pCrsr = &p->aCsr[i])->pCursor!=0 ){
    pCur = pCrsr->pCursor;
    if( pCrsr->atFirst ){
      pCrsr->atFirst = 0;
      res = 0;
    }else{
      rx = sqliteBtreeNext(pCur, &res);
      if( rx!=SQLITE_OK ) goto abort_due_to_error;
    }
    sqliteBtreeKeySize(pCur, &size);
    if( res>0 || size!=pCrsr->nKey+sizeof(u32) ||
      sqliteBtreeKeyCompare(pCur, pCrsr->zKey, pCrsr->nKey, &res)!=SQLITE_OK ||
      res!=0
    ){
      pc = pOp->p2 - 1;
      POPSTACK;
    }else{
      int recno;
      sqliteBtreeKey(pCur, pCrsr->nKey, sizeof(u32), (char*)&recno);
      recno = bigEndian(recno);
      p->aCsr[i].lastRecno = aStack[tos].i = recno;
      p->aCsr[i].recnoIsValid = 1;
      aStack[tos].flags = STK_Int;
    }
  }
  break;
}

/* Opcode: PutIdx P1 P2 P3
**
** The top of the stack hold an SQL index key made using the
** MakeIdxKey instruction.  This opcode writes that key into the
** index P1.  Data for the entry is nil.
**
** If P2==1, then the key must be unique.  If the key is not unique,
** the program aborts with a SQLITE_CONSTRAINT error and the database
** is rolled back.  If P3 is not null, then it because part of the
** error message returned with the SQLITE_CONSTRAINT.
*/
case OP_PutIdx: {
  int i = pOp->p1;
  int tos = p->tos;
  BtCursor *pCrsr;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( VERIFY( i>=0 && i<p->nCursor && ) (pCrsr = p->aCsr[i].pCursor)!=0 ){
    int nKey = aStack[tos].n;
    const char *zKey = zStack[tos];
    if( pOp->p2 ){
      int res, n;
      assert( aStack[tos].n >= 4 );
      rc = sqliteBtreeMoveto(pCrsr, zKey, nKey-4, &res);
      if( rc!=SQLITE_OK ) goto abort_due_to_error;
      while( res!=0 ){
        int c;
        sqliteBtreeKeySize(pCrsr, &n);
        if( n==nKey
           && sqliteBtreeKeyCompare(pCrsr, zKey, nKey-4, &c)==SQLITE_OK
           && c==0
        ){
          rc = SQLITE_CONSTRAINT;
          if( pOp->p3 && pOp->p3[0] ){
            sqliteSetString(pzErrMsg, "duplicate index entry: ", pOp->p3,0);
          }
          goto abort_due_to_error;
        }
        if( res<0 ){
          sqliteBtreeNext(pCrsr, &res);
          res = +1;
        }else{
          break;
        }
      }
    }
    rc = sqliteBtreeInsert(pCrsr, zKey, nKey, "", 0);
  }
  POPSTACK;
  break;
}

/* Opcode: DeleteIdx P1 * *
**
** The top of the stack is an index key built using the MakeIdxKey opcode.
** This opcode removes that entry from the index.
*/
case OP_DeleteIdx: {
  int i = pOp->p1;
  int tos = p->tos;
  BtCursor *pCrsr;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( VERIFY( i>=0 && i<p->nCursor && ) (pCrsr = p->aCsr[i].pCursor)!=0 ){
    int rx, res;
    rx = sqliteBtreeMoveto(pCrsr, zStack[tos], aStack[tos].n, &res);
    if( rx==SQLITE_OK && res==0 ){
      rc = sqliteBtreeDelete(pCrsr);
    }
  }
  POPSTACK;
  break;
}

/* Opcode: Destroy P1 P2 *
**
** Delete an entire database table or index whose root page in the database
** file is given by P1.
**
** The table being destroyed is in the main database file if P2==0.  If
** P2==1 then the table to be clear is in the auxiliary database file
** that is used to store tables create using CREATE TEMPORARY TABLE.
**
** See also: Clear
*/
case OP_Destroy: {
  sqliteBtreeDropTable(pOp->p2 ? db->pBeTemp : pBt, pOp->p1);
  break;
}

/* Opcode: Clear P1 P2 *
**
** Delete all contents of the database table or index whose root page
** in the database file is given by P1.  But, unlike Destroy, do not
** remove the table or index from the database file.
**
** The table being clear is in the main database file if P2==0.  If
** P2==1 then the table to be clear is in the auxiliary database file
** that is used to store tables create using CREATE TEMPORARY TABLE.
**
** See also: Destroy
*/
case OP_Clear: {
  sqliteBtreeClearTable(pOp->p2 ? db->pBeTemp : pBt, pOp->p1);
  break;
}

/* Opcode: CreateTable * P2 P3
**
** Allocate a new table in the main database file if P2==0 or in the
** auxiliary database file if P2==1.  Push the page number
** for the root page of the new table onto the stack.
**
** The root page number is also written to a memory location that P3
** points to.  This is the mechanism is used to write the root page
** number into the parser's internal data structures that describe the
** new table.
**
** See also: CreateIndex
*/
/* Opcode: CreateIndex * P2 P3
**
** This instruction does exactly the same thing as CreateTable.  It
** has a different name for historical reasons.
**
** See also: CreateTable
*/
case OP_CreateIndex:
case OP_CreateTable: {
  int i = ++p->tos;
  int pgno;
  VERIFY( if( NeedStack(p, p->tos) ) goto no_mem; )
  assert( pOp->p3!=0 && pOp->p3dyn==0 );
  rc = sqliteBtreeCreateTable(pOp->p2 ? db->pBeTemp : pBt, &pgno);
  if( rc==SQLITE_OK ){
    aStack[i].i = pgno;
    aStack[i].flags = STK_Int;
    *(u32*)pOp->p3 = pgno;
    pOp->p3 = 0;
  }
  break;
}

/* Opcode: Reorganize P1 * *
**
** Compress, optimize, and tidy up table or index whose root page in the
** database file is P1.
**
** In the current implementation, this is a no-op.
*/
case OP_Reorganize: {
  /* This is currently a no-op */
  break;
}

/* Opcode: ListOpen P1 * *
**
** Open a "List" structure used for temporary storage of integer 
** record numbers.  P1 will server as a handle to this list for future
** interactions.  If another list with the P1 handle is
** already opened, the prior list is closed and a new one opened
** in its place.
*/
case OP_ListOpen: {
  int i = pOp->p1;
  VERIFY( if( i<0 ) goto bad_instruction; )
  if( i>=p->nList ){
    int j;
    p->apList = sqliteRealloc( p->apList, (i+1)*sizeof(Keylist*) );
    if( p->apList==0 ){ p->nList = 0; goto no_mem; }
    for(j=p->nList; j<=i; j++) p->apList[j] = 0;
    p->nList = i+1;
  }else if( p->apList[i] ){
    KeylistFree(p->apList[i]);
    p->apList[i] = 0;
  }
  break;
}

/* Opcode: ListWrite P1 * *
**
** Write the integer on the top of the stack
** into the temporary storage list P1.
*/
case OP_ListWrite: {
  int i = pOp->p1;
  Keylist *pKeylist;
  VERIFY( if( i<0 || i>=p->nList ) goto bad_instruction; )
  VERIFY( if( p->tos<0 ) goto not_enough_stack; )
  pKeylist = p->apList[i];
  if( pKeylist==0 || pKeylist->nUsed>=pKeylist->nKey ){
    pKeylist = sqliteMalloc( sizeof(Keylist)+999*sizeof(pKeylist->aKey[0]) );
    if( pKeylist==0 ) goto no_mem;
    pKeylist->nKey = 1000;
    pKeylist->nRead = 0;
    pKeylist->nUsed = 0;
    pKeylist->pNext = p->apList[i];
    p->apList[i] = pKeylist;
  }
  Integerify(p, p->tos);
  pKeylist->aKey[pKeylist->nUsed++] = aStack[p->tos].i;
  POPSTACK;
  break;
}

/* Opcode: ListRewind P1 * *
**
** Rewind the temporary buffer P1 back to the beginning.
*/
case OP_ListRewind: {
  int i = pOp->p1;
  VERIFY( if( i<0 ) goto bad_instruction; )
  /* This is now a no-op */
  break;
}

/* Opcode: ListRead P1 P2 *
**
** Attempt to read an integer from temporary storage buffer P1
** and push it onto the stack.  If the storage buffer is empty, 
** push nothing but instead jump to P2.
*/
case OP_ListRead: {
  int i = pOp->p1;
  Keylist *pKeylist;
  VERIFY(if( i<0 || i>=p->nList ) goto bad_instruction;)
  pKeylist = p->apList[i];
  if( pKeylist!=0 ){
    VERIFY(
      if( pKeylist->nRead<0 
        || pKeylist->nRead>=pKeylist->nUsed
        || pKeylist->nRead>=pKeylist->nKey ) goto bad_instruction;
    )
    p->tos++;
    if( NeedStack(p, p->tos) ) goto no_mem;
    aStack[p->tos].i = pKeylist->aKey[pKeylist->nRead++];
    aStack[p->tos].flags = STK_Int;
    zStack[p->tos] = 0;
    if( pKeylist->nRead>=pKeylist->nUsed ){
      p->apList[i] = pKeylist->pNext;
      sqliteFree(pKeylist);
    }
  }else{
    pc = pOp->p2 - 1;
  }
  break;
}

/* Opcode: ListClose P1 * *
**
** Close the temporary storage buffer and discard its contents.
*/
case OP_ListClose: {
  int i = pOp->p1;
  VERIFY( if( i<0 ) goto bad_instruction; )
  VERIFY( if( i>=p->nList ) goto bad_instruction; )
  KeylistFree(p->apList[i]);
  p->apList[i] = 0;
  break;
}

/* Opcode: SortOpen P1 * *
**
** Create a new sorter with index P1
*/
case OP_SortOpen: {
  int i = pOp->p1;
  VERIFY( if( i<0 ) goto bad_instruction; )
  if( i>=p->nSort ){
    int j;
    p->apSort = sqliteRealloc( p->apSort, (i+1)*sizeof(Sorter*) );
    if( p->apSort==0 ){ p->nSort = 0; goto no_mem; }
    for(j=p->nSort; j<=i; j++) p->apSort[j] = 0;
    p->nSort = i+1;
  }
  break;
}

/* Opcode: SortPut P1 * *
**
** The TOS is the key and the NOS is the data.  Pop both from the stack
** and put them on the sorter.
*/
case OP_SortPut: {
  int i = pOp->p1;
  int tos = p->tos;
  int nos = tos - 1;
  Sorter *pSorter;
  VERIFY( if( i<0 || i>=p->nSort ) goto bad_instruction; )
  VERIFY( if( tos<1 ) goto not_enough_stack; )
  if( Stringify(p, tos) || Stringify(p, nos) ) goto no_mem;
  pSorter = sqliteMalloc( sizeof(Sorter) );
  if( pSorter==0 ) goto no_mem;
  pSorter->pNext = p->apSort[i];
  p->apSort[i] = pSorter;
  pSorter->nKey = aStack[tos].n;
  pSorter->zKey = zStack[tos];
  pSorter->nData = aStack[nos].n;
  pSorter->pData = zStack[nos];
  aStack[tos].flags = 0;
  aStack[nos].flags = 0;
  zStack[tos] = 0;
  zStack[nos] = 0;
  p->tos -= 2;
  break;
}

/* Opcode: SortMakeRec P1 * *
**
** The top P1 elements are the arguments to a callback.  Form these
** elements into a single data entry that can be stored on a sorter
** using SortPut and later fed to a callback using SortCallback.
*/
case OP_SortMakeRec: {
  char *z;
  char **azArg;
  int nByte;
  int nField;
  int i, j;

  nField = pOp->p1;
  VERIFY( if( p->tos+1<nField ) goto not_enough_stack; )
  nByte = 0;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( (aStack[i].flags & STK_Null)==0 ){
      if( Stringify(p, i) ) goto no_mem;
      nByte += aStack[i].n;
    }
  }
  nByte += sizeof(char*)*(nField+1);
  azArg = sqliteMalloc( nByte );
  if( azArg==0 ) goto no_mem;
  z = (char*)&azArg[nField+1];
  for(j=0, i=p->tos-nField+1; i<=p->tos; i++, j++){
    if( aStack[i].flags & STK_Null ){
      azArg[j] = 0;
    }else{
      azArg[j] = z;
      strcpy(z, zStack[i]);
      z += aStack[i].n;
    }
  }
  PopStack(p, nField);
  VERIFY( NeedStack(p, p->tos+1); )
  p->tos++;
  aStack[p->tos].n = nByte;
  zStack[p->tos] = (char*)azArg;
  aStack[p->tos].flags = STK_Str|STK_Dyn;
  break;
}

/* Opcode: SortMakeKey P1 * P3
**
** Convert the top few entries of the stack into a sort key.  The
** number of stack entries consumed is the number of characters in 
** the string P3.  One character from P3 is prepended to each entry.
** The first character of P3 is prepended to the element lowest in
** the stack and the last character of P3 is appended to the top of
** the stack.  All stack entries are separated by a \000 character
** in the result.  The whole key is terminated by two \000 characters
** in a row.
**
** See also the MakeKey and MakeIdxKey opcodes.
*/
case OP_SortMakeKey: {
  char *zNewKey;
  int nByte;
  int nField;
  int i, j, k;

  nField = strlen(pOp->p3);
  VERIFY( if( p->tos+1<nField ) goto not_enough_stack; )
  nByte = 1;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    if( Stringify(p, i) ) goto no_mem;
    nByte += aStack[i].n+2;
  }
  zNewKey = sqliteMalloc( nByte );
  if( zNewKey==0 ) goto no_mem;
  j = 0;
  k = 0;
  for(i=p->tos-nField+1; i<=p->tos; i++){
    zNewKey[j++] = pOp->p3[k++];
    memcpy(&zNewKey[j], zStack[i], aStack[i].n-1);
    j += aStack[i].n-1;
    zNewKey[j++] = 0;
  }
  zNewKey[j] = 0;
  PopStack(p, nField);
  VERIFY( NeedStack(p, p->tos+1); )
  p->tos++;
  aStack[p->tos].n = nByte;
  aStack[p->tos].flags = STK_Str|STK_Dyn;
  zStack[p->tos] = zNewKey;
  break;
}

/* Opcode: Sort P1 * *
**
** Sort all elements on the given sorter.  The algorithm is a
** mergesort.
*/
case OP_Sort: {
  int j;
  j = pOp->p1;
  VERIFY( if( j<0 ) goto bad_instruction; )
  if( j<p->nSort ){
    int i;
    Sorter *pElem;
    Sorter *apSorter[NSORT];
    for(i=0; i<NSORT; i++){
      apSorter[i] = 0;
    }
    while( p->apSort[j] ){
      pElem = p->apSort[j];
      p->apSort[j] = pElem->pNext;
      pElem->pNext = 0;
      for(i=0; i<NSORT-1; i++){
        if( apSorter[i]==0 ){
          apSorter[i] = pElem;
          break;
        }else{
          pElem = Merge(apSorter[i], pElem);
          apSorter[i] = 0;
        }
      }
      if( i>=NSORT-1 ){
        apSorter[NSORT-1] = Merge(apSorter[NSORT-1],pElem);
      }
    }
    pElem = 0;
    for(i=0; i<NSORT; i++){
      pElem = Merge(apSorter[i], pElem);
    }
    p->apSort[j] = pElem;
  }
  break;
}

/* Opcode: SortNext P1 P2 *
**
** Push the data for the topmost element in the given sorter onto the
** stack, then remove the element from the sorter.
*/
case OP_SortNext: {
  int i = pOp->p1;
  VERIFY( if( i<0 ) goto bad_instruction; )
  if( VERIFY( i<p->nSort && ) p->apSort[i]!=0 ){
    Sorter *pSorter = p->apSort[i];
    p->apSort[i] = pSorter->pNext;
    p->tos++;
    VERIFY( NeedStack(p, p->tos); )
    zStack[p->tos] = pSorter->pData;
    aStack[p->tos].n = pSorter->nData;
    aStack[p->tos].flags = STK_Str|STK_Dyn;
    sqliteFree(pSorter->zKey);
    sqliteFree(pSorter);
  }else{
    pc = pOp->p2 - 1;
  }
  break;
}

#if 0 /* NOT USED */
/* Opcode: SortKey P1 * *
**
** Push the key for the topmost element of the sorter onto the stack.
** But don't change the sorter an any other way.
*/
case OP_SortKey: {
  int i = pOp->p1;
  VERIFY( if( i<0 ) goto bad_instruction; )
  if( i<p->nSort && p->apSort[i]!=0 ){
    Sorter *pSorter = p->apSort[i];
    p->tos++;
    VERIFY( NeedStack(p, p->tos); )
    sqliteSetString(&zStack[p->tos], pSorter->zKey, 0);
    aStack[p->tos].n = pSorter->nKey;
    aStack[p->tos].flags = STK_Str|STK_Dyn;
  }
  break;
}
#endif /* NOT USED */

/* Opcode: SortCallback P1 P2 *
**
** The top of the stack contains a callback record built using
** the SortMakeRec operation with the same P1 value as this
** instruction.  Pop this record from the stack and invoke the
** callback on it.
*/
case OP_SortCallback: {
  int i = p->tos;
  VERIFY( if( i<0 ) goto not_enough_stack; )
  if( xCallback!=0 ){
    if( xCallback(pArg, pOp->p1, (char**)zStack[i], p->azColName) ){
      rc = SQLITE_ABORT;
    }
  }
  POPSTACK;
  break;
}

/* Opcode: SortClose P1 * *
**
** Close the given sorter and remove all its elements.
*/
case OP_SortClose: {
  Sorter *pSorter;
  int i = pOp->p1;
  VERIFY( if( i<0 ) goto bad_instruction; )
  if( i<p->nSort ){
     while( (pSorter = p->apSort[i])!=0 ){
       p->apSort[i] = pSorter->pNext;
       sqliteFree(pSorter->zKey);
       sqliteFree(pSorter->pData);
       sqliteFree(pSorter);
     }
  }
  break;
}

/* Opcode: FileOpen * * P3
**
** Open the file named by P3 for reading using the FileRead opcode.
** If P3 is "stdin" then open standard input for reading.
*/
case OP_FileOpen: {
  VERIFY( if( pOp->p3==0 ) goto bad_instruction; )
  if( p->pFile ){
    if( p->pFile!=stdin ) fclose(p->pFile);
    p->pFile = 0;
  }
  if( sqliteStrICmp(pOp->p3,"stdin")==0 ){
    p->pFile = stdin;
  }else{
    p->pFile = fopen(pOp->p3, "r");
  }
  if( p->pFile==0 ){
    sqliteSetString(pzErrMsg,"unable to open file: ", pOp->p3, 0);
    rc = SQLITE_ERROR;
    goto cleanup;
  }
  break;
}

#if 0 /* NOT USED */
/* Opcode: FileClose * * *
**
** Close a file previously opened using FileOpen.  This is a no-op
** if there is no prior FileOpen call.
*/
case OP_FileClose: {
  if( p->pFile ){
    if( p->pFile!=stdin ) fclose(p->pFile);
    p->pFile = 0;
  }
  if( p->azField ){
    sqliteFree(p->azField);
    p->azField = 0;
  }
  p->nField = 0;
  if( p->zLine ){
    sqliteFree(p->zLine);
    p->zLine = 0;
  }
  p->nLineAlloc = 0;
  break;
}
#endif

/* Opcode: FileRead P1 P2 P3
**
** Read a single line of input from the open file (the file opened using
** FileOpen).  If we reach end-of-file, jump immediately to P2.  If
** we are able to get another line, split the line apart using P3 as
** a delimiter.  There should be P1 fields.  If the input line contains
** more than P1 fields, ignore the excess.  If the input line contains
** fewer than P1 fields, assume the remaining fields contain an
** empty strings.
*/
case OP_FileRead: {
  int n, eol, nField, i, c, nDelim;
  char *zDelim, *z;
  if( p->pFile==0 ) goto fileread_jump;
  nField = pOp->p1;
  if( nField<=0 ) goto fileread_jump;
  if( nField!=p->nField || p->azField==0 ){
    p->azField = sqliteRealloc(p->azField, sizeof(char*)*nField+1);
    if( p->azField==0 ){
      p->nField = 0;
      goto fileread_jump;
    }
    p->nField = nField;
  }
  n = 0;
  eol = 0;
  while( eol==0 ){
    if( p->zLine==0 || n+200>p->nLineAlloc ){
      p->nLineAlloc = p->nLineAlloc*2 + 300;
      p->zLine = sqliteRealloc(p->zLine, p->nLineAlloc);
      if( p->zLine==0 ){
        p->nLineAlloc = 0;
        goto fileread_jump;
      }
    }
    if( fgets(&p->zLine[n], p->nLineAlloc-n, p->pFile)==0 ){
      eol = 1;
      p->zLine[n] = 0;
    }else{
      while( p->zLine[n] ){ n++; }
      if( n>0 && p->zLine[n-1]=='\n' ){
        n--;
        p->zLine[n] = 0;
        eol = 1;
      }
    }
  }
  if( n==0 ) goto fileread_jump;
  z = p->zLine;
  if( z[0]=='\\' && z[1]=='.' && z[2]==0 ){
    goto fileread_jump;
  }
  zDelim = pOp->p3;
  if( zDelim==0 ) zDelim = "\t";
  c = zDelim[0];
  nDelim = strlen(zDelim);
  p->azField[0] = z;
  for(i=1; *z!=0 && i<=nField; i++){
    int from, to;
    from = to = 0;
    while( z[from] ){
      if( z[from]=='\\' && z[from+1]!=0 ){
        z[to++] = z[from+1];
        from += 2;
        continue;
      }
      if( z[from]==c && strncmp(&z[from],zDelim,nDelim)==0 ) break;
      z[to++] = z[from++];
    }
    if( z[from] ){
      z[to] = 0;
      z += from + nDelim;
      if( i<nField ) p->azField[i] = z;
    }else{
      z[to] = 0;
      z = "";
    }
  }
  while( i<nField ){
    p->azField[i++] = "";
  }
  break;

  /* If we reach end-of-file, or if anything goes wrong, jump here.
  ** This code will cause a jump to P2 */
fileread_jump:
  pc = pOp->p2 - 1;
  break;
}

/* Opcode: FileColumn P1 * *
**
** Push onto the stack the P1-th column of the most recently read line
** from the input file.
*/
case OP_FileColumn: {
  int i = pOp->p1;
  char *z;
  VERIFY( if( NeedStack(p, p->tos+1) ) goto no_mem; )
  if( VERIFY( i>=0 && i<p->nField && ) p->azField ){
    z = p->azField[i];
  }else{
    z = 0;
  }
  if( z==0 ) z = "";
  p->tos++;
  aStack[p->tos].n = strlen(z) + 1;
  zStack[p->tos] = z;
  aStack[p->tos].flags = STK_Str;
  break;
}

/* Opcode: MemStore P1 * *
**
** Pop a single value of the stack and store that value into memory
** location P1.  P1 should be a small integer since space is allocated
** for all memory locations between 0 and P1 inclusive.
*/
case OP_MemStore: {
  int i = pOp->p1;
  int tos = p->tos;
  Mem *pMem;
  char *zOld;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( i>=p->nMem ){
    int nOld = p->nMem;
    p->nMem = i + 5;
    p->aMem = sqliteRealloc(p->aMem, p->nMem*sizeof(p->aMem[0]));
    if( p->aMem==0 ) goto no_mem;
    if( nOld<p->nMem ){
      memset(&p->aMem[nOld], 0, sizeof(p->aMem[0])*(p->nMem-nOld));
    }
  }
  pMem = &p->aMem[i];
  if( pMem->s.flags & STK_Dyn ){
    zOld = pMem->z;
  }else{
    zOld = 0;
  }
  pMem->s = aStack[tos];
  if( pMem->s.flags & STK_Str ){
    pMem->z = sqliteStrNDup(zStack[tos], pMem->s.n);
    pMem->s.flags |= STK_Dyn;
  }
  if( zOld ) sqliteFree(zOld);
  POPSTACK;
  break;
}

/* Opcode: MemLoad P1 * *
**
** Push a copy of the value in memory location P1 onto the stack.
*/
case OP_MemLoad: {
  int tos = ++p->tos;
  int i = pOp->p1;
  VERIFY( if( NeedStack(p, tos) ) goto no_mem; )
  if( i<0 || i>=p->nMem ){
    aStack[tos].flags = STK_Null;
    zStack[tos] = 0;
  }else{
    aStack[tos] = p->aMem[i].s;
    if( aStack[tos].flags & STK_Str ){
      char *z = sqliteMalloc(aStack[tos].n);
      if( z==0 ) goto no_mem;
      memcpy(z, p->aMem[i].z, aStack[tos].n);
      zStack[tos] = z;
      aStack[tos].flags |= STK_Dyn;
    }
  }
  break;
}

/* Opcode: AggReset * P2 *
**
** Reset the aggregator so that it no longer contains any data.
** Future aggregator elements will contain P2 values each.
*/
case OP_AggReset: {
  AggReset(&p->agg);
  p->agg.nMem = pOp->p2;
  break;
}

/* Opcode: AggFocus * P2 *
**
** Pop the top of the stack and use that as an aggregator key.  If
** an aggregator with that same key already exists, then make the
** aggregator the current aggregator and jump to P2.  If no aggregator
** with the given key exists, create one and make it current but
** do not jump.
**
** The order of aggregator opcodes is important.  The order is:
** AggReset AggFocus AggNext.  In other words, you must execute
** AggReset first, then zero or more AggFocus operations, then
** zero or more AggNext operations.  You must not execute an AggFocus
** in between an AggNext and an AggReset.
*/
case OP_AggFocus: {
  int tos = p->tos;
  AggElem *pElem;
  char *zKey;
  int nKey;

  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( Stringify(p, tos) ) goto no_mem;
  zKey = zStack[tos]; 
  nKey = aStack[tos].n;
  pElem = sqliteHashFind(&p->agg.hash, zKey, nKey);
  if( pElem ){
    p->agg.pCurrent = pElem;
    pc = pOp->p2 - 1;
  }else{
    AggInsert(&p->agg, zKey, nKey);
    if( sqlite_malloc_failed ) goto no_mem;
  }
  POPSTACK;
  break; 
}

/* Opcode: AggIncr P1 P2 *
**
** Increase the integer value in the P2-th field of the aggregate
** element current in focus by an amount P1.
*/
case OP_AggIncr: {
  AggElem *pFocus = AggInFocus(p->agg);
  int i = pOp->p2;
  if( pFocus==0 ) goto no_mem;
  if( i>=0 && i<p->agg.nMem ){
    Mem *pMem = &pFocus->aMem[i];
    if( pMem->s.flags!=STK_Int ){
      if( pMem->s.flags & STK_Int ){
        /* Do nothing */
      }else if( pMem->s.flags & STK_Real ){
        pMem->s.i = pMem->s.r;
      }else if( pMem->s.flags & STK_Str ){
        pMem->s.i = atoi(pMem->z);
      }else{
        pMem->s.i = 0;
      }
      if( pMem->s.flags & STK_Dyn ) sqliteFree(pMem->z);
      pMem->z = 0;
      pMem->s.flags = STK_Int;
    }
    pMem->s.i += pOp->p1;
  }
  break;
}

/* Opcode: AggSet * P2 *
**
** Move the top of the stack into the P2-th field of the current
** aggregate.  String values are duplicated into new memory.
*/
case OP_AggSet: {
  AggElem *pFocus = AggInFocus(p->agg);
  int i = pOp->p2;
  int tos = p->tos;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( pFocus==0 ) goto no_mem;
  if( VERIFY( i>=0 && ) i<p->agg.nMem ){
    Mem *pMem = &pFocus->aMem[i];
    char *zOld;
    if( pMem->s.flags & STK_Dyn ){
      zOld = pMem->z;
    }else{
      zOld = 0;
    }
    pMem->s = aStack[tos];
    if( pMem->s.flags & STK_Str ){
      pMem->z = sqliteMalloc( aStack[tos].n );
      if( pMem->z==0 ) goto no_mem;
      memcpy(pMem->z, zStack[tos], pMem->s.n);
      pMem->s.flags |= STK_Str|STK_Dyn;
    }
    if( zOld ) sqliteFree(zOld);
  }
  POPSTACK;
  break;
}

/* Opcode: AggGet * P2 *
**
** Push a new entry onto the stack which is a copy of the P2-th field
** of the current aggregate.  Strings are not duplicated so
** string values will be ephemeral.
*/
case OP_AggGet: {
  AggElem *pFocus = AggInFocus(p->agg);
  int i = pOp->p2;
  int tos = ++p->tos;
  VERIFY( if( NeedStack(p, tos) ) goto no_mem; )
  if( pFocus==0 ) goto no_mem;
  if( VERIFY( i>=0 && ) i<p->agg.nMem ){
    Mem *pMem = &pFocus->aMem[i];
    aStack[tos] = pMem->s;
    zStack[tos] = pMem->z;
    aStack[tos].flags &= ~STK_Dyn;
  }
  break;
}

/* Opcode: AggNext * P2 *
**
** Make the next aggregate value the current aggregate.  The prior
** aggregate is deleted.  If all aggregate values have been consumed,
** jump to P2.
**
** The order of aggregator opcodes is important.  The order is:
** AggReset AggFocus AggNext.  In other words, you must execute
** AggReset first, then zero or more AggFocus operations, then
** zero or more AggNext operations.  You must not execute an AggFocus
** in between an AggNext and an AggReset.
*/
case OP_AggNext: {
  if( p->agg.pSearch==0 ){
    p->agg.pSearch = sqliteHashFirst(&p->agg.hash);
  }else{
    p->agg.pSearch = sqliteHashNext(p->agg.pSearch);
  }
  if( p->agg.pSearch==0 ){
    pc = pOp->p2 - 1;
  } else {
    p->agg.pCurrent = sqliteHashData(p->agg.pSearch);
  }
  break;
}

#if 0 /* NOT USED */
/* Opcode: SetClear P1 * *
**
** Remove all elements from the P1-th Set.
*/
case OP_SetClear: {
  int i = pOp->p1;
  if( i>=0 && i<p->nSet ){
    sqliteHashClear(&p->aSet[i].hash);
  }
  break;
}
#endif /* NOT USED */

/* Opcode: SetInsert P1 * P3
**
** If Set P1 does not exist then create it.  Then insert value
** P3 into that set.  If P3 is NULL, then insert the top of the
** stack into the set.
*/
case OP_SetInsert: {
  int i = pOp->p1;
  if( p->nSet<=i ){
    int k;
    p->aSet = sqliteRealloc(p->aSet, (i+1)*sizeof(p->aSet[0]) );
    if( p->aSet==0 ) goto no_mem;
    for(k=p->nSet; k<=i; k++){
      sqliteHashInit(&p->aSet[k].hash, SQLITE_HASH_BINARY, 1);
    }
    p->nSet = i+1;
  }
  if( pOp->p3 ){
    sqliteHashInsert(&p->aSet[i].hash, pOp->p3, strlen(pOp->p3)+1, p);
  }else{
    int tos = p->tos;
    if( tos<0 ) goto not_enough_stack;
    if( Stringify(p, tos) ) goto no_mem;
    sqliteHashInsert(&p->aSet[i].hash, zStack[tos], aStack[tos].n, p);
    POPSTACK;
  }
  if( sqlite_malloc_failed ) goto no_mem;
  break;
}

/* Opcode: SetFound P1 P2 *
**
** Pop the stack once and compare the value popped off with the
** contents of set P1.  If the element popped exists in set P1,
** then jump to P2.  Otherwise fall through.
*/
case OP_SetFound: {
  int i = pOp->p1;
  int tos = p->tos;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( Stringify(p, tos) ) goto no_mem;
  if( VERIFY( i>=0 && i<p->nSet &&) 
       sqliteHashFind(&p->aSet[i].hash, zStack[tos], aStack[tos].n)){
    pc = pOp->p2 - 1;
  }
  POPSTACK;
  break;
}

/* Opcode: SetNotFound P1 P2 *
**
** Pop the stack once and compare the value popped off with the
** contents of set P1.  If the element popped does not exists in 
** set P1, then jump to P2.  Otherwise fall through.
*/
case OP_SetNotFound: {
  int i = pOp->p1;
  int tos = p->tos;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( Stringify(p, tos) ) goto no_mem;
  if(VERIFY( i>=0 && i<p->nSet &&)
       sqliteHashFind(&p->aSet[i].hash, zStack[tos], aStack[tos].n)==0 ){
    pc = pOp->p2 - 1;
  }
  POPSTACK;
  break;
}

/* Opcode: Strlen * * *
**
** Interpret the top of the stack as a string.  Replace the top of
** stack with an integer which is the length of the string.
*/
case OP_Strlen: {
  int tos = p->tos;
  int len;
  VERIFY( if( tos<0 ) goto not_enough_stack; )
  if( Stringify(p, tos) ) goto no_mem;
#ifdef SQLITE_UTF8
  {
    char *z = zStack[tos];
    for(len=0; *z; z++){ if( (0xc0&*z)!=0x80 ) len++; }
  }
#else
  len = aStack[tos].n-1;
#endif
  POPSTACK;
  p->tos++;
  aStack[tos].i = len;
  aStack[tos].flags = STK_Int;
  break;
}

/* Opcode: Substr P1 P2 *
**
** This operation pops between 1 and 3 elements from the stack and
** pushes back a single element.  The bottom-most element popped from
** the stack is a string and the element pushed back is also a string.
** The other two elements popped are integers.  The integers are taken
** from the stack only if P1 and/or P2 are 0.  When P1 or P2 are
** not zero, the value of the operand is used rather than the integer
** from the stack.  In the sequel, we will use P1 and P2 to describe
** the two integers, even if those integers are really taken from the
** stack.
**
** The string pushed back onto the stack is a substring of the string
** that was popped.  There are P2 characters in the substring.  The
** first character of the substring is the P1-th character of the
** original string where the left-most character is 1 (not 0).  If P1
** is negative, then counting begins at the right instead of at the
** left.
*/
case OP_Substr: {
  int cnt;
  int start;
  int n;
  char *z;

  if( pOp->p2==0 ){
    VERIFY( if( p->tos<0 ) goto not_enough_stack; )
    Integerify(p, p->tos);
    cnt = aStack[p->tos].i;
    POPSTACK;
  }else{
    cnt = pOp->p2;
  }
  if( pOp->p1==0 ){
    VERIFY( if( p->tos<0 ) goto not_enough_stack; )
    Integerify(p, p->tos);
    start = aStack[p->tos].i - 1;
    POPSTACK;
  }else{
    start = pOp->p1 - 1;
  }
  VERIFY( if( p->tos<0 ) goto not_enough_stack; )
  if( Stringify(p, p->tos) ) goto no_mem;

  /* "n" will be the number of characters in the input string.
  ** For iso8859, the number of characters is the number of bytes.
  ** Buf for UTF-8, some characters can use multiple bytes and the
  ** situation is more complex. 
  */
#ifdef SQLITE_UTF8
  z = zStack[p->tos];
  for(n=0; *z; z++){ if( (0xc0&*z)!=0x80 ) n++; }
#else
  n = aStack[p->tos].n - 1;
#endif
  if( start<0 ){
    start += n + 1;
    if( start<0 ){
      cnt += start;
      start = 0;
    }
  }
  if( start>n ){
    start = n;
  }
  if( cnt<0 ) cnt = 0;
  if( cnt > n ){
    cnt = n;
  }

  /* At this point, "start" is the index of the first character to
  ** extract and "cnt" is the number of characters to extract.  We
  ** need to convert units on these variable from characters into
  ** bytes.  For iso8859, the conversion is a no-op, but for UTF-8
  ** we have to do a little work.
  */
#ifdef SQLITE_UTF8
  {
    int c_start = start;
    int c_cnt = cnt;
    int i;
    z = zStack[p->tos];
    for(start=i=0; i<c_start; i++){
      while( (0xc0&z[++start])==0x80 ){}
    }
    for(cnt=i=0; i<c_cnt; i++){
      while( (0xc0&z[(++cnt)+start])==0x80 ){}
    }
  }
#endif
  z = sqliteMalloc( cnt+1 );
  if( z==0 ) goto no_mem;
  strncpy(z, &zStack[p->tos][start], cnt);
  z[cnt] = 0;
  POPSTACK;
  p->tos++;
  zStack[p->tos] = z;
  aStack[p->tos].n = cnt + 1;
  aStack[p->tos].flags = STK_Str|STK_Dyn;
  break;
}

/* An other opcode is illegal...
*/
default: {
  sprintf(zBuf,"%d",pOp->opcode);
  sqliteSetString(pzErrMsg, "unknown opcode ", zBuf, 0);
  rc = SQLITE_INTERNAL;
  break;
}

/*****************************************************************************
** The cases of the switch statement above this line should all be indented
** by 6 spaces.  But the left-most 6 spaces have been removed to improve the
** readability.  From this point on down, the normal indentation rules are
** restored.
*****************************************************************************/
    }

    /* The following code adds nothing to the actual functionality
    ** of the program.  It is only here for testing and debugging.
    ** On the other hand, it does burn CPU cycles every time through
    ** the evaluator loop.  So we can leave it out when NDEBUG is defined.
    */
#ifndef NDEBUG
    if( pc<-1 || pc>=p->nOp ){
      sqliteSetString(pzErrMsg, "jump destination out of range", 0);
      rc = SQLITE_INTERNAL;
    }
    if( p->trace && p->tos>=0 ){
      int i;
      fprintf(p->trace, "Stack:");
      for(i=p->tos; i>=0 && i>p->tos-5; i--){
        if( aStack[i].flags & STK_Null ){
          fprintf(p->trace, " NULL");
        }else if( aStack[i].flags & STK_Int ){
          fprintf(p->trace, " i:%d", aStack[i].i);
        }else if( aStack[i].flags & STK_Real ){
          fprintf(p->trace, " r:%g", aStack[i].r);
        }else if( aStack[i].flags & STK_Str ){
          int j, k;
          char zBuf[100];
          zBuf[0] = ' ';
          zBuf[1] = (aStack[i].flags & STK_Dyn)!=0 ? 'z' : 's';
          zBuf[2] = '[';
          k = 3;
          for(j=0; j<20 && j<aStack[i].n; j++){
            int c = zStack[i][j];
            if( c==0 && j==aStack[i].n-1 ) break;
            if( isprint(c) && !isspace(c) ){
              zBuf[k++] = c;
            }else{
              zBuf[k++] = '.';
            }
          }
          zBuf[k++] = ']';
          zBuf[k++] = 0;
          fprintf(p->trace, "%s", zBuf);
        }else{
          fprintf(p->trace, " ???");
        }
      }
      fprintf(p->trace,"\n");
    }
#endif
  }

cleanup:
  Cleanup(p);
  if( rc!=SQLITE_OK ){
    closeAllCursors(p);
    sqliteBtreeRollback(pBt);
    if( db->pBeTemp ) sqliteBtreeRollback(db->pBeTemp);
    sqliteRollbackInternalChanges(db);
    db->flags &= ~SQLITE_InTrans;
  }
  return rc;

  /* Jump to here if a malloc() fails.  It's hard to get a malloc()
  ** to fail on a modern VM computer, so this code is untested.
  */
no_mem:
  sqliteSetString(pzErrMsg, "out or memory", 0);
  rc = SQLITE_NOMEM;
  goto cleanup;

  /* Jump to here for any other kind of fatal error.  The "rc" variable
  ** should hold the error number.
  */
abort_due_to_error:
  sqliteSetString(pzErrMsg, sqliteErrStr(rc), 0);
  goto cleanup;

  /* Jump to here if a operator is encountered that requires more stack
  ** operands than are currently available on the stack.
  */
not_enough_stack:
  sprintf(zBuf,"%d",pc);
  sqliteSetString(pzErrMsg, "too few operands on stack at ", zBuf, 0);
  rc = SQLITE_INTERNAL;
  goto cleanup;

  /* Jump here if an illegal or illformed instruction is executed.
  */
bad_instruction:
  sprintf(zBuf,"%d",pc);
  sqliteSetString(pzErrMsg, "illegal operation at ", zBuf, 0);
  rc = SQLITE_INTERNAL;
  goto cleanup;
}
