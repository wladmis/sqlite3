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
** Utility functions used throughout sqlite.
**
** This file contains functions for allocating memory, comparing
** strings, and stuff like that.
**
** $Id$
*/
#include "sqliteInt.h"
#include <stdarg.h>
#include <ctype.h>

/*
** If malloc() ever fails, this global variable gets set to 1.
** This causes the library to abort and never again function.
*/
int sqlite_malloc_failed = 0;

/*
** If MEMORY_DEBUG is defined, then use versions of malloc() and
** free() that track memory usage and check for buffer overruns.
*/
#ifdef MEMORY_DEBUG

/*
** For keeping track of the number of mallocs and frees.   This
** is used to check for memory leaks.
*/
int sqlite_nMalloc;         /* Number of sqliteMalloc() calls */
int sqlite_nFree;           /* Number of sqliteFree() calls */
int sqlite_iMallocFail;     /* Fail sqliteMalloc() after this many calls */


/*
** Allocate new memory and set it to zero.  Return NULL if
** no memory is available.
*/
void *sqliteMalloc_(int n, char *zFile, int line){
  void *p;
  int *pi;
  int k;
  sqlite_nMalloc++;
  if( sqlite_iMallocFail>=0 ){
    sqlite_iMallocFail--;
    if( sqlite_iMallocFail==0 ){
      sqlite_malloc_failed++;
      return 0;
    }
  }
  if( n==0 ) return 0;
  k = (n+sizeof(int)-1)/sizeof(int);
  pi = malloc( (3+k)*sizeof(int));
  if( pi==0 ){
    sqlite_malloc_failed++;
    return 0;
  }
  pi[0] = 0xdead1122;
  pi[1] = n;
  pi[k+2] = 0xdead3344;
  p = &pi[2];
  memset(p, 0, n);
#if MEMORY_DEBUG>1
  fprintf(stderr,"malloc %d bytes at 0x%x from %s:%d\n", n, (int)p, zFile,line);
#endif
  return p;
}

/*
** Free memory previously obtained from sqliteMalloc()
*/
void sqliteFree_(void *p, char *zFile, int line){
  if( p ){
    int *pi, k, n;
    pi = p;
    pi -= 2;
    sqlite_nFree++;
    if( pi[0]!=0xdead1122 ){
      fprintf(stderr,"Low-end memory corruption at 0x%x\n", (int)p);
      return;
    }
    n = pi[1];
    k = (n+sizeof(int)-1)/sizeof(int);
    if( pi[k+2]!=0xdead3344 ){
      fprintf(stderr,"High-end memory corruption at 0x%x\n", (int)p);
      return;
    }
    memset(pi, 0xff, (k+3)*sizeof(int));
#if MEMORY_DEBUG>1
    fprintf(stderr,"free %d bytes at 0x%x from %s:%d\n", n, (int)p, zFile,line);
#endif
    free(pi);
  }
}

/*
** Resize a prior allocation.  If p==0, then this routine
** works just like sqliteMalloc().  If n==0, then this routine
** works just like sqliteFree().
*/
void *sqliteRealloc_(void *oldP, int n, char *zFile, int line){
  int *oldPi, *pi, k, oldN, oldK;
  void *p;
  if( oldP==0 ){
    return sqliteMalloc_(n,zFile,line);
  }
  if( n==0 ){
    sqliteFree_(oldP,zFile,line);
    return 0;
  }
  oldPi = oldP;
  oldPi -= 2;
  if( oldPi[0]!=0xdead1122 ){
    fprintf(stderr,"Low-end memory corruption in realloc at 0x%x\n", (int)p);
    return 0;
  }
  oldN = oldPi[1];
  oldK = (oldN+sizeof(int)-1)/sizeof(int);
  if( oldPi[oldK+2]!=0xdead3344 ){
    fprintf(stderr,"High-end memory corruption in realloc at 0x%x\n", (int)p);
    return 0;
  }
  k = (n + sizeof(int) - 1)/sizeof(int);
  pi = malloc( (k+3)*sizeof(int) );
  if( pi==0 ){
    sqlite_malloc_failed++;
    return 0;
  }
  pi[0] = 0xdead1122;
  pi[1] = n;
  pi[k+2] = 0xdead3344;
  p = &pi[2];
  memcpy(p, oldP, n>oldN ? oldN : n);
  if( n>oldN ){
    memset(&((char*)p)[oldN], 0, n-oldN);
  }
  memset(oldPi, 0, (oldK+3)*sizeof(int));
  free(oldPi);
#if MEMORY_DEBUG>1
  fprintf(stderr,"realloc %d to %d bytes at 0x%x to 0x%x at %s:%d\n", oldN, n,
    (int)oldP, (int)p, zFile, line);
#endif
  return p;
}

/*
** Make a duplicate of a string into memory obtained from malloc()
** Free the original string using sqliteFree().
**
** This routine is called on all strings that are passed outside of
** the SQLite library.  That way clients can free the string using free()
** rather than having to call sqliteFree().
*/
void sqliteStrRealloc(char **pz){
  char *zNew;
  if( pz==0 || *pz==0 ) return;
  zNew = malloc( strlen(*pz) + 1 );
  if( zNew==0 ){
    sqlite_malloc_failed++;
    sqliteFree(*pz);
    *pz = 0;
  }
  strcpy(zNew, *pz);
  sqliteFree(*pz);
  *pz = zNew;
}

/*
** Make a copy of a string in memory obtained from sqliteMalloc()
*/
char *sqliteStrDup_(const char *z, char *zFile, int line){
  char *zNew = sqliteMalloc_(strlen(z)+1, zFile, line);
  if( zNew ) strcpy(zNew, z);
  return zNew;
}
char *sqliteStrNDup_(const char *z, int n, char *zFile, int line){
  char *zNew = sqliteMalloc_(n+1, zFile, line);
  if( zNew ){
    memcpy(zNew, z, n);
    zNew[n] = 0;
  }
  return zNew;
}
#endif /* MEMORY_DEBUG */

/*
** The following versions of malloc() and free() are for use in a
** normal build.
*/
#if !defined(MEMORY_DEBUG)

/*
** Allocate new memory and set it to zero.  Return NULL if
** no memory is available.
*/
void *sqliteMalloc(int n){
  void *p = malloc(n);
  if( p==0 ){
    sqlite_malloc_failed++;
    return 0;
  }
  memset(p, 0, n);
  return p;
}

/*
** Free memory previously obtained from sqliteMalloc()
*/
void sqliteFree(void *p){
  if( p ){
    free(p);
  }
}

/*
** Resize a prior allocation.  If p==0, then this routine
** works just like sqliteMalloc().  If n==0, then this routine
** works just like sqliteFree().
*/
void *sqliteRealloc(void *p, int n){
  if( p==0 ){
    return sqliteMalloc(n);
  }
  if( n==0 ){
    sqliteFree(p);
    return 0;
  }
  p = realloc(p, n);
  if( p==0 ){
    sqlite_malloc_failed++;
  }
  return p;
}

/*
** Make a copy of a string in memory obtained from sqliteMalloc()
*/
char *sqliteStrDup(const char *z){
  char *zNew = sqliteMalloc(strlen(z)+1);
  if( zNew ) strcpy(zNew, z);
  return zNew;
}
char *sqliteStrNDup(const char *z, int n){
  char *zNew = sqliteMalloc(n+1);
  if( zNew ){
    memcpy(zNew, z, n);
    zNew[n] = 0;
  }
  return zNew;
}
#endif /* !defined(MEMORY_DEBUG) */

/*
** Create a string from the 2nd and subsequent arguments (up to the
** first NULL argument), store the string in memory obtained from
** sqliteMalloc() and make the pointer indicated by the 1st argument
** point to that string.
*/
void sqliteSetString(char **pz, const char *zFirst, ...){
  va_list ap;
  int nByte;
  const char *z;
  char *zResult;

  if( pz==0 ) return;
  nByte = strlen(zFirst) + 1;
  va_start(ap, zFirst);
  while( (z = va_arg(ap, const char*))!=0 ){
    nByte += strlen(z);
  }
  va_end(ap);
  sqliteFree(*pz);
  *pz = zResult = sqliteMalloc( nByte );
  if( zResult==0 ) return;
  strcpy(zResult, zFirst);
  zResult += strlen(zResult);
  va_start(ap, zFirst);
  while( (z = va_arg(ap, const char*))!=0 ){
    strcpy(zResult, z);
    zResult += strlen(zResult);
  }
  va_end(ap);
#ifdef MEMORY_DEBUG
#if MEMORY_DEBUG>1
  fprintf(stderr,"string at 0x%x is %s\n", (int)*pz, *pz);
#endif
#endif
}

/*
** Works like sqliteSetString, but each string is now followed by
** a length integer which specifies how much of the source string 
** to copy (in bytes).  -1 means use the whole string.
*/
void sqliteSetNString(char **pz, ...){
  va_list ap;
  int nByte;
  const char *z;
  char *zResult;
  int n;

  if( pz==0 ) return;
  nByte = 0;
  va_start(ap, pz);
  while( (z = va_arg(ap, const char*))!=0 ){
    n = va_arg(ap, int);
    if( n<=0 ) n = strlen(z);
    nByte += n;
  }
  va_end(ap);
  sqliteFree(*pz);
  *pz = zResult = sqliteMalloc( nByte + 1 );
  if( zResult==0 ) return;
  va_start(ap, pz);
  while( (z = va_arg(ap, const char*))!=0 ){
    n = va_arg(ap, int);
    if( n<=0 ) n = strlen(z);
    strncpy(zResult, z, n);
    zResult += n;
  }
  *zResult = 0;
#ifdef MEMORY_DEBUG
#if MEMORY_DEBUG>1
  fprintf(stderr,"string at 0x%x is %s\n", (int)*pz, *pz);
#endif
#endif
  va_end(ap);
}

/*
** Convert an SQL-style quoted string into a normal string by removing
** the quote characters.  The conversion is done in-place.  If the
** input does not begin with a quote character, then this routine
** is a no-op.
*/
void sqliteDequote(char *z){
  int quote;
  int i, j;
  if( z==0 ) return;
  quote = z[0];
  if( quote!='\'' && quote!='"' ) return;
  for(i=1, j=0; z[i]; i++){
    if( z[i]==quote ){
      if( z[i+1]==quote ){
        z[j++] = quote;
        i++;
      }else{
        z[j++] = 0;
        break;
      }
    }else{
      z[j++] = z[i];
    }
  }
}

/* An array to map all upper-case characters into their corresponding
** lower-case character. 
*/
static unsigned char UpperToLower[] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17,
     18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
     36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
     54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 97, 98, 99,100,101,102,103,
    104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,
    122, 91, 92, 93, 94, 95, 96, 97, 98, 99,100,101,102,103,104,105,106,107,
    108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,
    126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,
    162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,
    180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,
    198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,
    216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,
    234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,
    252,253,254,255
};

/*
** This function computes a hash on the name of a keyword.
** Case is not significant.
*/
int sqliteHashNoCase(const char *z, int n){
  int h = 0;
  if( n<=0 ) n = strlen(z);
  while( n > 0  ){
    h = (h<<3) ^ h ^ UpperToLower[(unsigned char)*z++];
    n--;
  }
  if( h<0 ) h = -h;
  return h;
}

/*
** Some systems have stricmp().  Others have strcasecmp().  Because
** there is no consistency, we will define our own.
*/
int sqliteStrICmp(const char *zLeft, const char *zRight){
  register unsigned char *a, *b;
  a = (unsigned char *)zLeft;
  b = (unsigned char *)zRight;
  while( *a!=0 && UpperToLower[*a]==UpperToLower[*b]){ a++; b++; }
  return *a - *b;
}
int sqliteStrNICmp(const char *zLeft, const char *zRight, int N){
  register unsigned char *a, *b;
  a = (unsigned char *)zLeft;
  b = (unsigned char *)zRight;
  while( N-- > 0 && *a!=0 && UpperToLower[*a]==UpperToLower[*b]){ a++; b++; }
  return N<0 ? 0 : *a - *b;
}

/* Notes on string comparisions.
**
** We want the main string comparision function used for sorting to
** sort both numbers and alphanumeric words into the correct sequence.
** The same routine should do both without prior knowledge of which
** type of text the input represents.  It should even work for strings
** which are a mixture of text and numbers.
**
** To accomplish this, we keep track of a state number while scanning
** the two strings.  The states are as follows:
**
**    1      Beginning of word
**    2      Arbitrary text
**    3      Integer
**    4      Negative integer
**    5      Real number
**    6      Negative real
**
** The scan begins in state 1, beginning of word.  Transitions to other
** states are determined by characters seen, as shown in the following
** chart:
**
**      Current State         Character Seen  New State
**      --------------------  --------------  -------------------
**      0 Beginning of word   "-"             3 Negative integer
**                            digit           2 Integer
**                            space           0 Beginning of word
**                            otherwise       1 Arbitrary text
**
**      1 Arbitrary text      space           0 Beginning of word
**                            digit           2 Integer
**                            otherwise       1 Arbitrary text
**
**      2 Integer             space           0 Beginning of word
**                            "."             4 Real number
**                            digit           2 Integer
**                            otherwise       1 Arbitrary text
**
**      3 Negative integer    space           0 Beginning of word
**                            "."             5 Negative Real num
**                            digit           3 Negative integer
**                            otherwise       1 Arbitrary text
**
**      4 Real number         space           0 Beginning of word
**                            digit           4 Real number
**                            otherwise       1 Arbitrary text
**
**      5 Negative real num   space           0 Beginning of word
**                            digit           5 Negative real num
**                            otherwise       1 Arbitrary text
**
** To implement this state machine, we first classify each character
** into on of the following categories:
**
**      0  Text
**      1  Space
**      2  Digit
**      3  "-"
**      4  "."
**
** Given an arbitrary character, the array charClass[] maps that character
** into one of the atove categories.
*/
static const unsigned char charClass[] = {
        /* x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF */
/* 0x */   0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0,
/* 1x */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 2x */   1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4, 0,
/* 3x */   2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0,
/* 4x */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 5x */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 6x */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 7x */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 8x */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 9x */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* Ax */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* Bx */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* Cx */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* Dx */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* Ex */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* Fx */   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
#define N_CHAR_CLASS 5

/*
** Given the current state number (0 thru 5), this array figures
** the new state number given the character class.
*/
static const unsigned char stateMachine[] = {
 /* Text,  Space, Digit, "-", "." */
      1,      0,    2,    3,   1,      /* State 0: Beginning of word */
      1,      0,    2,    1,   1,      /* State 1: Arbitrary text */
      1,      0,    2,    1,   4,      /* State 2: Integer */
      1,      0,    3,    1,   5,      /* State 3: Negative integer */
      1,      0,    4,    1,   1,      /* State 4: Real number */
      1,      0,    5,    1,   1,      /* State 5: Negative real num */
};

/* This routine does a comparison of two strings.  Case is used only
** if useCase!=0.  Numbers compare in numerical order.
*/
static int privateStrCmp(const char *atext, const char *btext, int useCase){
  register unsigned char *a, *b, *map, ca, cb;
  int result;
  register int cclass = 0;

  a = (unsigned char *)atext;
  b = (unsigned char *)btext;
  if( useCase ){
    do{
      if( (ca= *a++)!=(cb= *b++) ) break;
      cclass = stateMachine[cclass*N_CHAR_CLASS + charClass[ca]];
    }while( ca!=0 );
  }else{
    map = UpperToLower;
    do{
      if( (ca=map[*a++])!=(cb=map[*b++]) ) break;
      cclass = stateMachine[cclass*N_CHAR_CLASS + charClass[ca]];
    }while( ca!=0 );
  }
  switch( cclass ){
    case 0:
    case 1: {
      if( isdigit(ca) && isdigit(cb) ){
        cclass = 2;
      }
      break;
    }
    default: {
      break;
    }
  }
  switch( cclass ){
    case 2:
    case 3: {
      if( isdigit(ca) ){
        if( isdigit(cb) ){
          int acnt, bcnt;
          acnt = bcnt = 0;
          while( isdigit(*a++) ) acnt++;
          while( isdigit(*b++) ) bcnt++;
          result = acnt - bcnt;
          if( result==0 ) result = ca-cb;
        }else{
          result = 1;
        }
      }else if( isdigit(cb) ){
        result = -1;
      }else if( ca=='.' ){
        result = 1;
      }else if( cb=='.' ){
        result = -1;
      }else{
        result = ca - cb;
        cclass = 2;
      }
      if( cclass==3 ) result = -result;
      break;
    }
    case 0:
    case 1:
    case 4: {
      result = ca - cb;
      break;
    }
    case 5: {
      result = cb - ca;
    };
  }
  return result;
}

/*
** Do a comparison of pure numerics.  If either string is not a pure
** numeric, then return 0.  Otherwise return 1 and set *pResult to be
** negative, zero or positive if the first string are numerially less than
** equal to, or greater than the second.
*/
static int privateCompareNum(const char *a, const char *b, int *pResult){
  char *endPtr;
  double rA, rB;
  int isNumA, isNumB;
  if( isdigit(*a) || ((*a=='-' || *a=='+') && isdigit(a[1])) ){
    rA = strtod(a, &endPtr);
    isNumA = *endPtr==0;
  }else{
    isNumA = 0;
  }
  if( isdigit(*b) || ((*b=='-' || *b=='+') && isdigit(b[1])) ){
    rB = strtod(b, &endPtr);
    isNumB = *endPtr==0;
  }else{
    isNumB = 0;
  }
  if( isNumB==0 && isNumA==0 ) return 0;
  if( isNumA!=isNumB ){
    *pResult =  isNumA - isNumB;
  }else if( rA<rB ){
    *pResult = -1;
  }else if( rA>rB ){
    *pResult = 1;
  }else{
    *pResult = 0;
  }
  return 1;
}

/* This comparison routine is what we use for comparison operations
** in an SQL expression.  (Ex:  name<'Hello' or value<5).  Compare two
** strings.  Use case only as a tie-breaker.  Numbers compare in
** numerical order.
*/
int sqliteCompare(const char *atext, const char *btext){
  int result;
  if( !privateCompareNum(atext, btext, &result) || result==0 ){
    result = privateStrCmp(atext, btext, 0);
    if( result==0 ) result = privateStrCmp(atext, btext, 1);
  }
  return result;
}

/*
** If you compile just this one file with the -DTEST_COMPARE=1 option,
** it generates a program to test the comparisons routines.  
*/
#ifdef TEST_COMPARE
#include <stdlib.h>
#include <stdio.h>
int sortCmp(const char **a, const char **b){
  return sqliteCompare(*a, *b);
}
int main(int argc, char **argv){
  int i, j, k, n, cnt;
  static char *azStr[] = {
     "abc", "aBc", "abcd", "aBcd", 
     "123", "124", "1234", "-123", "-124", "-1234", "+124",
     "123.45", "123.456", "123.46", "-123.45", "-123.46", "-123.456", 
     "x9", "x10", "x-9", "x-10", "X9", "X10",
     "1.234e+02", "+123", "1.23E2", "1.2345e+2", "-1.2345e2", "+w"
  };
  n = sizeof(azStr)/sizeof(azStr[0]);
  qsort(azStr, n, sizeof(azStr[0]), sortCmp);
  for(i=0; i<n; i++){
    printf("%s\n", azStr[i]);
  }
  printf("Sanity1...");
  fflush(stdout);
  cnt = 0;
  for(i=0; i<n-1; i++){
    char *a = azStr[i];
    for(j=i+1; j<n; j++){
      char *b = azStr[j];
      if( sqliteCompare(a,b) != -sqliteCompare(b,a) ){
        printf("Failed!  \"%s\" vs \"%s\"\n", a, b);
        i = j = n;
      }
      cnt++;
    }
  }
  if( i<n ){
    printf(" OK (%d)\n", cnt);
  }
  printf("Sanity2...");
  fflush(stdout);
  cnt = 0;
  for(i=0; i<n; i++){
    char *a = azStr[i];
    for(j=0; j<n; j++){
      char *b = azStr[j];
      for(k=0; k<n; k++){
        char *c = azStr[k];
        int x1, x2, x3, success;
        x1 = sqliteCompare(a,b);
        x2 = sqliteCompare(b,c);
        x3 = sqliteCompare(a,c);
        if( x1==0 ){
          success = x2==x3;
        }else if( x1<0 ){
          success = (x2<=0 && x3<=0) || x2>0;
        }else{
          success = (x2>=0 && x3>=0) || x2<0;
        }
        if( !success ){
          printf("Failed!  \"%s\" vs \"%s\" vs \"%s\"\n", a, b, c);
          i = j = k = n+1;
        }
        cnt++;
      }
    }
  }
  if( i<n+1 ){
    printf(" OK (%d)\n", cnt);
  }
  return 0;
}
#endif

/*
** This routine is used for sorting.  Each key is a list of one or more
** null-terminated strings.  The list is terminated by two nulls in
** a row.  For example, the following text is key with three strings:
**
**            +one\000-two\000+three\000\000
**
** Both arguments will have the same number of strings.  This routine
** returns negative, zero, or positive if the first argument is less
** than, equal to, or greater than the first.  (Result is a-b).
**
** Every string begins with either a "+" or "-" character.  If the
** character is "-" then the return value is negated.  This is done
** to implement a sort in descending order.
*/
int sqliteSortCompare(const char *a, const char *b){
  int len;
  int res = 0;

  while( res==0 && *a && *b ){
    res = sqliteCompare(&a[1], &b[1]);
    if( res==0 ){
      len = strlen(a) + 1;
      a += len;
      b += len;
    }
  }
  if( *a=='-' ) res = -res;
  return res;
}

#ifdef SQLITE_UTF8
/*
** X is a pointer to the first byte of a UTF-8 character.  Increment
** X so that it points to the next character.  This only works right
** if X points to a well-formed UTF-8 string.
*/
#define sqliteNextChar(X)  while( (0xc0&*++(X))==0x80 ){}
#define sqliteCharVal(X)   sqlite_utf8_to_int(X)

#else /* !defined(SQLITE_UTF8) */
/*
** For iso8859 encoding, the next character is just the next byte.
*/
#define sqliteNextChar(X)  (++(X));
#define sqliteCharVal(X)   ((int)*(X))

#endif /* defined(SQLITE_UTF8) */


#ifdef SQLITE_UTF8
/*
** Convert the UTF-8 character to which z points into a 31-bit
** UCS character.  This only works right if z points to a well-formed
** UTF-8 string.
*/
static int sqlite_utf8_to_int(const unsigned char *z){
  int c;
  static const int initVal[] = {
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
     30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
     45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
     60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,
     75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,
     90,  91,  92,  93,  94,  95,  96,  97,  98,  99, 100, 101, 102, 103, 104,
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
    165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,   0,   1,   2,
      3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,
     18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,   0,
      1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,
      0,   1,   2,   3,   4,   5,   6,   7,   0,   1,   2,   3,   0,   1, 254,
    255,
  };
  c = initVal[*(z++)];
  while( (0xc0&*z)==0x80 ){
    c = (c<<6) | (0x3f&*(z++));
  }
  return c;
}
#endif

/*
** Compare two UTF-8 strings for equality where the first string can
** potentially be a "glob" expression.  Return true (1) if they
** are the same and false (0) if they are different.
**
** Globbing rules:
**
**      '*'       Matches any sequence of zero or more characters.
**
**      '?'       Matches exactly one character.
**
**     [...]      Matches one character from the enclosed list of
**                characters.
**
**     [^...]     Matches one character not in the enclosed list.
**
** With the [...] and [^...] matching, a ']' character can be included
** in the list by making it the first character after '[' or '^'.  A
** range of characters can be specified using '-'.  Example:
** "[a-z]" matches any single lower-case letter.  To match a '-', make
** it the last character in the list.
**
** This routine is usually quick, but can be N**2 in the worst case.
**
** Hints: to match '*' or '?', put them in "[]".  Like this:
**
**         abc[*]xyz        Matches "abc*xyz" only
*/
int 
sqliteGlobCompare(const unsigned char *zPattern, const unsigned char *zString){
  register int c;
  int invert;
  int seen;
  int c2;

  while( (c = *zPattern)!=0 ){
    switch( c ){
      case '*':
        while( (c=zPattern[1]) == '*' || c == '?' ){
          if( c=='?' ){
            if( *zString==0 ) return 0;
            sqliteNextChar(zString);
          }
          zPattern++;
        }
        if( c==0 ) return 1;
        c = UpperToLower[c];
        if( c=='[' ){
          while( *zString && sqliteGlobCompare(&zPattern[1],zString)==0 ){
            sqliteNextChar(zString);
          }
          return *zString!=0;
        }else{
          while( (c2 = *zString)!=0 ){
            while( c2 != 0 && c2 != c ){ c2 = *++zString; }
            if( c2==0 ) return 0;
            if( sqliteGlobCompare(&zPattern[1],zString) ) return 1;
            sqliteNextChar(zString);
          }
          return 0;
        }
      case '?': {
        if( *zString==0 ) return 0;
        sqliteNextChar(zString);
        zPattern++;
        break;
      }
      case '[': {
        int prior_c = 0;
        seen = 0;
        invert = 0;
        c = sqliteCharVal(zString);
        if( c==0 ) return 0;
        c2 = *++zPattern;
        if( c2=='^' ){ invert = 1; c2 = *++zPattern; }
        if( c2==']' ){
          if( c==']' ) seen = 1;
          c2 = *++zPattern;
        }
        while( (c2 = sqliteCharVal(zPattern))!=0 && c2!=']' ){
          if( c2=='-' && zPattern[1]!=']' && zPattern[1]!=0 && prior_c>0 ){
            zPattern++;
            c2 = sqliteCharVal(zPattern);
            if( c>=prior_c && c<=c2 ) seen = 1;
            prior_c = 0;
          }else if( c==c2 ){
            seen = 1;
            prior_c = c2;
          }else{
            prior_c = c2;
          }
          sqliteNextChar(zPattern);
        }
        if( c2==0 || (seen ^ invert)==0 ) return 0;
        sqliteNextChar(zString);
        zPattern++;
        break;
      }
      default: {
        if( c != *zString ) return 0;
        zPattern++;
        zString++;
        break;
      }
    }
  }
  return *zString==0;
}

/*
** Compare two UTF-8 strings for equality using the "LIKE" operator of
** SQL.  The '%' character matches any sequence of 0 or more
** characters and '_' matches any single character.  Case is
** not significant.
**
** This routine is just an adaptation of the sqliteGlobCompare()
** routine above.
*/
int 
sqliteLikeCompare(const unsigned char *zPattern, const unsigned char *zString){
  register int c;
  int c2;

  while( (c = UpperToLower[*zPattern])!=0 ){
    switch( c ){
      case '%': {
        while( (c=zPattern[1]) == '%' || c == '_' ){
          if( c=='_' ){
            if( *zString==0 ) return 0;
            sqliteNextChar(zString);
          }
          zPattern++;
        }
        if( c==0 ) return 1;
        c = UpperToLower[c];
        while( (c2=UpperToLower[*zString])!=0 ){
          while( c2 != 0 && c2 != c ){ c2 = UpperToLower[*++zString]; }
          if( c2==0 ) return 0;
          if( sqliteLikeCompare(&zPattern[1],zString) ) return 1;
          sqliteNextChar(zString);
        }
        return 0;
      }
      case '_': {
        if( *zString==0 ) return 0;
        sqliteNextChar(zString);
        zPattern++;
        break;
      }
      default: {
        if( c != UpperToLower[*zString] ) return 0;
        zPattern++;
        zString++;
        break;
      }
    }
  }
  return *zString==0;
}

/*
** Return a static string that describes the kind of error specified in the
** argument.
*/
const char *sqliteErrStr(int rc){
  const char *z;
  switch( rc ){
    case SQLITE_OK:         z = "not an error";                          break;
    case SQLITE_ERROR:      z = "SQL logic error or missing database";   break;
    case SQLITE_INTERNAL:   z = "internal SQLite implementation flaw";   break;
    case SQLITE_PERM:       z = "access permission denied";              break;
    case SQLITE_ABORT:      z = "callback requested query abort";        break;
    case SQLITE_BUSY:       z = "database is locked";                    break;
    case SQLITE_LOCKED:     z = "database table is locked";              break;
    case SQLITE_NOMEM:      z = "out of memory";                         break;
    case SQLITE_READONLY:   z = "attempt to write a readonly database";  break;
    case SQLITE_INTERRUPT:  z = "interrupted";                           break;
    case SQLITE_IOERR:      z = "disk I/O error";                        break;
    case SQLITE_CORRUPT:    z = "database disk image is malformed";      break;
    case SQLITE_NOTFOUND:   z = "table or record not found";             break;
    case SQLITE_FULL:       z = "database is full";                      break;
    case SQLITE_CANTOPEN:   z = "unable to open database file";          break;
    case SQLITE_PROTOCOL:   z = "database locking protocol failure";     break;
    case SQLITE_EMPTY:      z = "table contains no data";                break;
    case SQLITE_SCHEMA:     z = "database schema has changed";           break;
    case SQLITE_TOOBIG:     z = "too much data for one table row";       break;
    case SQLITE_CONSTRAINT: z = "constraint failed";                     break;
    default:                z = "unknown error";                         break;
  }
  return z;
}
