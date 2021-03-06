/*
 * PANDA -- a simple transaction monitor
 * Copyright (C) 2004-2008 Ogochan.
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
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <signal.h>

#include "const.h"
#include "enum.h"
#include "libmondai.h"
#include "directory.h"
#include "wfcdata.h"
#include "dbgroup.h"
#include "monsys.h"
#include "bytea.h"
#include "dbops.h"
#include "debug.h"

static ValueStruct *_ExportBLOB(DBG_Struct *dbg, DBCOMM_CTRL *ctrl,
                                RecordStruct *rec, ValueStruct *args) {
  ValueStruct *ret, *file, *obj;
  DBG_Struct *mondbg;
  int rc;
  char *filename;

  ret = NULL;
  file = GetItemLongName(args, "file");
  obj = GetItemLongName(args, "object");
  if (file != NULL && obj != NULL) {
    filename = ValueToString(file, dbg->coding);
    mondbg = GetDBG_monsys();
    if (monblob_export_file(mondbg, ValueObjectId(obj), filename)) {
      monblob_delete(mondbg,ValueObjectId(obj));
      rc = MCP_OK;
    } else {
      rc = MCP_BAD_OTHER;
    }
    ret = DuplicateValue(args, TRUE);
  } else {
    rc = MCP_BAD_ARG;
  }
  if (ctrl != NULL) {
    ctrl->rc = rc;
  }
  return ret;
}

static ValueStruct *_ImportBLOB(DBG_Struct *dbg, DBCOMM_CTRL *ctrl,
                                RecordStruct *rec, ValueStruct *args) {
  ValueStruct *file, *obj;
  DBG_Struct *mondbg;
  int rc;
  char *filename,*id;

  mondbg = GetDBG_monsys();
  file = GetItemLongName(args, "file");
  obj = GetItemLongName(args, "object");
  if (file != NULL && obj != NULL) {
    filename = ValueToString(file, dbg->coding);
    id = monblob_import(mondbg, NULL, 0, filename, NULL, MON_LIFE_SHORT);
    if (id != NULL) {
      SetValueString(obj,id,NULL);
      xfree(id);
      rc = MCP_OK;
    } else {
      rc = MCP_BAD_ARG;
    }
  } else {
    rc = MCP_BAD_ARG;
  }
  if (ctrl != NULL) {
    ctrl->rc = rc;
  }

  return DuplicateValue(args, TRUE);
}

static ValueStruct *_CheckBLOB(DBG_Struct *dbg, DBCOMM_CTRL *ctrl,
                               RecordStruct *rec, ValueStruct *args) {
  int rc;
  ValueStruct *obj;
  DBG_Struct *mondbg;

  if (rec->type != RECORD_DB) {
    rc = MCP_BAD_ARG;
  } else {
    if ((obj = GetItemLongName(args, "object")) != NULL) {
      mondbg = GetDBG_monsys();
      if (monblob_check_id(mondbg, ValueObjectId(obj))) {
        rc = MCP_OK;
      } else {
        rc = MCP_EOF;
      }
    } else {
      rc = MCP_BAD_ARG;
    }
  }
  if (ctrl != NULL) {
    ctrl->rc = rc;
  }

  return NULL;
}

static ValueStruct *_PersistBLOB(DBG_Struct *dbg, DBCOMM_CTRL *ctrl,
                                 RecordStruct *rec, ValueStruct *args) {
  DBG_Struct *mondbg;
  ValueStruct *obj;
  int rc;
  if (rec->type != RECORD_DB) {
    rc = MCP_BAD_ARG;
  } else {
    obj = GetItemLongName(args, "object");
    if (obj != NULL) {
      mondbg = GetDBG_monsys();
      monblob_persist(mondbg, ValueObjectId(obj), NULL, NULL, MON_LIFE_LONG);
      rc = MCP_OK;
    } else {
      rc = MCP_BAD_OTHER;
    }
  }
  if (ctrl != NULL) {
    ctrl->rc = rc;
  }
  return NULL;
}

static ValueStruct *_DestroyBLOB(DBG_Struct *dbg, DBCOMM_CTRL *ctrl,
                                 RecordStruct *rec, ValueStruct *args) {
  DBG_Struct *mondbg;
  ValueStruct *obj;
  int rc;
  if (rec->type != RECORD_DB) {
    rc = MCP_BAD_ARG;
  } else {
    obj = GetItemLongName(args, "object");
    if (obj != NULL) {
      mondbg = GetDBG_monsys();
      if (monblob_delete(mondbg, ValueObjectId(obj))) {
        rc = MCP_OK;
      } else {
        rc = MCP_BAD_OTHER;
      }
    } else {
      rc = MCP_BAD_ARG;
    }
  }
  if (ctrl != NULL) {
    ctrl->rc = rc;
  }
  return NULL;
}

static DB_OPS Operations[] = {
    /*	DB operations		*/
    {"DBOPEN", (DB_FUNC)_DBOPEN},
    {"DBDISCONNECT", (DB_FUNC)_DBDISCONNECT},
    {"DBSTART", (DB_FUNC)_DBSTART},
    {"DBCOMMIT", (DB_FUNC)_DBCOMMIT},
    /*	table operations	*/
    {"BLOBEXPORT", _ExportBLOB},
    {"BLOBIMPORT", _ImportBLOB},
    {"BLOBCHECK", _CheckBLOB},
    {"BLOBPERSIST", _PersistBLOB},
    {"BLOBDESTROY", _DestroyBLOB},

    {NULL, NULL}};

static DB_Primitives Core = {
    _EXEC,
    _DBACCESS,
    _QUERY,
    NULL,
};

extern DB_Func *InitNativeBLOB(void) {
  return (EnterDB_Function("NativeBLOB", Operations, DB_PARSER_NULL, &Core, "",
                           ""));
}
