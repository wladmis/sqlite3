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
** This file contains code to implement a pseudo-random number
** generator (PRNG) for SQLite.
**
** Random numbers are used by some of the database backends in order
** to generate random integer keys for tables or random filenames.
**
** $Id$
*/
#include "sqliteInt.h"
#include <time.h>

/*
** Get a single 8-bit random value from the RC4 PRNG.
*/
int sqliteRandomByte(void){
  int t;

  /*
  ** The following structure holds the current state of the RC4 algorithm.
  ** We use RC4 as a random number generator.  Each call to RC4 gives
  ** a random 8-bit number.
  **
  ** Nothing in this file or anywhere else in SQLite does any kind of
  ** encryption.  The RC4 algorithm is being used as a PRNG (pseudo-random
  ** number generator) not as an encryption device.
  */
  static struct {
    int isInit;
    int i, j;
    int s[256];
  } prng_state;
 
  /* Initialize the state of the random number generator once,
  ** the first time this routine is called.  The seed value does
  ** not need to contain a lot of randomness since we are not
  ** trying to do secure encryption or anything like that...
  */
  if( !prng_state.isInit ){
    int i;
    static char seed[] = "    sqlite random seed";
    char k[256];
    time((time_t*)seed);
    prng_state.j = 0;
    prng_state.i = 0;
    for(i=0; i<256; i++){
      prng_state.s[i] = i;
      k[i] = seed[i%sizeof(seed)];
    }
    for(i=0; i<256; i++){
      int t;
      prng_state.j = (prng_state.j + prng_state.s[i] + k[i]) & 0xff;
      t = prng_state.s[prng_state.j];
      prng_state.s[prng_state.j] = prng_state.s[i];
      prng_state.s[i] = t;
    }
    prng_state.isInit = 1;
  }

  /* Generate and return single random byte
  */
  prng_state.i = (prng_state.i + 1) & 0xff;
  prng_state.j = (prng_state.j + prng_state.s[prng_state.i]) & 0xff;
  t = prng_state.s[prng_state.i];
  prng_state.s[prng_state.i] = prng_state.s[prng_state.j];
  prng_state.s[prng_state.j] = t;
  t = prng_state.s[prng_state.i] + prng_state.s[prng_state.j];
  return prng_state.s[t & 0xff];
}

/*
** Return a random 32-bit integer.  The integer is generated by making
** 4 calls to sqliteRandomByte().
*/
int sqliteRandomInteger(void){
  int r;
  int i;
  r = sqliteRandomByte();
  for(i=1; i<4; i++){
    r = (r<<8) + sqliteRandomByte();
  }
  return r;
}

/*
** Return a random 16-bit unsigned integer.  The integer is generated by
** making 2 calls to sqliteRandomByte().
*/
int sqliteRandomShort(void){
  int r;
  r = sqliteRandomByte();
  r = (r<<8) + sqliteRandomByte();
  return r;
}

/*
** Generate a random filename with the given prefix.  The new filename
** is written into zBuf[].  The calling function must insure that
** zBuf[] is big enough to hold the prefix plus 20 or so extra
** characters.
**
** Very random names are chosen so that the chance of a
** collision with an existing filename is very very small.
*/
void sqliteRandomName(char *zBuf, char *zPrefix){
  int i, j;
  static const char zRandomChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  strcpy(zBuf, zPrefix);
  j = strlen(zBuf);
  for(i=0; i<15; i++){
    int c = sqliteRandomByte() % (sizeof(zRandomChars) - 1);
    zBuf[j++] = zRandomChars[c];
  }
  zBuf[j] = 0;
}
