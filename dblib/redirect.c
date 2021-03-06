/*
 * PANDA -- a simple transaction monitor
 * Copyright (C) 2001-2008 Ogochan & JMA (Japan Medical Association).
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
 */

/*
#define	DEBUG
#define	TRACE
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h> /*	for SOCK_STREAM	*/
#include <unistd.h>
#include <glib.h>

#include "libmondai.h"
#include "net.h"
#include "dbgroup.h"
#include "redirect.h"
#include "comm.h"
#include "debug.h"

static Bool SendCommit_Redirect(DBG_Struct *dbg) {
  Bool rc = TRUE;

  if (dbg->fpLog == NULL) {
    return rc;
  }
  SendPacketClass(dbg->fpLog, RED_COMMIT);
  ON_IO_ERROR(dbg->fpLog, badio);
  SendInt(dbg->fpLog, dbg->ticket_id);
  ON_IO_ERROR(dbg->fpLog, badio);
  if (RecvPacketClass(dbg->fpLog) != RED_OK) {
  badio:
    rc = FALSE;
    Warning("Redirect Commit error (%d)", dbg->ticket_id);
  }
  return rc;
}

static Bool SendVeryfyData_Redirect(DBG_Struct *dbg) {
  Bool rc = FALSE;
  if ((dbg->fpLog != NULL) && (dbg->redirectData != NULL) &&
      (LBS_Size(dbg->redirectData) > 0)) {
    LBS_EmitEnd(dbg->checkData);
    LBS_EmitEnd(dbg->redirectData);
    SendPacketClass(dbg->fpLog, RED_DATA);
    ON_IO_ERROR(dbg->fpLog, badio);
    SendInt(dbg->fpLog, dbg->ticket_id);
    ON_IO_ERROR(dbg->fpLog, badio);
    SendLBS(dbg->fpLog, dbg->checkData);
    ON_IO_ERROR(dbg->fpLog, badio);
    SendLBS(dbg->fpLog, dbg->redirectData);
    ON_IO_ERROR(dbg->fpLog, badio);
  }
  rc = SendCommit_Redirect(dbg);
badio:
  return rc;
}

static void ChangeDBStatus_Redirect(DBG_Struct *dbg, int dbstatus) {
  DBG_Struct *rdbg;
  if (dbg->redirect != NULL) {
    rdbg = dbg->redirect;
    rdbg->dbstatus = dbstatus;
  }
}

static Bool RecvSTATUS_Redirect(DBG_Struct *dbg) {
  int dbstatus;
  Bool rc = FALSE;
  if (dbg->fpLog != NULL) {
    SendPacketClass(dbg->fpLog, RED_STATUS);
    ON_IO_ERROR(dbg->fpLog, badio);
    dbstatus = RecvChar(dbg->fpLog);
    ON_IO_ERROR(dbg->fpLog, badio);
    ChangeDBStatus_Redirect(dbg, dbstatus);
  }
  rc = TRUE;
badio:
  return rc;
}

extern void OpenDB_RedirectPort(DBG_Struct *dbg) {
  int fh;
  DBG_Struct *rdbg;

  dbgprintf("dbg [%s]\n", dbg->name);
  if ((fNoRedirect) || (dbg->redirect == NULL)) {
    dbg->fpLog = NULL;
    dbg->redirectData = NULL;
  } else {
    rdbg = dbg->redirect;
    if ((rdbg->redirectPort == NULL) ||
        ((fh = ConnectSocket(rdbg->redirectPort, SOCK_STREAM)) < 0)) {
      dbgmsg("loging server not ready");
      dbg->fpLog = NULL;
      dbg->redirectData = NULL;
      if (!fNoCheck) {
        ChangeDBStatus_Redirect(dbg, DB_STATUS_REDFAILURE);
      }
    } else {
      dbg->fpLog = SocketToNet(fh);
      dbg->redirectData = NewLBS();
      if (!RecvSTATUS_Redirect(dbg)) {
        CloseDB_RedirectPort(dbg);
      }
    }
  }
}

extern void CloseDB_RedirectPort(DBG_Struct *dbg) {
  if (dbg->redirect == NULL)
    return;
  if (dbg->fpLog != NULL) {
    SendPacketClass(dbg->fpLog, RED_END);
    CloseNet(dbg->fpLog);
    dbg->fpLog = NULL;
  }
  if (dbg->redirectData != NULL) {
    FreeLBS(dbg->redirectData);
    dbg->redirectData = NULL;
  }
}

extern void PutDB_Redirect(DBG_Struct *dbg, char *data) {
  if (dbg->redirectData != NULL) {
    LBS_EmitString(dbg->redirectData, data);
  }
}

extern void PutCheckDataDB_Redirect(DBG_Struct *dbg, char *data) {
  LBS_EmitString(dbg->checkData, ":");
  LBS_EmitString(dbg->checkData, data);
}

extern void CopyCheckDataDB_Redirect(DBG_Struct *dbg, char *data) {
  LBS_EmitString(dbg->checkData, data);
}

extern void LockDB_Redirect(DBG_Struct *dbg) {
  if ((dbg->redirect != NULL) && (dbg->fpLog != NULL)) {
    SendPacketClass(dbg->fpLog, RED_LOCK);
  }
}

extern void UnLockDB_Redirect(DBG_Struct *dbg) {
  if ((dbg->redirect != NULL) && (dbg->fpLog != NULL)) {
    SendPacketClass(dbg->fpLog, RED_UNLOCK);
  }
}

extern void BeginDB_Redirect(DBG_Struct *dbg) {
  if (dbg->redirect == NULL)
    return;

  if (dbg->fpLog != NULL) {
    SendPacketClass(dbg->fpLog, RED_BEGIN);
    ON_IO_ERROR(dbg->fpLog, badio);
    dbg->ticket_id = RecvInt(dbg->fpLog);
    if (dbg->ticket_id == 0) {
      Warning("Illegal ticket_id.");
    }
  } else {
    dbg->ticket_id = 0;
  }
  if (dbg->redirectData != NULL) {
    LBS_EmitStart(dbg->redirectData);
    LBS_EmitStart(dbg->checkData);
  }
  return;
badio:
  Warning("dbredirector connection is lost.");
  return;
}

extern Bool CheckDB_Redirect(DBG_Struct *dbg) {
  Bool rc = TRUE;
  if (dbg->redirect == NULL)
    return rc;
  if (dbg->redirectData != NULL) {
    if (dbg->fpLog != NULL) {
      SendPacketClass(dbg->fpLog, RED_PING);
      ON_IO_ERROR(dbg->fpLog, badio);
      if (RecvPacketClass(dbg->fpLog) != RED_PONG) {
      badio:
        Warning("dbredirect server down?");
        ChangeDBStatus_Redirect(dbg, DB_STATUS_REDFAILURE);
        CloseDB_RedirectPort(dbg);
        rc = FALSE;
      }
    }
  }
  return (rc);
}

extern void AbortDB_Redirect(DBG_Struct *dbg) {
  if (dbg->redirect == NULL)
    return;

  if (dbg->fpLog != NULL) {
    SendPacketClass(dbg->fpLog, RED_ABORT);
    ON_IO_ERROR(dbg->fpLog, badio);
    SendInt(dbg->fpLog, dbg->ticket_id);
    ON_IO_ERROR(dbg->fpLog, badio);
  }
  if (dbg->redirectData != NULL) {
    LBS_EmitStart(dbg->redirectData);
    LBS_EmitStart(dbg->checkData);
  }
badio:
  return;
}

extern void CommitDB_Redirect(DBG_Struct *dbg) {
  Bool rc = TRUE;

  if (dbg->redirect == NULL)
    return;
  rc = SendVeryfyData_Redirect(dbg);
  if (rc) {
    rc = RecvSTATUS_Redirect(dbg);
  }
  if (!rc) {
    CloseDB_RedirectPort(dbg);
  }
}

extern void PutDB_AuditLog(DBG_Struct *dbg, LargeByteString *lbs) {
  if (dbg->fpLog != NULL) {
    SendPacketClass(dbg->fpLog, RED_AUDIT);
    ON_IO_ERROR(dbg->fpLog, badio);
    SendLBS(dbg->fpLog, lbs);
    ON_IO_ERROR(dbg->fpLog, badio);
  }
badio:
  return;
}
