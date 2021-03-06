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

#define MAIN

/*
#define	DEBUG
#define	TRACE
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <libgen.h>
#include <sys/socket.h>
#include <glib.h>

#include "const.h"
#include "enum.h"
#include "term.h"
#include "dirs.h"
#include "net.h"
#include "comm.h"
#include "comms.h"
#include "authstub.h"
#include "directory.h"
#include "dblib.h"
#include "dbgroup.h"
#include "dbs_main.h"
#include "RecParser.h"
#include "option.h"
#include "message.h"
#include "debug.h"

/*
#define DBS_VERSION VERSION
 */
#define DBS_VERSION "1.5.1"

static char AppName[128];
static DBD_Struct *ThisDBD;
static sigset_t hupset;
static char *PortNumber;
static int Back;
static char *Directory;
static char *AuthURL;
static URL Auth;
static char *Encoding;

static void InitSystem(char *name) {
  sigemptyset(&hupset);
  sigaddset(&hupset, SIGHUP);

  InitDirectory();
  SetUpDirectory(Directory, "", "", name, P_ALL);
  if ((ThisDBD = GetDBD(name)) == NULL) {
    fprintf(stderr, "DBD \"%s\" not found.\n", name);
    exit(1);
  }

  ThisDB = ThisDBD->db;
  DB_Table = ThisDBD->DBD_Table;

  InitDB_Process(AppName);
}

#define COMM_STRING 1
#define COMM_BINARY 2
#define COMM_STRINGE 3

typedef struct {
  char user[SIZE_USER + 1];
  char remote[SIZE_HOST + 1];
  int type;
  Bool fCount;
  Bool fLimit;
  Bool fIgnore; /*	ignore no data request	*/
  Bool fSendTermLF;
  Bool fCmdTuples;
  Bool fFixFieldName;
  Bool fLimitResult;
} SessionNode;

static void FinishSession(SessionNode *ses) {
  Message("[%s@%s] session end", ses->user, ses->remote);
  xfree(ses);
}

static int ParseVersion(char *str) {
  int major;
  int minor;
  int micro;

  major = 0;
  minor = 0;
  micro = 0;

  while ((*str != 0) && (isdigit(*str))) {
    major = major * 10 + (*str - '0');
    str++;
  }
  if (*str != 0)
    str++;
  while ((*str != 0) && (isdigit(*str))) {
    minor = minor * 10 + (*str - '0');
    str++;
  }
  if (*str != 0)
    str++;
  while ((*str != 0) && (isdigit(*str))) {
    micro = micro * 10 + (*str - '0');
    str++;
  }
  dbgprintf("version = [%d.%d.%d]", major, minor, micro);
  return (major * 10000 + minor * 100 + micro);
}

static SessionNode *NewSessionNode(void) {
  SessionNode *ses;

  ses = New(SessionNode);
  memclear(ses->user, SIZE_USER + 1);
  memclear(ses->remote, SIZE_HOST + 1);
  ses->type = COMM_STRING;
  ses->fCount = FALSE;
  ses->fLimit = FALSE;
  ses->fIgnore = FALSE;
  ses->fSendTermLF = FALSE;
  ses->fCmdTuples = FALSE;
  ses->fFixFieldName = FALSE;
  ses->fLimitResult = FALSE;

  return (ses);
}

static SessionNode *InitDBSSession(NETFILE *fpComm, char *remote) {
  SessionNode *ses;
  char buff[SIZE_BUFF + 1];
  char *pass = NULL;
  char *p, *q;
  int ver = 0;

  ses = NewSessionNode();
  /*
   version user password type\n
   */
  g_strlcpy(ses->remote, remote, SIZE_HOST);
  RecvStringDelim(fpComm, SIZE_BUFF, buff);
  p = buff;
  if ((q = strchr(p, ' ')) != NULL) {
    *q = 0;
    ver = ParseVersion(p);
    p = q + 1;
  }
  if ((q = strchr(p, ' ')) != NULL) {
    *q = 0;
    g_strlcpy(ses->user, p, SIZE_USER);
    p = q + 1;
  }
  if ((q = strchr(p, ' ')) != NULL) {
    *q = 0;
    pass = p;
  }
  if (q != NULL) {
    if (!stricmp(q + 1, "binary")) {
      ses->type = COMM_BINARY;
    } else if (!stricmp(q + 1, "string")) {
      ses->type = COMM_STRING;
    } else if (!stricmp(q + 1, "stringe")) {
      ses->type = COMM_STRINGE;
    } else {
    }
  }
  if (ver < 10200) {
    SendStringDelim(fpComm, "Error: version\n");
    Warning("[@%s] reject client(invalid version %d)", ses->remote, ver);
    xfree(ses);
    ses = NULL;
  } else if (AuthUser(&Auth, ses->user, pass, "dbs", NULL)) {
    if (ver >= 10500) {
      snprintf(buff, SIZE_BUFF, "Connect: OK;%s\n", DBS_VERSION);
      SendStringDelim(fpComm, buff);
      Message("[%s@%s] [version:%d] session start ", ses->user, ses->remote,
              ver);
    } else {
      SendStringDelim(fpComm, "Connect: OK\n");
    }
    if (ver >= 10403) {
      ses->fCount = TRUE;
      ses->fLimit = TRUE;
    }
    if (ver >= 10405) {
      Encoding = NULL;
    } else {
      Encoding = "euc-jisx0213";
    }
    dbgprintf("Encoding = [%s]\n", Encoding);
    if (ver >= 10500) {
      ses->fIgnore = TRUE;
    }
    if (ver >= 10501) {
      ses->fSendTermLF = TRUE;
      ses->fCmdTuples = TRUE;
      ses->fFixFieldName = TRUE;
      ses->fLimitResult = TRUE;
    }
  } else {
    SendStringDelim(fpComm, "Error: authentication\n");
    Warning("[%s@%s] [version %d] reject client (authentication error)",
            ses->user, ses->remote);
    xfree(ses);
    ses = NULL;
  }
  return (ses);
}

static void DecodeName(char *rname, char *vname, char *p) {
  while ((*p != 0) && (isspace(*p)))
    p++;
  while ((*p != 0) && (*p != '.') && (!isspace(*p))) {
    *rname++ = *p++;
  }
  *rname = 0;

  if (*p != 0) {
    p++;
    while ((*p != 0) && (isspace(*p)))
      p++;
    while (*p != 0) {
      if (!isspace(*p)) {
        *vname++ = *p;
      }
      p++;
    }
  }
  *vname = 0;
}

static void RecvData(NETFILE *fpComm, ValueStruct *args) {
  char buff[SIZE_BUFF + 1];
  char vname[SIZE_BUFF + 1], rname[SIZE_BUFF + 1], str[SIZE_BUFF + 1];
  char *p;
  ValueStruct *value;

  do {
    RecvStringDelim(fpComm, SIZE_BUFF, buff);
    dbgprintf("RecvData [%s]", buff);
    if ((*buff != 0) && ((p = strchr(buff, ':')) != NULL)) {
      *p = 0;
      DecodeName(rname, vname, buff);
      dbgprintf("rname[%s], vname[%s]", rname, vname);
      DecodeStringURL(str, p + 1);
      value = GetItemLongName(args, vname);
      if (value != NULL) {
        ValueIsUpdate(value);
        SetValueString(value, str, Encoding);
      } else {
        Warning("invalid record name [%s:%s]\n", rname, vname);
      }
    } else {
      break;
    }
  } while (TRUE);
}

static void WriteClientString(SessionNode *ses, NETFILE *fpComm, Bool fType,
                              DBCOMM_CTRL *ctrl, ValueStruct *args) {
  char name[SIZE_BUFF + 1], rname[SIZE_BUFF + 1], vname[SIZE_BUFF + 1];
  char buff[SIZE_BUFF + 1];
  ValueStruct *value, *rec;
  char *p, *q;
  Bool fName;
  int ix, i, limit = INT_MAX;

  SendStringDelim(fpComm, "Exec: ");
  if (ses->fCount) {
    snprintf(buff, SIZE_BUFF, "%d:%d\n", ctrl->rc, ctrl->rcount);
  } else {
    snprintf(buff, SIZE_BUFF, "%d\n", ctrl->rc);
    ctrl->rcount = 1;
  }
  SendStringDelim(fpComm, buff);
  dbgprintf("Exec: [%s]", buff);
  if ((ses->fIgnore) && ((ctrl->rcount == 0) || (args == NULL))) {
    return;
  }
  ix = 0;
  fName = FALSE;
  while (RecvStringDelim(fpComm, SIZE_BUFF, name)) {
    if (*name == 0) {
      ix++;
      if (ix >= ctrl->rcount)
        break;
      if (ses->fLimitResult && ix >= limit)
        break;
    }
    dbgprintf("name = [%s]", name);
    if ((p = strchr(name, ';')) != NULL) {
      *p = 0;
      q = p + 1;
      limit = atoi(q);
    } else {
      q = name;
      limit = 1;
    }
    if (!ses->fCount) {
      limit = 1;
    }
    if (limit < 0) {
      limit = ctrl->rcount;
    }
    if ((p = strchr(q, ':')) != NULL) {
      *p = 0;
      fName = FALSE;
    } else {
      fName = TRUE;
    }
    dbgprintf("limit = %d", limit);
    dbgprintf("name  = [%s]", name);
    DecodeName(rname, vname, name);
    if (limit > 1) {
      for (i = 0; i < limit; i++) {
        rec = ValueArrayItem(args, ix);
        if (*vname != 0) {
          value = GetItemLongName(rec, vname);
        } else {
          value = rec;
        }
        SetValueName(name);
        SendValueString(fpComm, value, NULL, fName, fType, Encoding);
        if (fName) {
          SendStringDelim(fpComm, "\n");
        }
        ix++;
        if (ix == ctrl->rcount)
          break;
      }
    } else {
      if (*vname != 0) {
        value = GetItemLongName(args, vname);
      } else {
        if (ses->fFixFieldName) {
          if (IS_VALUE_RECORD(args)) {
            value = args;
          } else {
            value = ValueArrayItem(args, 0);
          }
        } else {
          value = args;
        }
      }
#ifdef DEBUG
      DumpValueStruct(value);
#endif
      if (value != NULL) {
        SetValueName(name);
        dbgmsg("*");
        SendValueString(fpComm, value, NULL, fName, fType, Encoding);
      }
      if (ses->fSendTermLF && fName) {
        SendStringDelim(fpComm, "\n");
      }
    }
    if (fName) {
      SendStringDelim(fpComm, "\n");
    }
  }
}

static void DumpItems(NETFILE *fp, ValueStruct *value) {
  char buff[SIZE_LONGNAME];
  int i;

  if (value == NULL)
    return;
  switch (ValueType(value)) {
  case GL_TYPE_INT:
    SendStringDelim(fp, "int");
    break;
  case GL_TYPE_BOOL:
    SendStringDelim(fp, "bool");
    break;
  case GL_TYPE_BYTE:
    SendStringDelim(fp, "byte");
    break;
  case GL_TYPE_CHAR:
    snprintf(buff, SIZE_LONGNAME, "char(%d)", (int)ValueStringLength(value));
    SendStringDelim(fp, buff);
    break;
  case GL_TYPE_VARCHAR:
    snprintf(buff, SIZE_LONGNAME, "varchar(%d)", (int)ValueStringLength(value));
    SendStringDelim(fp, buff);
    break;
  case GL_TYPE_DBCODE:
    snprintf(buff, SIZE_LONGNAME, "dbcode(%d)", (int)ValueStringLength(value));
    SendStringDelim(fp, buff);
    break;
  case GL_TYPE_NUMBER:
    if (ValueFixedSlen(value) == 0) {
      snprintf(buff, SIZE_LONGNAME, "number(%d)", (int)ValueFixedLength(value));
    } else {
      snprintf(buff, SIZE_LONGNAME, "number(%d,%d)",
               (int)ValueFixedLength(value), (int)ValueFixedSlen(value));
    }
    SendStringDelim(fp, buff);
    break;
  case GL_TYPE_TEXT:
    SendStringDelim(fp, "text");
    break;
  case GL_TYPE_ARRAY:
    DumpItems(fp, ValueArrayItem(value, 0));
    snprintf(buff, SIZE_LONGNAME, "[%d]", (int)ValueArraySize(value));
    SendStringDelim(fp, buff);
    break;
  case GL_TYPE_RECORD:
    SendStringDelim(fp, "{");
    for (i = 0; i < ValueRecordSize(value); i++) {
      snprintf(buff, SIZE_LONGNAME, "%s ", ValueRecordName(value, i));
      SendStringDelim(fp, buff);
      DumpItems(fp, ValueRecordItem(value, i));
      SendStringDelim(fp, ";");
    }
    SendStringDelim(fp, "}");
    break;
  default:
    break;
  }
}

static void WriteClientStructure(SessionNode *ses, NETFILE *fpComm,
                                 RecordStruct *rec) {
  SendStringDelim(fpComm, rec->name);
  DumpItems(fpComm, rec->value);
  SendStringDelim(fpComm, ";\n");
}

static Bool ExecQuery(SessionNode *ses, NETFILE *fpComm, char *para) {
  ValueStruct *retvalue;
  char func[SIZE_BUFF + 1], rname[SIZE_BUFF + 1], pname[SIZE_BUFF + 1];
  Bool fType, ret;
  DBCOMM_CTRL ctrl;
  char *p, *q;

  InitializeCTRL(&ctrl);
  printf("para => [%s]\n", para);
  if ((q = strchr(para, ':')) != NULL) {
    *q = 0;
    DecodeStringURL(func, para);
    g_strlcpy(ctrl.func, func, SIZE_FUNC);
    p = q + 1;
    if ((q = strchr(p, ':')) != NULL) {
      *q = 0;
      DecodeStringURL(rname, p);
      SetDBCTRLRecord(&ctrl, rname);
      p = q + 1;
    } else {
      g_strlcpy(rname, "", SIZE_NAME);
    }
    if ((q = strchr(p, ':')) != NULL) {
      *q = 0;
      ctrl.limit = atoi(q + 1);
    } else {
      ctrl.limit = 1;
    }
    if (!ses->fLimit) {
      ctrl.limit = 1;
    }
    DecodeStringURL(pname, p);
    SetDBCTRLValue(&ctrl, pname);
    dbgprintf("func[%s] record[%s,%s] rc = %d", ctrl.func, ctrl.rname,
              ctrl.pname, ctrl.rc);
    if (ctrl.rec == NULL) {
      Warning("func[%s] record[%s,%s] not fund", ctrl.func, ctrl.rname,
              ctrl.pname);
      ctrl.rc = MCP_BAD_ARG;
    }
  } else {
    DecodeStringURL(func, para);
    g_strlcpy(ctrl.func, func, SIZE_FUNC);
    ctrl.fDBOperation = TRUE;
    ret = FALSE;
  }
  if (ctrl.value)
    InitializeValue(ctrl.value);
  RecvData(fpComm, ctrl.value);
  retvalue = NULL;

  if (ctrl.rc == 0) {
    dbgprintf("func[%s] record[%s,%s] rc = %d\n", ctrl.func, ctrl.rname,
              ctrl.pname, ctrl.rc);
    retvalue = ExecDB_Process(&ctrl, ctrl.rec, ctrl.value);
  } else {
    ctrl.rcount = 0;
  }
  ret = TRUE;
  fType = (ses->type == COMM_STRINGE) ? TRUE : FALSE;
  WriteClientString(ses, fpComm, fType, &ctrl, retvalue);
  if (retvalue) {
    FreeValueStruct(retvalue);
  }

  return (ret);
}

static Bool GetPathTable(SessionNode *ses, NETFILE *fpComm, char *para) {
  RecordStruct *rec;
  char rname[SIZE_BUFF + 1], buff[SIZE_BUFF + 1];
  Bool ret;
  int rno;
  char *pname, *p;

  void _SendPathTable(char *pname) {
    snprintf(buff, SIZE_BUFF, "%s$%s\n", rname, pname);
    SendStringDelim(fpComm, buff);
  }
  void _SendTable(char *name, int rno) {
    rec = ThisDB[rno - 1];
    g_strlcpy(rname, name, SIZE_NAME);
    g_hash_table_foreach(rec->opt.db->paths, (GHFunc)_SendPathTable, NULL);
  }

  if (*para != 0) {
    dbgprintf("para = [%s]", para);
    g_strlcpy(rname, para, SIZE_NAME);
    if ((p = strchr(rname, '$')) != NULL) {
      *p = 0;
      pname = p + 1;
    } else {
      pname = NULL;
    }
    if ((rno = (int)(long)g_hash_table_lookup(DB_Table, rname)) != 0) {
      rec = ThisDB[rno - 1];
      if (pname == NULL) {
        g_hash_table_foreach(rec->opt.db->paths, (GHFunc)_SendPathTable, NULL);
      } else {
        if (g_hash_table_lookup(rec->opt.db->paths, pname) != NULL) {
          snprintf(buff, SIZE_BUFF, "%s$%s\n", rname, pname);
          SendStringDelim(fpComm, buff);
        }
      }
    }
  } else {
    g_hash_table_foreach(DB_Table, (GHFunc)_SendTable, NULL);
  }
  SendStringDelim(fpComm, "\n");

  ret = TRUE;
  return (ret);
}

static Bool GetSchema(SessionNode *ses, NETFILE *fpComm, char *para) {
  RecordStruct *rec;
  char rname[SIZE_BUFF + 1], pname[SIZE_BUFF + 1];
  int rno, pno;
  char *p, *q;
  DBCOMM_CTRL ctrl;
  Bool ret = FALSE;

  if ((q = strchr(para, ':')) != NULL) {
    *q = 0;
    DecodeStringURL(rname, para);
    p = q + 1;
    DecodeStringURL(pname, p);
    dbgprintf("rname => [%s], pname => [%s]\n", rname, pname);
    if ((rno = (int)(long)g_hash_table_lookup(DB_Table, rname)) != 0) {
      ctrl.rno = rno - 1;
      rec = ThisDB[ctrl.rno];
      if ((pno = (int)(long)g_hash_table_lookup(rec->opt.db->paths, pname)) ==
          0) {
        rec = NULL;
        Warning("invalid path name [%s]\n", pname);
      }
    } else {
      rec = NULL;
      Warning("invalid record name [%s]\n", rname);
    }
  } else {
    rec = NULL;
    Warning("invalid message [%s]\n", para);
  }
  if (rec != NULL) {
    WriteClientStructure(ses, fpComm, rec);
  } else {
    SendStringDelim(fpComm, "\n");
  }
  ret = TRUE;
  return (ret);
}

static Bool SetFeature(SessionNode *ses, NETFILE *fpComm, char *para) {
  char *p = para, *q;
  Bool fOn, ret;

  while (*p != 0) {
    while ((*p != 0) && (isspace(*p)))
      p++;
    if (*p == 0)
      break;
    if ((q = strchr(p, ',')) != NULL) {
      *q = 0;
      q++;
    } else {
      q = p + strlen(p);
    }
    if (*p == '!') {
      fOn = FALSE;
      p++;
    } else {
      fOn = TRUE;
    }
    if (strcmp(p, "count") == 0) {
      ses->fCount = fOn;
    } else if (strcmp(p, "limit") == 0) {
      ses->fLimit = fOn;
    } else if (strcmp(p, "ignore") == 0) {
      ses->fIgnore = fOn;
    }
    p = q;
  }
  ret = TRUE;
  return (ret);
}

static Bool do_String(NETFILE *fpComm, char *input, SessionNode *ses) {
  Bool ret;
  if (strlcmp(input, "Exec: ") == 0) {
    dbgmsg("exec");
    ret = ExecQuery(ses, fpComm, input + 6);
  } else if (strlcmp(input, "PathTables: ") == 0) {
    dbgmsg("pathtables");
    ret = GetPathTable(ses, fpComm, input + 12);
  } else if (strlcmp(input, "Schema: ") == 0) {
    dbgmsg("schema");
    ret = GetSchema(ses, fpComm, input + 8);
  } else if (strlcmp(input, "Feature: ") == 0) {
    dbgmsg("feature");
    ret = SetFeature(ses, fpComm, input + 9);
  } else if (strlcmp(input, "End") == 0) {
    dbgmsg("end");
    ret = FALSE;
  } else {
    Warning("invalid message [%s]\n", input);
    ret = FALSE;
  }
  return (ret);
}

static Bool MainLoop(NETFILE *fpComm, SessionNode *ses) {
  char buff[SIZE_BUFF + 1];
  Bool ret = FALSE;

  if (RecvStringDelim(fpComm, SIZE_BUFF, buff)) {
    switch (ses->type) {
    case COMM_STRING:
    case COMM_STRINGE:
      ret = do_String(fpComm, buff, ses);
      break;
    default:
      ret = FALSE;
      break;
    }
  }
  return (ret);
}

static void ExecuteServer(void) {
  int fh;
  int soc_len;
  int soc[MAX_SOCKET];
  NETFILE *fp;
  int pid;
  SessionNode *ses;
  Port *port;
  char remote[SIZE_HOST + 1];

  port = ParPortName(PortNumber);
  soc_len = InitServerMultiPort(port, Back, soc);
  while (TRUE) {
    if ((fh = AcceptLoop(soc, soc_len)) < 0) {
      continue;
    }
    if ((pid = fork()) > 0) { /*	parent	*/
      close(fh);
    } else if (pid == 0) { /*	child	*/
      fp = SocketToNet(fh);
      RemoteIP(fh, remote, SIZE_HOST);
      if ((ses = InitDBSSession(fp, remote)) != NULL) {
        while (MainLoop(fp, ses))
          ;
        FinishSession(ses);
      }
      CloseNet(fp);
      exit(0);
    }
  }
}

static void StopProcess(int ec) {
  exit(ec);
}

static ARG_TABLE option[] = {
    {"port", STRING, TRUE, (void *)&PortNumber, "port number"},
    {"back", INTEGER, TRUE, (void *)&Back, "connection waiting queue length"},

    {"base", STRING, TRUE, (void *)&BaseDir, "base directory"},
    {"record", STRING, TRUE, (void *)&RecordDir, "record directory"},
    {"ddir", STRING, TRUE, (void *)&D_Dir, "defines directory"},
    {"dir", STRING, TRUE, (void *)&Directory, "config file name"},

    {"dbhost", STRING, TRUE, (void *)&DB_Host, "DB host name(for override)"},
    {"dbport", STRING, TRUE, (void *)&DB_Port, "DB port numver(for override)"},
    {"db", STRING, TRUE, (void *)&DB_Name, "DB name(for override)"},
    {"dbuser", STRING, TRUE, (void *)&DB_User, "DB user name(for override)"},
    {"dbpass", STRING, TRUE, (void *)&DB_Pass, "DB password(for override)"},

    {"auth", STRING, TRUE, (void *)&AuthURL, "authentication server"},

    {"nocheck", BOOLEAN, TRUE, (void *)&fNoCheck, "no check dbredirector"},
    {"noredirect", BOOLEAN, TRUE, (void *)&fNoRedirect, "no DB redirection"},
    {"numeric", BOOLEAN, TRUE, (void *)&fNumericHOST,
     "Numeric form of hostname"},

    {NULL, 0, FALSE, NULL, NULL}};

static void SetDefault(void) {
  PortNumber = PORT_DBSERV;
  Back = 5;

  BaseDir = NULL;
  RecordDir = NULL;
  D_Dir = NULL;
  Directory = "./directory";
  AuthURL = "glauth://localhost:8001"; /*	PORT_GLAUTH	*/

  DB_User = NULL;
  DB_Pass = NULL;
  DB_Host = NULL;
  DB_Port = NULL;
  DB_Name = DB_User;

  fNoCheck = FALSE;
  fNoRedirect = FALSE;
  fNumericHOST = FALSE;
}

extern int main(int argc, char **argv) {
  FILE_LIST *fl;
  int rc;

  (void)signal(SIGCHLD, SIG_IGN);
  (void)signal(SIGHUP, (void *)StopProcess);
  SetDefault();
  fl = GetOption(option, argc, argv, NULL);
  InitMessage("dbs", NULL);

  ParseURL(&Auth, AuthURL, "file");

  if ((fl != NULL) && (fl->name != NULL)) {
    InitNET();
    snprintf(AppName, sizeof(AppName), "dbs-%s", fl->name);
    InitSystem(fl->name);
    ExecuteServer();
    StopProcess(0);
    rc = 0;
  } else {
    rc = -1;
    fprintf(stderr, "DBD is not specified\n");
  }
  exit(rc);
}
