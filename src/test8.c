/*
** 2006 June 10
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Code for testing the virtual table interfaces.  This code
** is not included in the SQLite library.  It is used for automated
** testing of the SQLite library.
**
** $Id$
*/
#include "sqliteInt.h"
#include "tcl.h"
#include "os.h"
#include <stdlib.h>
#include <string.h>

typedef struct echo_vtab echo_vtab;
typedef struct echo_cursor echo_cursor;

/*
** The test module defined in this file uses two global Tcl variables to
** commicate with test-scripts:
**
**     $::echo_module
**     $::echo_module_sync_fail
*/

/* 
** An echo virtual-table object.
**
** echo.vtab.aIndex is an array of booleans. The nth entry is true if 
** the nth column of the real table is the left-most column of an index
** (implicit or otherwise). In other words, if SQLite can optimize
** a query like "SELECT * FROM real_table WHERE col = ?".
**
** Member variable aCol[] contains copies of the column names of the real
** table.
*/
struct echo_vtab {
  sqlite3_vtab base;
  Tcl_Interp *interp;
  sqlite3 *db;

  char *zTableName;       /* Name of the real table */
  char *zLogName;         /* Name of the log table */
  int nCol;               /* Number of columns in the real table */
  int *aIndex;            /* Array of size nCol. True if column has an index */
  char **aCol;            /* Array of size nCol. Column names */
};

/* An echo cursor object */
struct echo_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_stmt *pStmt;
  int errcode;                 /* Error code */
};

static int getColumnNames(
  sqlite3 *db, 
  const char *zTab,
  char ***paCol, 
  int *pnCol
){
  char **aCol = 0;
  char zBuf[1024];
  sqlite3_stmt *pStmt = 0;
  int rc = SQLITE_OK;
  int nCol = 0;

  sprintf(zBuf, "SELECT * FROM %s", zTab);
  rc = sqlite3_prepare(db, zBuf, -1, &pStmt, 0);
  if( rc==SQLITE_OK ){
    int ii;
    nCol = sqlite3_column_count(pStmt);
    aCol = sqliteMalloc(sizeof(char *) * nCol);
    if( !aCol ){
      rc = SQLITE_NOMEM;
      goto fail;
    }
    for(ii=0; ii<nCol; ii++){
      aCol[ii] = sqlite3StrDup(sqlite3_column_name(pStmt, ii));
      if( !aCol[ii] ){
        rc = SQLITE_NOMEM;
        goto fail;
      }
    }
  }

  *paCol = aCol;
  *pnCol = nCol;

fail:
  sqlite3_finalize(pStmt);
  if( rc!=SQLITE_OK && aCol ){
    int ii;
    for(ii=0; ii<nCol; ii++){
      sqliteFree(aCol[ii]);
    }
    sqliteFree(aCol);
  }
  return rc;
}

static int getIndexArray(sqlite3 *db, const char *zTab, int **paIndex){
  char zBuf[1024];
  sqlite3_stmt *pStmt = 0;
  int nCol;
  int *aIndex = 0;
  int rc;

  sprintf(zBuf, "SELECT * FROM %s", zTab);
  rc = sqlite3_prepare(db, zBuf, -1, &pStmt, 0);
  nCol = sqlite3_column_count(pStmt);

  sqlite3_finalize(pStmt);
  pStmt = 0;
  if( rc!=SQLITE_OK ){
    goto get_index_array_out;
  }

  aIndex = (int *)sqliteMalloc(sizeof(int) * nCol);
  if( !aIndex ){
    rc = SQLITE_NOMEM;
    goto get_index_array_out;
  }

  sprintf(zBuf, "PRAGMA index_list(%s)", zTab);
  rc = sqlite3_prepare(db, zBuf, -1, &pStmt, 0);

  while( pStmt && sqlite3_step(pStmt)==SQLITE_ROW ){
    sqlite3_stmt *pStmt2 = 0;
    sprintf(zBuf, "PRAGMA index_info(%s)", sqlite3_column_text(pStmt, 1));
    rc = sqlite3_prepare(db, zBuf, -1, &pStmt2, 0);
    if( pStmt2 && sqlite3_step(pStmt2)==SQLITE_ROW ){
      int cid = sqlite3_column_int(pStmt2, 1);
      assert( cid>=0 && cid<nCol );
      aIndex[cid] = 1;
    }
    if( pStmt2 ){
      rc = sqlite3_finalize(pStmt2);
    }
    if( rc!=SQLITE_OK ){
      sqlite3_finalize(pStmt);
      goto get_index_array_out;
    }
  }

  if( pStmt ){
    rc = sqlite3_finalize(pStmt);
  }

get_index_array_out:
  if( rc!=SQLITE_OK ){
    sqliteFree(aIndex);
    aIndex = 0;
  }
  *paIndex = aIndex;
  return rc;
}

/*
** Global Tcl variable $echo_module is a list. This routine appends
** the string element zArg to that list in interpreter interp.
*/
static void appendToEchoModule(Tcl_Interp *interp, const char *zArg){
  int flags = (TCL_APPEND_VALUE | TCL_LIST_ELEMENT | TCL_GLOBAL_ONLY);
  Tcl_SetVar(interp, "echo_module", (zArg?zArg:""), flags);
}

/*
** This function is called from within the echo-modules xCreate and
** xConnect methods. The argc and argv arguments are copies of those 
** passed to the calling method. This function is responsible for
** calling sqlite3_declare_vtab() to declare the schema of the virtual
** table being created or connected.
**
** If the constructor was passed just one argument, i.e.:
**
**   CREATE TABLE t1 AS echo(t2);
**
** Then t2 is assumed to be the name of a *real* database table. The
** schema of the virtual table is declared by passing a copy of the 
** CREATE TABLE statement for the real table to sqlite3_declare_vtab().
** Hence, the virtual table should have exactly the same column names and 
** types as the real table.
*/
static int echoDeclareVtab(
  echo_vtab *pVtab, 
  sqlite3 *db, 
  int argc, 
  char **argv
){
  int rc = SQLITE_OK;

  if( argc>=4 ){
    sqlite3_stmt *pStmt = 0;
    sqlite3_prepare(db, 
        "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = ?",
        -1, &pStmt, 0);
    sqlite3_bind_text(pStmt, 1, argv[3], -1, 0);
    if( sqlite3_step(pStmt)==SQLITE_ROW ){
      const char *zCreateTable = sqlite3_column_text(pStmt, 0);
#ifndef SQLITE_OMIT_VIRTUALTABLE
      sqlite3_declare_vtab(db, zCreateTable);
#endif
      rc = sqlite3_finalize(pStmt);
    } else {
      rc = sqlite3_finalize(pStmt);
      if( rc==SQLITE_OK ){ 
        rc = SQLITE_ERROR;
      }
    }

    if( rc==SQLITE_OK ){
      rc = getIndexArray(db, argv[3], &pVtab->aIndex);
    }
    if( rc==SQLITE_OK ){
      rc = getColumnNames(db, argv[3], &pVtab->aCol, &pVtab->nCol);
    }
  }

  return rc;
}

static int echoDestructor(sqlite3_vtab *pVtab){
  int ii;
  echo_vtab *p = (echo_vtab*)pVtab;
  sqliteFree(p->aIndex);
  for(ii=0; ii<p->nCol; ii++){
    sqliteFree(p->aCol[ii]);
  }
  sqliteFree(p->aCol);
  sqliteFree(p->zTableName);
  sqliteFree(p->zLogName);
  sqliteFree(p);
  return 0;
}

static int echoConstructor(
  sqlite3 *db,
  void *pAux,
  int argc, char **argv,
  sqlite3_vtab **ppVtab
){
  int i;
  echo_vtab *pVtab;

  pVtab = sqliteMalloc( sizeof(*pVtab) );
  if( !pVtab ){
    return SQLITE_NOMEM;
  }
  pVtab->interp = (Tcl_Interp *)pAux;
  pVtab->db = db;
  pVtab->zTableName = sqlite3MPrintf("%s", argv[3]);
  if( !pVtab->zTableName ){
    echoDestructor((sqlite3_vtab *)pVtab);
    return SQLITE_NOMEM;
  }

  for(i=0; i<argc; i++){
    appendToEchoModule(pVtab->interp, argv[i]);
  }

  if( echoDeclareVtab(pVtab, db, argc, argv) ){
    echoDestructor((sqlite3_vtab *)pVtab);
    return SQLITE_ERROR;
  }

  *ppVtab = &pVtab->base;
  return SQLITE_OK;
}

/* Methods for the echo module */
static int echoCreate(
  sqlite3 *db,
  void *pAux,
  int argc, char **argv,
  sqlite3_vtab **ppVtab
){
  int rc = SQLITE_OK;
  appendToEchoModule((Tcl_Interp *)(pAux), "xCreate");
  rc = echoConstructor(db, pAux, argc, argv, ppVtab);
#if 1
  if( rc==SQLITE_OK && argc==5 ){
    char *zSql;
    echo_vtab *pVtab = *(echo_vtab **)ppVtab;
    pVtab->zLogName = sqlite3MPrintf("%s", argv[4]);
    zSql = sqlite3MPrintf("CREATE TABLE %Q(logmsg)", pVtab->zLogName);
    rc = sqlite3_exec(db, zSql, 0, 0, 0);
    sqliteFree(zSql);
  }
#endif
  return rc;
}
static int echoConnect(
  sqlite3 *db,
  void *pAux,
  int argc, char **argv,
  sqlite3_vtab **ppVtab
){
  appendToEchoModule((Tcl_Interp *)(pAux), "xConnect");
  return echoConstructor(db, pAux, argc, argv, ppVtab);
}

static int echoDisconnect(sqlite3_vtab *pVtab){
  appendToEchoModule(((echo_vtab *)pVtab)->interp, "xDisconnect");
  return echoDestructor(pVtab);
}
static int echoDestroy(sqlite3_vtab *pVtab){
  int rc = SQLITE_OK;
  echo_vtab *p = (echo_vtab *)pVtab;
  appendToEchoModule(((echo_vtab *)pVtab)->interp, "xDestroy");
#if 1
  if( p && p->zLogName ){
    char *zSql;
    zSql = sqlite3MPrintf("DROP TABLE %Q", p->zLogName);
    rc = sqlite3_exec(p->db, zSql, 0, 0, 0);
    sqliteFree(zSql);
  }
#endif
  if( rc==SQLITE_OK ){
    rc = echoDestructor(pVtab);
  }
  return rc;
}

static int echoOpen(sqlite3_vtab *pVTab, sqlite3_vtab_cursor **ppCursor){
  echo_cursor *pCur;
  pCur = sqliteMalloc(sizeof(echo_cursor));
  *ppCursor = (sqlite3_vtab_cursor *)pCur;
  return (pCur ? SQLITE_OK : SQLITE_NOMEM);
}

static int echoClose(sqlite3_vtab_cursor *cur){
  int rc;
  echo_cursor *pCur = (echo_cursor *)cur;
  sqlite3_stmt *pStmt = pCur->pStmt;
  pCur->pStmt = 0;
  sqliteFree(pCur);
  rc = sqlite3_finalize(pStmt);
  return rc;
}

/*
** Return non-zero if the cursor does not currently point to a valid record
** (i.e if the scan has finished), or zero otherwise.
*/
static int echoEof(sqlite3_vtab_cursor *cur){
  return (((echo_cursor *)cur)->pStmt ? 0 : 1);
}

static int echoNext(sqlite3_vtab_cursor *cur){
  int rc;
  echo_cursor *pCur = (echo_cursor *)cur;
  sqlite3_stmt *pStmt = pCur->pStmt;
  rc = sqlite3_step(pCur->pStmt);

  if( rc==SQLITE_ROW ){
    rc = SQLITE_OK;
  }else{
    rc = sqlite3_finalize(pCur->pStmt);
    pCur->pStmt = 0;
  }

  return rc;
}

static int echoColumn(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int i){
  int iCol = i + 1;
  sqlite3_stmt *pStmt = ((echo_cursor *)cur)->pStmt;
  if( ((echo_cursor *)cur)->errcode ){
    return ((echo_cursor *)cur)->errcode;
  }
  if( !pStmt ){
    sqlite3_result_null(ctx);
  }else{
    assert( sqlite3_data_count(pStmt)>iCol );
    sqlite3_result_value(ctx, sqlite3_column_value(pStmt, iCol));
  }
  return SQLITE_OK;
}

static int echoRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid){
  sqlite3_stmt *pStmt = ((echo_cursor *)cur)->pStmt;
  *pRowid = sqlite3_column_int64(pStmt, 0);
  return SQLITE_OK;
}

/*
** Compute a simple hash of the null terminated string zString.
**
** This module uses only sqlite3_index_info.idxStr, not 
** sqlite3_index_info.idxNum. So to test idxNum, when idxStr is set
** in echoBestIndex(), idxNum is set to the corresponding hash value.
** In echoFilter(), code assert()s that the supplied idxNum value is
** indeed the hash of the supplied idxStr.
*/
static int hashString(const char *zString){
  int val = 0;
  int ii;
  for(ii=0; zString[ii]; ii++){
    val = (val << 3) + (int)zString[ii];
  }
  return val;
}


static int echoFilter(
  sqlite3_vtab_cursor *pVtabCursor, 
  int idxNum, const char *idxStr,
  int argc, sqlite3_value **argv
){
  int rc;
  int i;

  echo_cursor *pCur = (echo_cursor *)pVtabCursor;
  echo_vtab *pVtab = (echo_vtab *)pVtabCursor->pVtab;
  sqlite3 *db = pVtab->db;

  appendToEchoModule(pVtab->interp, "xFilter");
  appendToEchoModule(pVtab->interp, idxStr);
  for(i=0; i<argc; i++){
    appendToEchoModule(pVtab->interp, sqlite3_value_text(argv[i]));
  }

  assert( idxNum==hashString(idxStr) );
  sqlite3_finalize(pCur->pStmt);
  pCur->pStmt = 0;
  rc = sqlite3_prepare(db, idxStr, -1, &pCur->pStmt, 0);
  assert( pCur->pStmt || rc!=SQLITE_OK );
  for(i=0; rc==SQLITE_OK && i<argc; i++){
    switch( sqlite3_value_type(argv[i]) ){
      case SQLITE_INTEGER: {
        sqlite3_bind_int64(pCur->pStmt, i+1, sqlite3_value_int64(argv[i]));
        break;
      }
      case SQLITE_FLOAT: {
        sqlite3_bind_double(pCur->pStmt, i+1, sqlite3_value_double(argv[i]));
        break;
      }
      case SQLITE_NULL: {
        sqlite3_bind_null(pCur->pStmt, i+1);
        break;
      }
      case SQLITE_TEXT: {
        sqlite3_bind_text(pCur->pStmt, i+1, sqlite3_value_text(argv[i]),
                          sqlite3_value_bytes(argv[i]), SQLITE_TRANSIENT);
        break;
      }
      case SQLITE_BLOB: {
        sqlite3_bind_blob(pCur->pStmt, i+1, sqlite3_value_blob(argv[i]),
                          sqlite3_value_bytes(argv[i]), SQLITE_TRANSIENT);
        break;
      }
    }
  }
  if( rc==SQLITE_OK ){
    rc = echoNext(pVtabCursor);
  }else{
    assert( !pCur->pStmt );
  }

  return rc;
}

/*
** The echo module implements the subset of query constraints and sort
** orders that may take advantage of SQLite indices on the underlying
** real table. For example, if the real table is declared as:
**
**     CREATE TABLE real(a, b, c);
**     CREATE INDEX real_index ON real(b);
**
** then the echo module handles WHERE or ORDER BY clauses that refer
** to the column "b", but not "a" or "c". If a multi-column index is
** present, only it's left most column is considered. 
*/
static int echoBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo){
  int ii;
  char *zQuery = 0;
  char *zNew;
  int nArg = 0;
  const char *zSep = "WHERE";
  echo_vtab *pVtab = (echo_vtab *)tab;
  sqlite3_stmt *pStmt = 0;

  int nRow;
  int useIdx = 0;
  int rc = SQLITE_OK;

  /* Determine the number of rows in the table and store this value in local
  ** variable nRow. The 'estimated-cost' of the scan will be the number of
  ** rows in the table for a linear scan, or the log (base 2) of the 
  ** number of rows if the proposed scan uses an index.  
  */
  zQuery = sqlite3_mprintf("SELECT count(*) FROM %Q", pVtab->zTableName);
  rc = sqlite3_prepare(pVtab->db, zQuery, -1, &pStmt, 0);
  if( rc!=SQLITE_OK ){
    return rc;
  }
  sqlite3_step(pStmt);
  nRow = sqlite3_column_int(pStmt, 0);
  rc = sqlite3_finalize(pStmt);
  if( rc!=SQLITE_OK ){
    return rc;
  }

  zQuery = sqlite3_mprintf("SELECT rowid, * FROM %Q", pVtab->zTableName);
  for(ii=0; ii<pIdxInfo->nConstraint; ii++){
    const struct sqlite3_index_constraint *pConstraint;
    struct sqlite3_index_constraint_usage *pUsage;

    pConstraint = &pIdxInfo->aConstraint[ii];
    pUsage = &pIdxInfo->aConstraintUsage[ii];

    int iCol = pConstraint->iColumn;
    if( pVtab->aIndex[iCol] ){
      char *zCol = pVtab->aCol[iCol];
      char *zOp = 0;
      useIdx = 1;
      if( iCol<0 ){
        zCol = "rowid";
      }
      switch( pConstraint->op ){
        case SQLITE_INDEX_CONSTRAINT_EQ:
          zOp = "="; break;
        case SQLITE_INDEX_CONSTRAINT_LT:
          zOp = "<"; break;
        case SQLITE_INDEX_CONSTRAINT_GT:
          zOp = ">"; break;
        case SQLITE_INDEX_CONSTRAINT_LE:
          zOp = "<="; break;
        case SQLITE_INDEX_CONSTRAINT_GE:
          zOp = ">="; break;
        case SQLITE_INDEX_CONSTRAINT_MATCH:
          zOp = "LIKE"; break;
      }
      if( zOp[0]=='L' ){
        zNew = sqlite3_mprintf("%s %s %s LIKE (SELECT '%%'||?||'%%')", 
                               zQuery, zSep, zCol);
      } else {
        zNew = sqlite3_mprintf("%s %s %s %s ?", zQuery, zSep, zCol, zOp);
      }
      sqlite3_free(zQuery);
      zQuery = zNew;
      zSep = "AND";
      pUsage->argvIndex = ++nArg;
      pUsage->omit = 1;
    }
  }

  /* If there is only one term in the ORDER BY clause, and it is
  ** on a column that this virtual table has an index for, then consume 
  ** the ORDER BY clause.
  */
  if( pIdxInfo->nOrderBy==1 && pVtab->aIndex[pIdxInfo->aOrderBy->iColumn] ){
    char *zCol = pVtab->aCol[pIdxInfo->aOrderBy->iColumn];
    char *zDir = pIdxInfo->aOrderBy->desc?"DESC":"ASC";
    zNew = sqlite3_mprintf("%s ORDER BY %s %s", zQuery, zCol, zDir);
    sqlite3_free(zQuery);
    zQuery = zNew;
    pIdxInfo->orderByConsumed = 1;
  }

  appendToEchoModule(pVtab->interp, "xBestIndex");;
  appendToEchoModule(pVtab->interp, zQuery);

  pIdxInfo->idxNum = hashString(zQuery);
  pIdxInfo->idxStr = zQuery;
  pIdxInfo->needToFreeIdxStr = 1;
  if( useIdx ){
    /* Approximation of log2(nRow). */
    for( ii=0; ii<(sizeof(int)*8); ii++ ){
      if( nRow & (1<<ii) ){
        pIdxInfo->estimatedCost = (double)ii;
      }
    }
  } else {
    pIdxInfo->estimatedCost = (double)nRow;
  }
  return rc;
}

static void string_concat(char **pzStr, char *zAppend, int doFree){
  char *zIn = *pzStr;
  if( zIn ){
    char *zTemp = zIn;
    zIn = sqlite3_mprintf("%s%s", zIn, zAppend);
    sqlite3_free(zTemp);
  }else{
    zIn = sqlite3_mprintf("%s", zAppend);
  }
  *pzStr = zIn;
  if( doFree ){
    sqlite3_free(zAppend);
  }
}

/*
**    apData[0]  apData[1]  apData[2..]
**
**    INTEGER                              DELETE            
**
**    INTEGER    NULL       (nCol args)    UPDATE (do not set rowid)
**    INTEGER    INTEGER    (nCol args)    UPDATE (with SET rowid = <arg1>)
**
**    NULL       NULL       (nCol args)    INSERT INTO (automatic rowid value)
**    NULL       INTEGER    (nCol args)    INSERT (incl. rowid value)
**
*/
int echoUpdate(
  sqlite3_vtab *tab, 
  int nData, 
  sqlite3_value **apData, 
  sqlite_int64 *pRowid
){
  echo_vtab *pVtab = (echo_vtab *)tab;
  sqlite3 *db = pVtab->db;
  int rc = SQLITE_OK;

  sqlite3_stmt *pStmt;
  char *z = 0;               /* SQL statement to execute */
  int bindArgZero = 0;       /* True to bind apData[0] to sql var no. nData */
  int bindArgOne = 0;        /* True to bind apData[1] to sql var no. 1 */
  int i;                     /* Counter variable used by for loops */

  assert( nData==pVtab->nCol+2 || nData==1 );

  /* If apData[0] is an integer and nData>1 then do an UPDATE */
  if( nData>1 && sqlite3_value_type(apData[0])==SQLITE_INTEGER ){
    z = sqlite3_mprintf("UPDATE %Q", pVtab->zTableName);
    char *zSep = " SET";

    bindArgOne = (apData[1] && sqlite3_value_type(apData[1])==SQLITE_INTEGER);
    bindArgZero = 1;

    if( bindArgOne ){
       string_concat(&z, " SET rowid=?1 ", 0);
       zSep = ",";
    }
    for(i=2; i<nData; i++){
      if( apData[i]==0 ) continue;
      string_concat(&z, sqlite3_mprintf(
          "%s %Q=?%d", zSep, pVtab->aCol[i-2], i), 1);
      zSep = ",";
    }
    string_concat(&z, sqlite3_mprintf(" WHERE rowid=?%d", nData), 0);
  }

  /* If apData[0] is an integer and nData==1 then do a DELETE */
  else if( nData==1 && sqlite3_value_type(apData[0])==SQLITE_INTEGER ){
    z = sqlite3_mprintf("DELETE FROM %Q WHERE rowid = ?1", pVtab->zTableName);
    bindArgZero = 1;
  }

  /* If the first argument is NULL and there are more than two args, INSERT */
  else if( nData>2 && sqlite3_value_type(apData[0])==SQLITE_NULL ){
    int ii;
    char *zInsert = 0;
    char *zValues = 0;
  
    zInsert = sqlite3_mprintf("INSERT INTO %Q (", pVtab->zTableName);
    if( sqlite3_value_type(apData[1])==SQLITE_INTEGER ){
      bindArgOne = 1;
      zValues = sqlite3_mprintf("?");
      string_concat(&zInsert, "rowid", 0);
    }

    assert((pVtab->nCol+2)==nData);
    for(ii=2; ii<nData; ii++){
      string_concat(&zInsert, 
          sqlite3_mprintf("%s%Q", zValues?", ":"", pVtab->aCol[ii-2]), 1);
      string_concat(&zValues, 
          sqlite3_mprintf("%s?%d", zValues?", ":"", ii), 1);
    }

    string_concat(&z, zInsert, 1);
    string_concat(&z, ") VALUES(", 0);
    string_concat(&z, zValues, 1);
    string_concat(&z, ")", 0);
  }

  /* Anything else is an error */
  else{
    assert(0);
    return SQLITE_ERROR;
  }

  rc = sqlite3_prepare(db, z, -1, &pStmt, 0);
  assert( rc!=SQLITE_OK || pStmt );
  sqlite3_free(z);
  if( rc==SQLITE_OK ) {
    if( bindArgZero ){
      sqlite3_bind_value(pStmt, nData, apData[0]);
    }
    if( bindArgOne ){
      sqlite3_bind_value(pStmt, 1, apData[1]);
    }
    for(i=2; i<nData; i++){
      if( apData[i] ) sqlite3_bind_value(pStmt, i, apData[i]);
    }
    sqlite3_step(pStmt);
    rc = sqlite3_finalize(pStmt);
  }

  if( pRowid && rc==SQLITE_OK ){
    *pRowid = sqlite3_last_insert_rowid(db);
  }

  return rc;
}

/*
** xBegin, xSync, xCommit and xRollback callbacks for echo module
** virtual tables. Do nothing other than add the name of the callback
** to the $::echo_module Tcl variable.
*/
static int echoTransactionCall(sqlite3_vtab *tab, const char *zCall){
  char *z;
  echo_vtab *pVtab = (echo_vtab *)tab;
  z = sqlite3_mprintf("echo(%s)", pVtab->zTableName);
  appendToEchoModule(pVtab->interp, zCall);
  appendToEchoModule(pVtab->interp, z);
  sqlite3_free(z);
  return SQLITE_OK;
}
static int echoBegin(sqlite3_vtab *tab){
  return echoTransactionCall(tab, "xBegin");
}
static int echoSync(sqlite3_vtab *tab){
  echo_vtab *pVtab = (echo_vtab *)tab;
  Tcl_Interp *interp = pVtab->interp;
  const char *zVal; 

  echoTransactionCall(tab, "xSync");

  /* Check if the $::echo_module_sync_fail variable is defined. If it is,
  ** and it is set to the name of the real table underlying this virtual
  ** echo module table, then cause this xSync operation to fail.
  */
  zVal = Tcl_GetVar(interp, "echo_module_sync_fail", TCL_GLOBAL_ONLY);
  if( zVal && 0==strcmp(zVal, pVtab->zTableName) ){
    return -1;
  }
  return SQLITE_OK;
}
static int echoCommit(sqlite3_vtab *tab){
  return echoTransactionCall(tab, "xCommit");
}
static int echoRollback(sqlite3_vtab *tab){
  return echoTransactionCall(tab, "xRollback");
}

/*
** A virtual table module that merely echos method calls into TCL
** variables.
*/
static sqlite3_module echoModule = {
  0,                         /* iVersion */
  "echo",                    /* zName */
  echoCreate,
  echoConnect,
  echoBestIndex,
  echoDisconnect, 
  echoDestroy,
  echoOpen,                  /* xOpen - open a cursor */
  echoClose,                 /* xClose - close a cursor */
  echoFilter,                /* xFilter - configure scan constraints */
  echoNext,                  /* xNext - advance a cursor */
  echoEof,                   /* xEof */
  echoColumn,                /* xColumn - read data */
  echoRowid,                 /* xRowid - read data */
  echoUpdate,                /* xUpdate - write data */
  echoBegin,                 /* xBegin - begin transaction */
  echoSync,                  /* xSync - sync transaction */
  echoCommit,                /* xCommit - commit transaction */
  echoRollback               /* xRollback - rollback transaction */
};

/*
** Decode a pointer to an sqlite3 object.
*/
static int getDbPointer(Tcl_Interp *interp, const char *zA, sqlite3 **ppDb){
  *ppDb = (sqlite3*)sqlite3TextToPtr(zA);
  return TCL_OK;
}


/*
** Register the echo virtual table module.
*/
static int register_echo_module(
  ClientData clientData, /* Pointer to sqlite3_enable_XXX function */
  Tcl_Interp *interp,    /* The TCL interpreter that invoked this command */
  int objc,              /* Number of arguments */
  Tcl_Obj *CONST objv[]  /* Command arguments */
){
  sqlite3 *db;
  if( objc!=2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "DB");
    return TCL_ERROR;
  }
  if( getDbPointer(interp, Tcl_GetString(objv[1]), &db) ) return TCL_ERROR;
#ifndef SQLITE_OMIT_VIRTUALTABLE
  sqlite3_create_module(db, "echo", &echoModule, (void *)interp);
#endif
  return TCL_OK;
}


/*
** Register commands with the TCL interpreter.
*/
int Sqlitetest8_Init(Tcl_Interp *interp){
  static struct {
     char *zName;
     Tcl_ObjCmdProc *xProc;
     void *clientData;
  } aObjCmd[] = {
     { "register_echo_module",   register_echo_module, 0 },
  };
  int i;
  for(i=0; i<sizeof(aObjCmd)/sizeof(aObjCmd[0]); i++){
    Tcl_CreateObjCommand(interp, aObjCmd[i].zName, 
        aObjCmd[i].xProc, aObjCmd[i].clientData, 0);
  }
  return TCL_OK;
}
