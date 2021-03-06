       IDENTIFICATION              DIVISION.
       PROGRAM-ID.                 MCPSUB.
      ******************************************************************
      * PANDA -- a simple transaction monitor
      *
      * Copyright (C) 1993-1999 Ogochan.
      *               2000-2003 Ogochan & JMARI.
      *               2004-2005 Ogochan.
      *
      * This program is free software; you can redistribute it and/or modify
      * it under the terms of the GNU General Public License as published by
      * the Free Software Foundation; either version 2 of the License, or
      * (at your option) any later version.
      *
      * This program is distributed in the hope that it will be useful, but
      * WITHOUT ANY WARRANTY; without even the implied warranty of
      * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
      * General Public License for more details.
      *
      * You should have received a copy of the GNU General Public License
      * along with this program; if not, write to the Free Software
      * Foundation, 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
      ******************************************************************
      *  $B%7%9%F%`L>(B        : $B#O#R#C#A(B
      *  $B%5%V%7%9%F%`L>(B    : $B6&DL(B
      *  $B%3%s%]!<%M%s%HL>(B  : $B%5%V(B
      *  $B4IM}<T(B            : 
      *  $B:n@.F|IU(B    $B:n6H<T(B        $B5-=R(B
      *  XX.XX.XX    $B#N#N#N(B        $B?75,:n@.(B
      ******************************************************************
       ENVIRONMENT                 DIVISION.
       CONFIGURATION               SECTION.
       INPUT-OUTPUT            SECTION.
       FILE-CONTROL.
           SELECT  DBR-FILE
               ASSIGN  TO  "db.input"
               ORGANIZATION    SEQUENTIAL
               ACCESS  MODE    SEQUENTIAL.
           SELECT  DBW-FILE
               ASSIGN  TO  "db.output"
               ORGANIZATION    SEQUENTIAL
               ACCESS  MODE    SEQUENTIAL.
           SELECT  LOG-FILE
               ASSIGN  TO  "log.output"
               ORGANIZATION    SEQUENTIAL
               ACCESS  MODE    SEQUENTIAL.
       DATA                        DIVISION.
       FILE                    SECTION.
       FD  DBR-FILE.
       01  DBR-REC.
           02  FILLER      PIC X(1024).
       FD  DBW-FILE.
       01  DBW-REC.
           02  FILLER      PIC X(1024).
       FD  LOG-FILE.
       01  LOG-REC.
           02  FILLER      PIC X(1024).
       WORKING-STORAGE         SECTION.
       01  WORK.
           02  I           PIC S9(9)   BINARY.
           02  J           PIC S9(9)   BINARY.
       01  FLG.
           02  FLG-DBR-EOF PIC 9.
       01  PATH-NULL.
           02  FILLER      PIC S9(9)   BINARY  VALUE 1.
           02  FILLER      PIC S9(9)   BINARY  VALUE -1.
           02  FILLER      PIC S9(9)   BINARY  VALUE -1.
      *
           COPY    DBCOMM.
       LINKAGE                     SECTION.
           COPY    MCPAREA.
           COPY    DBAREA.
       PROCEDURE                   DIVISION
           USING   MCPAREA
                   DBAREA.
      *****************************************************************
       000-ENTRY               SECTION.
           PERFORM 010-DUMP-PARA.
      *
           EVALUATE    MCP-FUNC
             WHEN     'DBOPEN'
               PERFORM 100-DBOPEN
             WHEN     'PUTWINDOW'
               PERFORM 300-PUTWINDOW
             WHEN     'DBDISCONNECT'
               PERFORM 900-DBCLOSE
             WHEN     'DBCLOSE'
               PERFORM 900-DBCLOSE
             WHEN     'DBSTART'
               PERFORM 110-DB-OPERATION
             WHEN     'DBCOMMIT'
               PERFORM 110-DB-OPERATION
               CONTINUE
             WHEN      OTHER
               PERFORM 200-TABLE-OPERATION
           END-EVALUATE.
      *
           EXIT    PROGRAM.
      *****************************************************************
       010-DUMP-PARA           SECTION.
           DISPLAY '** MCP-FUNC     [' MCP-FUNC ']'
               UPON    SYSOUT.
           DISPLAY '** MCP-RC       [' MCP-RC ']'
               UPON    SYSOUT.
           DISPLAY '** MCP-BLOCKS   [' MCP-BLOCKS ']'
               UPON    SYSOUT.
           DISPLAY '** MCP-RNAME    [' MCP-RNAME ']'
               UPON    SYSOUT.
           DISPLAY '** MCP-PNAME    [' MCP-PNAME ']'
               UPON    SYSOUT.
      *****************************************************************
       100-DBOPEN              SECTION.
           OPEN    INPUT
               DBR-FILE.
      *
           PERFORM 110-DB-OPERATION.
      *****************************************************************
       110-DB-OPERATION        SECTION.
           MOVE    MCP-FUNC    TO  DBCOMM-FUNC.
           MOVE    PATH-NULL   TO  DBCOMM-PATH.
      *
           PERFORM 210-SEND-SQL.
           PERFORM 220-RECV-ANS.
      *****************************************************************
       200-TABLE-OPERATION     SECTION.
           MOVE    DBDATA      TO  DBCOMM-RECORD.
           MOVE    MCP-FUNC    TO  DBCOMM-FUNC.
           MOVE    MCP-PATH    TO  DBCOMM-PATH.
      *
           PERFORM 210-SEND-SQL.
           PERFORM 220-RECV-ANS.
      *****************************************************************
       210-SEND-SQL            SECTION.
           OPEN    OUTPUT
      *        LOG-FILE
               DBW-FILE.
      *
           IF      (  DBCOMM-PATH-BLOCKS  =  ZERO  )
               MOVE    1           TO  DBCOMM-PATH-BLOCKS
           END-IF.
      *
           PERFORM VARYING J   FROM    1   BY  1
                   UNTIL   (  J    >  DBCOMM-PATH-BLOCKS  )
               WRITE   DBW-REC     FROM    DBCOMM-BLOCK(J)
      *        WRITE   LOG-REC     FROM    DBCOMM-BLOCK(J)
           END-PERFORM.
      *
           CLOSE
      *        LOG-FILE
               DBW-FILE.
      *****************************************************************
       220-RECV-ANS            SECTION.
           MOVE    ZERO        TO  FLG-DBR-EOF.
           PERFORM VARYING J   FROM    1   BY  1
                   UNTIL   (  J    >  DBCOMM-PATH-BLOCKS  )
                       OR  (  FLG-DBR-EOF  >  ZERO        )
               READ    DBR-FILE    INTO    DBCOMM-BLOCK(J)
                 AT  END
                   MOVE    1           TO  FLG-DBR-EOF
               END-READ
           END-PERFORM.
      *
           MOVE    DBCOMM-RECORD   TO  DBDATA.
           MOVE    DBCOMM-RC       TO  MCP-RC.
      *****************************************************************
       300-PUTWINDOW           SECTION.
           MOVE   'PUTG'   TO  MCP-STATUS.
           MOVE    ZERO    TO  MCP-RC.
      *****************************************************************
       900-DBCLOSE             SECTION.
           PERFORM 110-DB-OPERATION.
      *
           CLOSE
               DBR-FILE.
