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
** This file contains C code routines that are called by the parser
** to handle UPDATE statements.
**
** $Id$
*/
#include "sqliteInt.h"

/*
** Process an UPDATE statement.
*/
void sqliteUpdate(
  Parse *pParse,         /* The parser context */
  Token *pTableName,     /* The table in which we should change things */
  ExprList *pChanges,    /* Things to be changed */
  Expr *pWhere           /* The WHERE clause.  May be null */
){
  int i, j;              /* Loop counters */
  Table *pTab;           /* The table to be updated */
  IdList *pTabList = 0;  /* List containing only pTab */
  int end, addr;         /* A couple of addresses in the generated code */
  WhereInfo *pWInfo;     /* Information about the WHERE clause */
  Vdbe *v;               /* The virtual database engine */
  Index *pIdx;           /* For looping over indices */
  int nIdx;              /* Number of indices that need updating */
  int base;              /* Index of first available table cursor */
  sqlite *db;            /* The database structure */
  Index **apIdx = 0;     /* An array of indices that need updating too */
  int *aXRef = 0;        /* aXRef[i] is the index in pChanges->a[] of the
                         ** an expression for the i-th column of the table.
                         ** aXRef[i]==-1 if the i-th column is not changed. */
  int openOp;            /* Opcode used to open tables */

  if( pParse->nErr || sqlite_malloc_failed ) goto update_cleanup;
  db = pParse->db;

  /* Locate the table which we want to update.  This table has to be
  ** put in an IdList structure because some of the subroutines we
  ** will be calling are designed to work with multiple tables and expect
  ** an IdList* parameter instead of just a Table* parameger.
  */
  pTabList = sqliteIdListAppend(0, pTableName);
  if( pTabList==0 ) goto update_cleanup;
  for(i=0; i<pTabList->nId; i++){
    pTabList->a[i].pTab = sqliteFindTable(db, pTabList->a[i].zName);
    if( pTabList->a[i].pTab==0 ){
      sqliteSetString(&pParse->zErrMsg, "no such table: ", 
         pTabList->a[i].zName, 0);
      pParse->nErr++;
      goto update_cleanup;
    }
    if( pTabList->a[i].pTab->readOnly ){
      sqliteSetString(&pParse->zErrMsg, "table ", pTabList->a[i].zName,
        " may not be modified", 0);
      pParse->nErr++;
      goto update_cleanup;
    }
  }
  pTab = pTabList->a[0].pTab;
  aXRef = sqliteMalloc( sizeof(int) * pTab->nCol );
  if( aXRef==0 ) goto update_cleanup;
  for(i=0; i<pTab->nCol; i++) aXRef[i] = -1;

  /* Resolve the column names in all the expressions in both the
  ** WHERE clause and in the new values.  Also find the column index
  ** for each column to be updated in the pChanges array.
  */
  if( pWhere ){
    sqliteExprResolveInSelect(pParse, pWhere);
  }
  for(i=0; i<pChanges->nExpr; i++){
    sqliteExprResolveInSelect(pParse, pChanges->a[i].pExpr);
  }
  if( pWhere ){
    if( sqliteExprResolveIds(pParse, pTabList, pWhere) ){
      goto update_cleanup;
    }
    if( sqliteExprCheck(pParse, pWhere, 0, 0) ){
      goto update_cleanup;
    }
  }
  for(i=0; i<pChanges->nExpr; i++){
    if( sqliteExprResolveIds(pParse, pTabList, pChanges->a[i].pExpr) ){
      goto update_cleanup;
    }
    if( sqliteExprCheck(pParse, pChanges->a[i].pExpr, 0, 0) ){
      goto update_cleanup;
    }
    for(j=0; j<pTab->nCol; j++){
      if( sqliteStrICmp(pTab->aCol[j].zName, pChanges->a[i].zName)==0 ){
        aXRef[j] = i;
        break;
      }
    }
    if( j>=pTab->nCol ){
      sqliteSetString(&pParse->zErrMsg, "no such column: ", 
         pChanges->a[i].zName, 0);
      pParse->nErr++;
      goto update_cleanup;
    }
  }

  /* Allocate memory for the array apIdx[] and fill it with pointers to every
  ** index that needs to be updated.  Indices only need updating if their
  ** key includes one of the columns named in pChanges.
  */
  for(nIdx=0, pIdx=pTab->pIndex; pIdx; pIdx=pIdx->pNext){
    for(i=0; i<pIdx->nColumn; i++){
      if( aXRef[pIdx->aiColumn[i]]>=0 ) break;
    }
    if( i<pIdx->nColumn ) nIdx++;
  }
  if( nIdx>0 ){
    apIdx = sqliteMalloc( sizeof(Index*) * nIdx );
    if( apIdx==0 ) goto update_cleanup;
  }
  for(nIdx=0, pIdx=pTab->pIndex; pIdx; pIdx=pIdx->pNext){
    for(i=0; i<pIdx->nColumn; i++){
      if( aXRef[pIdx->aiColumn[i]]>=0 ) break;
    }
    if( i<pIdx->nColumn ) apIdx[nIdx++] = pIdx;
  }

  /* Begin generating code.
  */
  v = sqliteGetVdbe(pParse);
  if( v==0 ) goto update_cleanup;
  if( (db->flags & SQLITE_InTrans)==0 ){
    sqliteVdbeAddOp(v, OP_Transaction, 0, 0);
    sqliteVdbeAddOp(v, OP_VerifyCookie, db->schema_cookie, 0);
    pParse->schemaVerified = 1;
  }

  /* Begin the database scan
  */
  pWInfo = sqliteWhereBegin(pParse, pTabList, pWhere, 1);
  if( pWInfo==0 ) goto update_cleanup;

  /* Remember the index of every item to be updated.
  */
  sqliteVdbeAddOp(v, OP_ListWrite, 0, 0);

  /* End the database scan loop.
  */
  sqliteWhereEnd(pWInfo);

  /* Initialize the count of updated rows
  */
  if( db->flags & SQLITE_CountRows ){
    sqliteVdbeAddOp(v, OP_Integer, 0, 0);
  }

  /* Rewind the list of records that need to be updated and
  ** open every index that needs updating.
  */
  sqliteVdbeAddOp(v, OP_ListRewind, 0, 0);
  base = pParse->nTab;
  openOp = pTab->isTemp ? OP_OpenWrAux : OP_OpenWrite;
  sqliteVdbeAddOp(v, openOp, base, pTab->tnum);
  for(i=0; i<nIdx; i++){
    sqliteVdbeAddOp(v, openOp, base+i+1, apIdx[i]->tnum);
  }

  /* Loop over every record that needs updating.  We have to load
  ** the old data for each record to be updated because some columns
  ** might not change and we will need to copy the old value.
  ** Also, the old data is needed to delete the old index entires.
  */
  end = sqliteVdbeMakeLabel(v);
  addr = sqliteVdbeAddOp(v, OP_ListRead, 0, end);
  sqliteVdbeAddOp(v, OP_Dup, 0, 0);
  sqliteVdbeAddOp(v, OP_MoveTo, base, 0);

  /* Delete the old indices for the current record.
  */
  for(i=0; i<nIdx; i++){
    sqliteVdbeAddOp(v, OP_Dup, 0, 0);
    pIdx = apIdx[i];
    for(j=0; j<pIdx->nColumn; j++){
      sqliteVdbeAddOp(v, OP_Column, base, pIdx->aiColumn[j]);
    }
    sqliteVdbeAddOp(v, OP_MakeIdxKey, pIdx->nColumn, 0);
    sqliteVdbeAddOp(v, OP_IdxDelete, base+i+1, 0);
  }

  /* Compute new data for this record.  
  */
  for(i=0; i<pTab->nCol; i++){
    j = aXRef[i];
    if( j<0 ){
      sqliteVdbeAddOp(v, OP_Column, base, i);
    }else{
      sqliteExprCode(pParse, pChanges->a[j].pExpr);
    }
  }

  /* Insert new index entries that correspond to the new data
  */
  for(i=0; i<nIdx; i++){
    sqliteVdbeAddOp(v, OP_Dup, pTab->nCol, 0); /* The KEY */
    pIdx = apIdx[i];
    for(j=0; j<pIdx->nColumn; j++){
      sqliteVdbeAddOp(v, OP_Dup, j+pTab->nCol-pIdx->aiColumn[j], 0);
    }
    sqliteVdbeAddOp(v, OP_MakeIdxKey, pIdx->nColumn, 0);
    sqliteVdbeAddOp(v, OP_IdxPut, base+i+1, pIdx->isUnique);
  }

  /* Write the new data back into the database.
  */
  sqliteVdbeAddOp(v, OP_MakeRecord, pTab->nCol, 0);
  sqliteVdbeAddOp(v, OP_Put, base, 0);

  /* Increment the count of rows affected by the update
  */
  if( db->flags & SQLITE_CountRows ){
    sqliteVdbeAddOp(v, OP_AddImm, 1, 0);
  }

  /* Repeat the above with the next record to be updated, until
  ** all record selected by the WHERE clause have been updated.
  */
  sqliteVdbeAddOp(v, OP_Goto, 0, addr);
  sqliteVdbeResolveLabel(v, end);
  sqliteVdbeAddOp(v, OP_ListReset, 0, 0);
  if( (db->flags & SQLITE_InTrans)==0 ){
    sqliteVdbeAddOp(v, OP_Commit, 0, 0);
  }

  /*
  ** Return the number of rows that were changed.
  */
  if( db->flags & SQLITE_CountRows ){
    sqliteVdbeAddOp(v, OP_ColumnCount, 1, 0);
    sqliteVdbeAddOp(v, OP_ColumnName, 0, 0);
    sqliteVdbeChangeP3(v, -1, "rows updated", P3_STATIC);
    sqliteVdbeAddOp(v, OP_Callback, 1, 0);
  }

update_cleanup:
  sqliteFree(apIdx);
  sqliteFree(aXRef);
  sqliteIdListDelete(pTabList);
  sqliteExprListDelete(pChanges);
  sqliteExprDelete(pWhere);
  return;
}
