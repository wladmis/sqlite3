/*
** Copyright (c) 1999, 2000 D. Richard Hipp
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
** Header file for the Virtual DataBase Engine (VDBE)
**
** This header defines the interface to the virtual database engine
** or VDBE.  The VDBE implements an abstract machine that runs a
** simple program to access and modify the underlying database.
**
** $Id$
*/
#ifndef _SQLITE_VDBE_H_
#define _SQLITE_VDBE_H_
#include <stdio.h>

/*
** A single VDBE is an opaque structure named "Vdbe".  Only routines
** in the source file sqliteVdbe.c are allowed to see the insides
** of this structure.
*/
typedef struct Vdbe Vdbe;

/*
** A single instruction of the virtual machine has an opcode
** and as many as three operands.  The instruction is recorded
** as an instance of the following structure:
*/
struct VdbeOp {
  int opcode;         /* What operation to perform */
  int p1;             /* First operand */
  int p2;             /* Second parameter (often the jump destination) */
  char *p3;           /* Third parameter */
};
typedef struct VdbeOp VdbeOp;

/*
** The following macro converts a relative address in the p2 field
** of a VdbeOp structure into a negative number so that 
** sqliteVdbeAddOpList() knows that the address is relative.  Calling
** the macro again restores the address.
*/
#define ADDR(X)  (-1-(X))

/*
** These are the available opcodes.
**
** If any of the values changes or if opcodes are added or removed,
** be sure to also update the zOpName[] array in sqliteVdbe.c to
** mirror the change.
**
** The source tree contains an AWK script named renumberOps.awk that
** can be used to renumber these opcodes when new opcodes are inserted.
*/
#define OP_Open                1
#define OP_Close               2
#define OP_Fetch               3
#define OP_New                 4
#define OP_Put                 5
#define OP_Distinct            6
#define OP_Delete              7
#define OP_Field               8
#define OP_Key                 9
#define OP_Rewind             10
#define OP_Next               11

#define OP_Destroy            12
#define OP_Reorganize         13

#define OP_ResetIdx           14
#define OP_NextIdx            15
#define OP_PutIdx             16
#define OP_DeleteIdx          17

#define OP_MemLoad            18
#define OP_MemStore           19

#define OP_ListOpen           20
#define OP_ListWrite          21
#define OP_ListRewind         22
#define OP_ListRead           23
#define OP_ListClose          24

#define OP_SortOpen           25
#define OP_SortPut            26
#define OP_SortMakeRec        27
#define OP_SortMakeKey        28
#define OP_Sort               29
#define OP_SortNext           30
#define OP_SortKey            31
#define OP_SortCallback       32
#define OP_SortClose          33

#define OP_FileOpen           34
#define OP_FileRead           35
#define OP_FileField          36
#define OP_FileClose          37

#define OP_MakeRecord         38
#define OP_MakeKey            39

#define OP_Goto               40
#define OP_If                 41
#define OP_Halt               42

#define OP_ColumnCount        43
#define OP_ColumnName         44
#define OP_Callback           45

#define OP_Integer            46
#define OP_String             47
#define OP_Null               48
#define OP_Pop                49
#define OP_Dup                50
#define OP_Pull               51

#define OP_Add                52
#define OP_AddImm             53
#define OP_Subtract           54
#define OP_Multiply           55
#define OP_Divide             56
#define OP_Min                57
#define OP_Max                58
#define OP_Like               59
#define OP_Glob               60
#define OP_Eq                 61
#define OP_Ne                 62
#define OP_Lt                 63
#define OP_Le                 64
#define OP_Gt                 65
#define OP_Ge                 66
#define OP_IsNull             67
#define OP_NotNull            68
#define OP_Negative           69
#define OP_And                70
#define OP_Or                 71
#define OP_Not                72
#define OP_Concat             73
#define OP_Noop               74

#define OP_MAX                74

/*
** Prototypes for the VDBE interface.  See comments on the implementation
** for a description of what each of these routines does.
*/
Vdbe *sqliteVdbeCreate(Dbbe*);
int sqliteVdbeAddOp(Vdbe*,int,int,int,const char*,int);
int sqliteVdbeAddOpList(Vdbe*, int nOp, VdbeOp const *aOp);
void sqliteVdbeChangeP3(Vdbe*, int addr, const char *zP1, int N);
void sqliteVdbeDequoteP3(Vdbe*, int addr);
int sqliteVdbeMakeLabel(Vdbe*);
void sqliteVdbeDelete(Vdbe*);
int sqliteVdbeOpcode(const char *zName);
int sqliteVdbeExec(Vdbe*,sqlite_callback,void*,char**);
int sqliteVdbeList(Vdbe*,sqlite_callback,void*,char**);
void sqliteVdbeResolveLabel(Vdbe*, int);
int sqliteVdbeCurrentAddr(Vdbe*);
void sqliteVdbeTrace(Vdbe*,FILE*);


#endif
