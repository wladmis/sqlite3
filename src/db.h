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

typedef struct Db Db;
typedef struct DbCursor DbCursor;

int sqliteDbOpen(const char *filename, Db**);
int sqliteDbClose(Db*);
int sqliteDbBeginTransaction(Db*);
int sqliteDbCommit(Db*);
int sqliteDbRollback(Db*);

int sqliteDbCreateTable(Db*, int *pTblno);
int sqliteDbDropTable(Db*, int tblno);

int sqliteDbCursorOpen(Db*, int tblno, DbCursor**);
int sqliteDbCursorClose(DbCursor*);

int sqliteDbCursorFirst(DbCursor*);
int sqliteDbCursorNext(DbCursor*);
int sqliteDbCursorDatasize(DbCursor*);
int sqliteDbCursorKeysize(DbCursor*);
int sqliteDbCursorRead(DbCursor*, int amt, int offset, char *buf);
int sqliteDbCursorReadKey(DbCursor*, int amt, int offset, char *buf);
int sqliteDbCursorMoveTo(DbCursor*, int nKey, void *pKey);
int sqliteDbCursorDelete(DbCursor*);
int sqliteDbCursorInsert(DbCursor*, int nKey, void *pKey, int nData, void *pD);

int sqliteDbReorganize(Db*);
