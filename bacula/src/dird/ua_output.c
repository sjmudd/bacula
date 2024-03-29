/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2020 Kern Sibbald

   The original author of Bacula is Kern Sibbald, with contributions
   from many others, a complete list can be found in the file AUTHORS.

   You may use this file and others of this release according to the
   license defined in the LICENSE file, which includes the Affero General
   Public License, v3.0 ("AGPLv3") and some additional permissions and
   terms pursuant to its AGPLv3 Section 7.

   This notice must be preserved when any source code is
   conveyed and/or propagated.

   Bacula(R) is a registered trademark of Kern Sibbald.
*/
/*
 *   Bacula Director -- User Agent Output Commands
 *     I.e. messages, listing database, showing resources, ...
 *
 *     Kern Sibbald, September MM
 */

#include "bacula.h"
#include "dird.h"

/* Imported subroutines */

/* Imported variables */

/* Imported functions */

/* Forward referenced functions */
static int do_list_cmd(UAContext *ua, const char *cmd, e_list_type llist);
static bool list_nextvol(UAContext *ua, int ndays);

/*
 * Turn auto display of console messages on/off
 */
int autodisplay_cmd(UAContext *ua, const char *cmd)
{
   static const char *kw[] = {
      NT_("on"),
      NT_("off"),
      NULL};

   switch (find_arg_keyword(ua, kw)) {
   case 0:
      ua->auto_display_messages = true;
      break;
   case 1:
      ua->auto_display_messages = false;
      break;
   default:
      ua->error_msg(_("ON or OFF keyword missing.\n"));
      break;
   }
   return 1;
}

/*
 * Turn GUI mode on/off
 */
int gui_cmd(UAContext *ua, const char *cmd)
{
   static const char *kw[] = {
      NT_("on"),
      NT_("off"),
      NULL};

   switch (find_arg_keyword(ua, kw)) {
   case 0:
      ua->jcr->gui = ua->gui = true;
      break;
   case 1:
      ua->jcr->gui = ua->gui = false;
      break;
   default:
      ua->error_msg(_("ON or OFF keyword missing.\n"));
      break;
   }
   return 1;
}

/*
 * Enter with Resources locked
 */
static void show_disabled_jobs(UAContext *ua)
{
   JOB *job;
   bool first = true;
   foreach_res(job, R_JOB) {
      if (!acl_access_ok(ua, Job_ACL, job->name())) {
         continue;
      }
      if (!job->is_enabled()) {
         if (first) {
            first = false;
            ua->send_msg(_("Disabled Jobs:\n"));
         }
         ua->send_msg("   %s\n", job->name());
     }
  }
  if (first) {
     ua->send_msg(_("No disabled Jobs.\n"));
  }
}

struct showstruct {const char *res_name; int type;};
static struct showstruct reses[] = {
   {NT_("directors"),  R_DIRECTOR},
   {NT_("clients"),    R_CLIENT},
   {NT_("counters"),   R_COUNTER},
   {NT_("devices"),    R_DEVICE},
   {NT_("jobs"),       R_JOB},
   {NT_("storages"),   R_STORAGE},
   {NT_("catalogs"),   R_CATALOG},
   {NT_("schedules"),  R_SCHEDULE},
   {NT_("filesets"),   R_FILESET},
   {NT_("pools"),      R_POOL},
   {NT_("messages"),   R_MSGS},
   {NT_("statistics"), R_COLLECTOR},
// {NT_("consoles"),   R_CONSOLE},
// {NT_("jobdefs"),    R_JOBDEFS},
// {NT_{"autochangers"), R_AUTOCHANGER},
   {NT_("all"),        -1},
   {NT_("help"),       -2},
   {NULL,           0}
};


/*
 *  Displays Resources
 *
 *  show all
 *  show <resource-keyword-name>  e.g. show directors
 *  show <resource-keyword-name>=<name> e.g. show director=HeadMan
 *  show disabled    shows disabled jobs
 *
 */
int show_cmd(UAContext *ua, const char *cmd)
{
   int i, j, type, len;
   int recurse;
   char *res_name;
   RES_HEAD *reshead = NULL;
   RES *res = NULL;

   Dmsg1(20, "show: %s\n", ua->UA_sock->msg);


   LockRes();
   for (i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("disabled")) == 0) {
         show_disabled_jobs(ua);
         goto bail_out;
      }

      res = NULL;
      reshead = NULL;
      type = 0;

      res_name = ua->argk[i];
      if (!ua->argv[i]) {             /* was a name given? */
         /* No name, dump all resources of specified type */
         recurse = 1;
         len = strlen(res_name);
         for (j=0; reses[j].res_name; j++) {
            if (strncasecmp(res_name, reses[j].res_name, len) == 0) {
               type = reses[j].type;
               if (type > 0) {
                  reshead = res_head[type-r_first];
               } else {
                  reshead = NULL;
               }
               break;
            }
         }

      } else {
         /* Dump a single resource with specified name */
         recurse = 0;
         len = strlen(res_name);
         for (j=0; reses[j].res_name; j++) {
            if (strncasecmp(res_name, reses[j].res_name, len) == 0) {
               type = reses[j].type;
               res = (RES *)GetResWithName(type, ua->argv[i]);
               if (!res) {
                  type = -3;
               }
               break;
            }
         }
      }

      switch (type) {
      /* All resources */
      case -1:
         for (j=r_first; j<=r_last; j++) {
            /* Skip R_DEVICE since it is really not used or updated */
            if (j != R_DEVICE) {
               dump_each_resource(j, bsendmsg, ua);
            }
         }
         break;
      /* Help */
      case -2:
         ua->send_msg(_("Keywords for the show command are:\n"));
         for (j=0; reses[j].res_name; j++) {
            ua->error_msg("%s\n", reses[j].res_name);
         }
         goto bail_out;
      /* Resource not found */
      case -3:
         ua->error_msg(_("%s resource %s not found.\n"), res_name, ua->argv[i]);
         goto bail_out;
      /* Resource not found */
      case 0:
         ua->error_msg(_("Resource %s not found\n"), res_name);
         goto bail_out;
      /* Dump a specific type */
      default:
         if (res) {             /* keyword and argument, ie: show job=name */
            dump_resource(recurse?type:-type, res, bsendmsg, ua);

         } else if (reshead) {  /* keyword only, ie: show job */
            dump_each_resource(-type, bsendmsg, ua);
         }
         break;
      }
   }
bail_out:
   UnlockRes();
   return 1;
}

/*
 * Check if the access is permitted for a list of jobids
 *
 * Not in ua_acl.c because it's using db access, and tools such
 * as bdirjson are not linked with cats.
 */
bool acl_access_jobid_ok(UAContext *ua, const char *jobids)
{
   char     *tmp=NULL, *p;
   bool      ret=false;
   JOB_DBR   jr;
   uint32_t  jid;

   if (!jobids) {
      return false;
   }

   if (!is_a_number_list(jobids)) {
      return false;
   }

   /* If no console resource => default console and all is permitted */
   if (!ua || !ua->cons) {
      Dmsg0(1400, "Root cons access OK.\n");
      return true;     /* No cons resource -> root console OK for everything */
   }

   alist *list = ua->cons->ACL_lists[Job_ACL];
   if (!list) {                       /* empty list */
      return false;                   /* List empty, reject everything */
   }

   /* Special case *all* gives full access */
   if (list->size() == 1 && strcasecmp("*all*", (char *)list->get(0)) == 0) {
      return true;
   }

   /* If we can't open the database, just say no */
   if (!open_new_client_db(ua)) {
      return false;
   }

   p = tmp = bstrdup(jobids);

   while (get_next_jobid_from_list(&p, &jid) > 0) {
      memset(&jr, 0, sizeof(jr));
      jr.JobId = jid;

      if (db_get_job_record(ua->jcr, ua->db, &jr)) {
         for (int i=0; i<list->size(); i++) {
            if (strcasecmp(jr.Name, (char *)list->get(i)) == 0) {
               Dmsg3(1400, "ACL found %s in %d %s\n", jr.Name,
                     Job_ACL, (char *)list->get(i));
               ret = true;
               goto bail_out;
            }
         }
      }
   }

bail_out:
   if (tmp) {
      free(tmp);
   }
   return ret;
}

/*
 *  List contents of database
 *
 *  list jobs           - lists all jobs run
 *  list jobid=nnn      - list job data for jobid
 *  list ujobid=uname   - list job data for unique jobid
 *  list job=name       - list all jobs with "name"
 *  list jobname=name   - same as above
 *  list jobmedia jobid=<nn>
 *  list jobmedia job=name
 *  list joblog jobid=<nn>
 *  list joblog job=name
 *  list files [type=<deleted|all>] jobid=<nn> - list files saved for job nn
 *  list files [type=<deleted|all>] job=name
 *  list pools          - list pool records
 *  list jobtotals      - list totals for all jobs
 *  list media          - list media for given pool (deprecated)
 *  list volumes        - list Volumes
 *  list clients        - list clients
 *  list nextvol job=xx  - list the next vol to be used by job
 *  list nextvolume job=xx - same as above.
 *  list copies jobid=x,y,z
 *  list pluginrestoreconf jobid=x,y,z [id=k]
 *
 *  Note: keyword "long" is before the first command on the command 
 *    line results in doing a llist (long listing).
 */

/* Do long or full listing */
int llist_cmd(UAContext *ua, const char *cmd)
{
   return do_list_cmd(ua, cmd, VERT_LIST);
}

/* Do short or summary listing */
int list_cmd(UAContext *ua, const char *cmd)
{
   if (find_arg(ua, "long") > 0) {
      return do_list_cmd(ua, cmd, VERT_LIST);  /* do a long list */
   } else {
      return do_list_cmd(ua, cmd, HORZ_LIST);  /* do a short list */
   }
}

static int do_list_cmd(UAContext *ua, const char *cmd, e_list_type llist)
{
   POOLMEM *VolumeName;
   int jobid=0, n;
   int i, j;
   JOB_DBR jr;
   POOL_DBR pr;
   MEDIA_DBR mr;

   if (!open_new_client_db(ua))
      return 1;

   memset(&jr, 0, sizeof(jr));
   memset(&pr, 0, sizeof(pr));

   Dmsg1(20, "list: %s\n", cmd);

   if (!ua->db) {
      ua->error_msg(_("Hey! DB is NULL\n"));
   }
   /* Apply any limit */
   for (j = 1; j < ua->argc ; j++) {
      if (strcasecmp(ua->argk[j], NT_("joberrors")) == 0) {
         jr.JobErrors = 1;
      } else if (!ua->argv[j]) {
         /* skip */
      } else if (strcasecmp(ua->argk[j], NT_("order")) == 0) {
         if ((strcasecmp(ua->argv[j], NT_("desc")) == 0) ||
            strcasecmp(ua->argv[j], NT_("descending")) == 0) {
            jr.order = 1;
         } else if ((strcasecmp(ua->argv[j], NT_("asc")) == 0) ||
            strcasecmp(ua->argv[j], NT_("ascending")) == 0) {
            jr.order = 0;
         } else {
            ua->error_msg(_("Unknown order type %s\n"), ua->argv[j]);
            return 1;
         }
      } else if (strcasecmp(ua->argk[j], NT_("limit")) == 0) {
         jr.limit = atoi(ua->argv[j]);

      } else if (strcasecmp(ua->argk[j], NT_("jobstatus")) == 0) {
         if (B_ISALPHA(ua->argv[j][0])) {
            jr.JobStatus = ua->argv[j][0]; /* TODO: Check if the code is correct */
         }
      } else if (strcasecmp(ua->argk[j], NT_("jobtype")) == 0) {
         if (B_ISALPHA(ua->argv[j][0])) {
            jr.JobType = ua->argv[j][0]; /* TODO: Check if the code is correct */
         }
      } else if (strcasecmp(ua->argk[j], NT_("level")) == 0) {
         if (strlen(ua->argv[j]) > 1) {
            jr.JobLevel = get_level_code_from_name(ua->argv[j]);

         } else if (B_ISALPHA(ua->argv[j][0])) {
            jr.JobLevel = ua->argv[j][0]; /* TODO: Check if the code is correct */
         }
      } else if (strcasecmp(ua->argk[j], NT_("level")) == 0) {


      } else if (strcasecmp(ua->argk[j], NT_("client")) == 0) {
         if (is_name_valid(ua->argv[j], NULL)) {
            CLIENT_DBR cr;
            memset(&cr, 0, sizeof(cr));
            /* Both Backup & Restore wants to list jobs for this client */
            if(get_client_dbr(ua, &cr, JT_BACKUP_RESTORE)) {
               jr.ClientId = cr.ClientId;
            }
         }
      }
   }

   /* Scan arguments looking for things to do */
   for (i=1; i<ua->argc; i++) {
      /* List JOBS */
      if (strcasecmp(ua->argk[i], NT_("jobs")) == 0) {
         db_list_job_records(ua->jcr, ua->db, &jr, prtit, ua, llist);

         /* List JOBTOTALS */
      } else if (strcasecmp(ua->argk[i], NT_("jobtotals")) == 0) {
         db_list_job_totals(ua->jcr, ua->db, &jr, prtit, ua);

      /* List JOBID=nn */
      } else if (strcasecmp(ua->argk[i], NT_("jobid")) == 0) {
         if (ua->argv[i]) {
            jobid = str_to_int64(ua->argv[i]);
            if (jobid > 0) {
               jr.JobId = jobid;
               db_list_job_records(ua->jcr, ua->db, &jr, prtit, ua, llist);
            }
         }

      /* List JOB=xxx */
      } else if ((strcasecmp(ua->argk[i], NT_("job")) == 0 ||
                  strcasecmp(ua->argk[i], NT_("jobname")) == 0) && ua->argv[i]) {
         bstrncpy(jr.Name, ua->argv[i], MAX_NAME_LENGTH);
         jr.JobId = 0;
         db_list_job_records(ua->jcr, ua->db, &jr, prtit, ua, llist);

      /* List UJOBID=xxx */
      } else if (strcasecmp(ua->argk[i], NT_("ujobid")) == 0 && ua->argv[i]) {
         bstrncpy(jr.Job, ua->argv[i], MAX_NAME_LENGTH);
         jr.JobId = 0;
         db_list_job_records(ua->jcr, ua->db, &jr, prtit, ua, llist);

      /* List Base files */
      } else if (strcasecmp(ua->argk[i], NT_("basefiles")) == 0) {
         /* TODO: cleanup this block */
         for (j=i+1; j<ua->argc; j++) {
            if (strcasecmp(ua->argk[j], NT_("ujobid")) == 0 && ua->argv[j]) {
               bstrncpy(jr.Job, ua->argv[j], MAX_NAME_LENGTH);
               jr.JobId = 0;
               db_get_job_record(ua->jcr, ua->db, &jr);
               jobid = jr.JobId;
            } else if (strcasecmp(ua->argk[j], NT_("jobid")) == 0 && ua->argv[j]) {
               jobid = str_to_int64(ua->argv[j]);
            } else {
               continue;
            }
            if (jobid > 0) {
               db_list_base_files_for_job(ua->jcr, ua->db, jobid, prtit, ua);
            }
         }

      /* List FILES */
      } else if (strcasecmp(ua->argk[i], NT_("files")) == 0) {
         int deleted = 0;       /* see only backed up files */
         for (j=i+1; j<ua->argc; j++) {
            if (strcasecmp(ua->argk[j], NT_("ujobid")) == 0 && ua->argv[j]) {
               bstrncpy(jr.Job, ua->argv[j], MAX_NAME_LENGTH);
               jr.JobId = 0;
               db_get_job_record(ua->jcr, ua->db, &jr);
               jobid = jr.JobId;

            } else if (strcasecmp(ua->argk[j], NT_("jobid")) == 0 && ua->argv[j]) {
               jobid = str_to_int64(ua->argv[j]);

            } else if (strcasecmp(ua->argk[j], NT_("type")) == 0 && ua->argv[j]) {
               if (strcasecmp(ua->argv[j], NT_("deleted")) == 0) {
                  deleted = 1;
               } else if (strcasecmp(ua->argv[j], NT_("all")) == 0) {
                  deleted = -1;
               }
               continue;        /* Type should be before the jobid... */
            } else {
               continue;
            }
            if (jobid > 0) {
               db_list_files_for_job(ua->jcr, ua->db, jobid, deleted, prtit, ua);
            }
         }

      /* List JOBMEDIA */
      } else if (strcasecmp(ua->argk[i], NT_("jobmedia")) == 0) {
         bool done = false;
         for (j=i+1; j<ua->argc; j++) {
            if (strcasecmp(ua->argk[j], NT_("ujobid")) == 0 && ua->argv[j]) {
               bstrncpy(jr.Job, ua->argv[j], MAX_NAME_LENGTH);
               jr.JobId = 0;
               db_get_job_record(ua->jcr, ua->db, &jr);
               jobid = jr.JobId;
            } else if (strcasecmp(ua->argk[j], NT_("jobid")) == 0 && ua->argv[j]) {
               jobid = str_to_int64(ua->argv[j]);
            } else {
               continue;
            }
            db_list_jobmedia_records(ua->jcr, ua->db, jobid, prtit, ua, llist);
            done = true;
         }
         if (!done) {
            /* List for all jobs (jobid=0) */
            db_list_jobmedia_records(ua->jcr, ua->db, 0, prtit, ua, llist);
         }

      /* List JOBLOG */
      } else if (strcasecmp(ua->argk[i], NT_("joblog")) == 0) {
         bool done = false;
         for (j=i+1; j<ua->argc; j++) {
            if (strcasecmp(ua->argk[j], NT_("ujobid")) == 0 && ua->argv[j]) {
               bstrncpy(jr.Job, ua->argv[j], MAX_NAME_LENGTH);
               jr.JobId = 0;
               db_get_job_record(ua->jcr, ua->db, &jr);
               jobid = jr.JobId;
            } else if (strcasecmp(ua->argk[j], NT_("jobid")) == 0 && ua->argv[j]) {
               jobid = str_to_int64(ua->argv[j]);
            } else {
               continue;
            }
            db_list_joblog_records(ua->jcr, ua->db, jobid, prtit, ua, llist);
            done = true;
         }
         if (!done) {
            /* List for all jobs (jobid=0) */
            db_list_joblog_records(ua->jcr, ua->db, 0, prtit, ua, llist);
         }


      /* List POOLS */
      } else if (strcasecmp(ua->argk[i], NT_("pool")) == 0 ||
                 strcasecmp(ua->argk[i], NT_("pools")) == 0) {
         POOL_DBR pr;
         memset(&pr, 0, sizeof(pr));
         if (ua->argv[i]) {
            bstrncpy(pr.Name, ua->argv[i], sizeof(pr.Name));
         }
         db_list_pool_records(ua->jcr, ua->db, &pr, prtit, ua, llist);

      } else if (strcasecmp(ua->argk[i], NT_("clients")) == 0) {
         db_list_client_records(ua->jcr, ua->db, prtit, ua, llist);

      } else if (strcasecmp(ua->argk[i], NT_("pluginrestoreconf")) == 0) {
         ROBJECT_DBR rr;
         memset(&rr, 0, sizeof(rr));
         rr.FileType = FT_PLUGIN_CONFIG;

         for (j=i+1; j<ua->argc; j++) {
            if (strcasecmp(ua->argk[j], NT_("ujobid")) == 0 && ua->argv[j]) {
               bstrncpy(jr.Job, ua->argv[j], MAX_NAME_LENGTH);
               jr.JobId = 0;

            } else if (strcasecmp(ua->argk[j], NT_("jobid")) == 0 && ua->argv[j]) {

               if (acl_access_jobid_ok(ua, ua->argv[j])) {

                  if (is_a_number(ua->argv[j])) {
                     rr.JobId = str_to_uint64(ua->argv[j]);

                  } else if (is_a_number_list(ua->argv[j])) {
                     /* In this case, loop directly to find if all jobids are
                      * accessible */
                     rr.JobIds = ua->argv[j];
                  }

               } else {
                  ua->error_msg(_("Invalid jobid argument\n"));
                  return 1;
               }

            } else if (((strcasecmp(ua->argk[j], NT_("id")) == 0) ||
                        (strcasecmp(ua->argk[j], NT_("restoreobjectid")) == 0))
                       && ua->argv[j])
            {
               rr.RestoreObjectId = str_to_uint64(ua->argv[j]);

            } else if (strcasecmp(ua->argk[j], NT_("objecttype")) == 0 && ua->argv[j]) {
               if (strcasecmp(ua->argv[j], NT_("PLUGIN_CONFIG")) == 0) {
                  rr.FileType = FT_PLUGIN_CONFIG;

               } else if (strcasecmp(ua->argv[j], NT_("PLUGIN_CONFIG_FILLED")) == 0) {
                  rr.FileType = FT_PLUGIN_CONFIG_FILLED;

               } else if (strcasecmp(ua->argv[j], NT_("RESTORE_FIRST")) == 0) {
                  rr.FileType = FT_RESTORE_FIRST;

               } else if (strcasecmp(ua->argv[j], NT_("ALL")) == 0) {
                  rr.FileType = 0;

               } else {
                  ua->error_msg(_("Unknown ObjectType %s\n"), ua->argv[j]);
                  return 1;
               }

            } else {
               continue;
            }
         }

         if (!rr.JobId && !rr.JobIds) {
            ua->error_msg(_("list pluginrestoreconf requires jobid argument\n"));
            return 1;
         }

          /* Display the content of the restore object */
         if (rr.RestoreObjectId > 0) {
            /* Here, the JobId and the RestoreObjectId are set */
            if (db_get_restoreobject_record(ua->jcr, ua->db, &rr)) {
               ua->send_msg("%s\n", NPRTB(rr.object));
            } else {
               Dmsg0(200, "Object not found\n");
            }

         } else {
            db_list_restore_objects(ua->jcr, ua->db, &rr, prtit, ua, llist);
         }

         db_free_restoreobject_record(ua->jcr, &rr);
         return 1;

      /* List MEDIA or VOLUMES */
      } else if (strcasecmp(ua->argk[i], NT_("media")) == 0 ||
                 strcasecmp(ua->argk[i], NT_("volume")) == 0 ||
                 strcasecmp(ua->argk[i], NT_("volumes")) == 0) {
         bool done = false;
         for (j=i+1; j<ua->argc; j++) {
            if (strcasecmp(ua->argk[j], NT_("ujobid")) == 0 && ua->argv[j]) {
               bstrncpy(jr.Job, ua->argv[j], MAX_NAME_LENGTH);
               jr.JobId = 0;
               db_get_job_record(ua->jcr, ua->db, &jr);
               jobid = jr.JobId;
            } else if (strcasecmp(ua->argk[j], NT_("jobid")) == 0 && ua->argv[j]) {
               jobid = str_to_int64(ua->argv[j]);
            } else {
               continue;
            }
            VolumeName = get_pool_memory(PM_FNAME);
            n = db_get_job_volume_names(ua->jcr, ua->db, jobid, &VolumeName);
            ua->send_msg(_("Jobid %d used %d Volume(s): %s\n"), jobid, n, VolumeName);
            free_pool_memory(VolumeName);
            done = true;
         }

         /* if no job or jobid keyword found, then we list all media */
         if (!done) {
            int num_pools;
            uint32_t *ids;
            /* List a specific volume? */
            if (ua->argv[i] && *ua->argv[i]) {
               bstrncpy(mr.VolumeName, ua->argv[i], sizeof(mr.VolumeName));
               db_list_media_records(ua->jcr, ua->db, &mr, prtit, ua, llist);
               return 1;
            }
            /* Is a specific pool wanted? */
            for (i=1; i<ua->argc; i++) {
               if (strcasecmp(ua->argk[i], NT_("pool")) == 0) {
                  if (!get_pool_dbr(ua, &pr)) {
                     ua->error_msg(_("No Pool specified.\n"));
                     return 1;
                  }
                  mr.PoolId = pr.PoolId;
                  db_list_media_records(ua->jcr, ua->db, &mr, prtit, ua, llist);
                  return 1;
               }
            }

            /* List Volumes in all pools */
            if (!db_get_pool_ids(ua->jcr, ua->db, &num_pools, &ids)) {
               ua->error_msg(_("Error obtaining pool ids. ERR=%s\n"),
                        db_strerror(ua->db));
               return 1;
            }
            if (num_pools <= 0) {
               return 1;
            }
            for (i=0; i < num_pools; i++) {
               pr.PoolId = ids[i];
               if (db_get_pool_record(ua->jcr, ua->db, &pr)) {
                  ua->send_msg(_("Pool: %s\n"), pr.Name);
               }
               mr.PoolId = ids[i];
               db_list_media_records(ua->jcr, ua->db, &mr, prtit, ua, llist);
            }
            free(ids);
            return 1;
         }
      /* List next volume */
      } else if (strcasecmp(ua->argk[i], NT_("nextvol")) == 0 ||
                 strcasecmp(ua->argk[i], NT_("nextvolume")) == 0) {
         n = 1;
         j = find_arg_with_value(ua, NT_("days"));
         if (j >= 0) {
            n = atoi(ua->argv[j]);
            if ((n < 0) || (n > 50)) {
              ua->warning_msg(_("Ignoring invalid value for days. Max is 50.\n"));
              n = 1;
            }
         }
         list_nextvol(ua, n);
      } else if (strcasecmp(ua->argk[i], NT_("copies")) == 0) {
         char *jobids = NULL;
         uint32_t limit=0;
         for (j=i+1; j<ua->argc; j++) {
            if (strcasecmp(ua->argk[j], NT_("jobid")) == 0 && ua->argv[j]) {
               if (is_a_number_list(ua->argv[j])) {
                  jobids = ua->argv[j];
               }
            } else if (strcasecmp(ua->argk[j], NT_("limit")) == 0 && ua->argv[j]) {
               limit = atoi(ua->argv[j]);
            }
         }
         db_list_copies_records(ua->jcr,ua->db,limit,jobids,prtit,ua,llist);
      } else if (strcasecmp(ua->argk[i], NT_("limit")) == 0
                 || strcasecmp(ua->argk[i], NT_("days")) == 0
                 || strcasecmp(ua->argk[i], NT_("joberrors")) == 0
                 || strcasecmp(ua->argk[i], NT_("order")) == 0
                 || strcasecmp(ua->argk[i], NT_("jobstatus")) == 0
                 || strcasecmp(ua->argk[i], NT_("client")) == 0
                 || strcasecmp(ua->argk[i], NT_("type")) == 0
                 || strcasecmp(ua->argk[i], NT_("level")) == 0
                 || strcasecmp(ua->argk[i], NT_("jobtype")) == 0
                 || strcasecmp(ua->argk[i], NT_("long")) == 0
         ) {
         /* Ignore it */
      } else if (strcasecmp(ua->argk[i], NT_("snapshot")) == 0 ||
                 strcasecmp(ua->argk[i], NT_("snapshots")) == 0) 
      {
         snapshot_list(ua, i, prtit, llist);
         return 1;

      } else {
         ua->error_msg(_("Unknown list keyword: %s\n"), NPRT(ua->argk[i]));
      }
   }
   return 1;
}

static bool list_nextvol(UAContext *ua, int ndays)
{
   JOB *job;
   JCR *jcr;
   USTORE store;
   RUN *run;
   utime_t runtime;
   bool found = false;
   MEDIA_DBR mr;
   POOL_DBR pr;
   POOL_MEM errmsg;
   char edl[50];

   int i = find_arg_with_value(ua, "job");
   if (i <= 0) {
      if ((job = select_job_resource(ua)) == NULL) {
         return false;
      }
   } else {
      job = (JOB *)GetResWithName(R_JOB, ua->argv[i]);
      if (!job) {
         Jmsg(ua->jcr, M_ERROR, 0, _("%s is not a job name.\n"), ua->argv[i]);
         if ((job = select_job_resource(ua)) == NULL) {
            return false;
         }
      }
   }

   jcr = new_jcr(sizeof(JCR), dird_free_jcr);
   for (run=NULL; (run = find_next_run(run, job, runtime, ndays)); ) {
      if (!complete_jcr_for_job(jcr, job, run->pool)) {
         found = false;
         goto get_out;
      }
      if (!jcr->jr.PoolId) {
         ua->error_msg(_("Could not find Pool for Job %s\n"), job->name());
         continue;
      }
      bmemset(&pr, 0, sizeof(pr));
      pr.PoolId = jcr->jr.PoolId;
      if (!db_get_pool_record(jcr, jcr->db, &pr)) {
         bstrncpy(pr.Name, "*UnknownPool*", sizeof(pr.Name));
      }
      mr.PoolId = jcr->jr.PoolId;
      get_job_storage(&store, job, run);
      set_storageid_in_mr(store.store, &mr);
      /* no need to set ScratchPoolId, since we use fnv_no_create_vol */
      if (!find_next_volume_for_append(jcr, &mr, 1, fnv_no_create_vol, fnv_prune, errmsg)) {
         ua->error_msg(_("Could not find next Volume for Job %s (Pool=%s, Level=%s). %s\n"),
            job->name(), pr.Name, level_to_str(edl, sizeof(edl), run->level), errmsg.c_str());
      } else {
         ua->send_msg(
            _("The next Volume to be used by Job \"%s\" (Pool=%s, Level=%s) will be %s\n"),
            job->name(), pr.Name, level_to_str(edl, sizeof(edl), run->level), mr.VolumeName);
         found = true;
      }
   }

get_out:
   if (jcr->db) db_close_database(jcr, jcr->db);
   jcr->db = NULL;
   free_jcr(jcr);
   if (!found) {
      ua->error_msg(_("Could not find next Volume for Job %s.\n"),
         job->hdr.name);
      return false;
   }
   return true;
}


/*
 * For a given job, we examine all his run records
 *  to see if it is scheduled today or tomorrow.
 */
RUN *find_next_run(RUN *run, JOB *job, utime_t &runtime, int ndays)
{
   time_t now, future, endtime;
   SCHED *sched;
   struct tm tm, runtm;
   int mday, wday, month, wom, i;
   int woy, ldom;
   int day;
   bool is_scheduled;

   sched = job->schedule;
   if (!sched || !job->is_enabled() || (sched && !sched->is_enabled()) ||
       (job->client && !job->client->is_enabled())) {
      return NULL;                 /* no nothing to report */
   }

   /* Break down the time into components */
   now = time(NULL);
   endtime = now + (ndays * 60 * 60 * 24);

   if (run == NULL) {
      run = sched->run;
   } else {
      run = run->next;
   }
   for ( ; run; run=run->next) {
      /*
       * Find runs in next 24 hours.  Day 0 is today, so if
       *   ndays=1, look at today and tomorrow.
       */
      for (day = 0; day <= ndays; day++) {
         future = now + (day * 60 * 60 * 24);

         /* Break down the time into components */
         (void)localtime_r(&future, &tm);
         mday = tm.tm_mday - 1;
         wday = tm.tm_wday;
         month = tm.tm_mon;
         wom = mday / 7;
         woy = tm_woy(future);
         ldom = tm_ldom(month, tm.tm_year + 1900);

         is_scheduled = (bit_is_set(mday, run->mday) &&
                         bit_is_set(wday, run->wday) &&
                         bit_is_set(month, run->month) &&
                         bit_is_set(wom, run->wom) &&
                         bit_is_set(woy, run->woy)) ||
                        (bit_is_set(month, run->month) &&
                         bit_is_set(31, run->mday) && mday == ldom);

#ifdef xxx
         Pmsg2(000, "day=%d is_scheduled=%d\n", day, is_scheduled);
         Pmsg1(000, "bit_set_mday=%d\n", bit_is_set(mday, run->mday));
         Pmsg1(000, "bit_set_wday=%d\n", bit_is_set(wday, run->wday));
         Pmsg1(000, "bit_set_month=%d\n", bit_is_set(month, run->month));
         Pmsg1(000, "bit_set_wom=%d\n", bit_is_set(wom, run->wom));
         Pmsg1(000, "bit_set_woy=%d\n", bit_is_set(woy, run->woy));
#endif

         if (is_scheduled) { /* Jobs scheduled on that day */
#ifdef xxx
            char buf[300], num[10];
            bsnprintf(buf, sizeof(buf), "tm.hour=%d hour=", tm.tm_hour);
            for (i=0; i<24; i++) {
               if (bit_is_set(i, run->hour)) {
                  bsnprintf(num, sizeof(num), "%d ", i);
                  bstrncat(buf, num, sizeof(buf));
               }
            }
            bstrncat(buf, "\n", sizeof(buf));
            Pmsg1(000, "%s", buf);
#endif
            /* find time (time_t) job is to be run */
            (void)localtime_r(&future, &runtm);
            for (i= 0; i < 24; i++) {
               if (bit_is_set(i, run->hour)) {
                  runtm.tm_hour = i;
                  runtm.tm_min = run->minute;
                  runtm.tm_sec = 0;
                  runtime = mktime(&runtm);
                  Dmsg2(200, "now=%d runtime=%lld\n", now, runtime);
                  if ((runtime > now) && (runtime < endtime)) {
                     Dmsg2(200, "Found it level=%d %c\n", run->level, run->level);
                     return run;         /* found it, return run resource */
                  }
               }
            }
         }
      }
   } /* end for loop over runs */
   /* Nothing found */
   return NULL;
}

/*
 * Fill in the remaining fields of the jcr as if it
 *  is going to run the job.
 */
bool complete_jcr_for_job(JCR *jcr, JOB *job, POOL *pool)
{
   POOL_DBR pr;

   bmemset(&pr, 0, sizeof(POOL_DBR));
   set_jcr_defaults(jcr, job);
   if (pool) {
      jcr->pool = pool;               /* override */
   }
   if (jcr->db) {
      Dmsg0(100, "complete_jcr close db\n");
      db_close_database(jcr, jcr->db);
      jcr->db = NULL;
   }

   Dmsg0(100, "complete_jcr open db\n");
   jcr->db = db_init_database(jcr, jcr->catalog->db_driver, jcr->catalog->db_name,
                jcr->catalog->db_user,
                jcr->catalog->db_password, jcr->catalog->db_address,
                jcr->catalog->db_port, jcr->catalog->db_socket,
                jcr->catalog->db_ssl_mode, jcr->catalog->db_ssl_key,
                jcr->catalog->db_ssl_cert, jcr->catalog->db_ssl_ca,
                jcr->catalog->db_ssl_capath, jcr->catalog->db_ssl_cipher,
                jcr->catalog->mult_db_connections,
                jcr->catalog->disable_batch_insert);
   if (!jcr->db || !db_open_database(jcr, jcr->db)) {
      Jmsg(jcr, M_FATAL, 0, _("Could not open database \"%s\".\n"),
                 jcr->catalog->db_name);
      if (jcr->db) {
         Jmsg(jcr, M_FATAL, 0, "%s", db_strerror(jcr->db));
         db_close_database(jcr, jcr->db);
         jcr->db = NULL;
      }
      return false;
   }
   bstrncpy(pr.Name, jcr->pool->name(), sizeof(pr.Name));
   while (!db_get_pool_record(jcr, jcr->db, &pr)) { /* get by Name */
      /* Try to create the pool */
      if (create_pool(jcr, jcr->db, jcr->pool, POOL_OP_CREATE) < 0) {
         Jmsg(jcr, M_FATAL, 0, _("Pool %s not in database. %s"), pr.Name,
            db_strerror(jcr->db));
         if (jcr->db) {
            db_close_database(jcr, jcr->db);
            jcr->db = NULL;
         }
         return false;
      } else {
         Jmsg(jcr, M_INFO, 0, _("Pool %s created in database.\n"), pr.Name);
      }
   }
   jcr->jr.PoolId = pr.PoolId;
   return true;
}


static void con_lock_release(void *arg)
{
   Vw(con_lock);
}

void do_messages(UAContext *ua, const char *cmd)
{
   char msg[2000];
   int mlen;
   bool do_truncate = false;

   if (ua->jcr) {
      dequeue_messages(ua->jcr);
   }
   Pw(con_lock);
   pthread_cleanup_push(con_lock_release, (void *)NULL);
   rewind(con_fd);
   while (fgets(msg, sizeof(msg), con_fd)) {
      mlen = strlen(msg);
      ua->UA_sock->msg = check_pool_memory_size(ua->UA_sock->msg, mlen+1);
      strcpy(ua->UA_sock->msg, msg);
      ua->UA_sock->msglen = mlen;
      ua->UA_sock->send();
      do_truncate = true;
   }
   if (do_truncate) {
      (void)ftruncate(fileno(con_fd), 0L);
   }
   console_msg_pending = false;
   ua->user_notified_msg_pending = false;
   pthread_cleanup_pop(0);
   Vw(con_lock);
}


int qmessagescmd(UAContext *ua, const char *cmd)
{
   if (console_msg_pending && ua->auto_display_messages) {
      do_messages(ua, cmd);
   }
   return 1;
}

int messagescmd(UAContext *ua, const char *cmd)
{
   if (console_msg_pending) {
      do_messages(ua, cmd);
   } else {
      ua->UA_sock->fsend(_("You have no messages.\n"));
   }
   return 1;
}

/*
 * Callback routine for "printing" database file listing
 */
void prtit(void *ctx, const char *msg)
{
   UAContext *ua = (UAContext *)ctx;

   if (ua) ua->send_msg("%s", msg);
}

/*
 * Format message and send to other end.

 * If the UA_sock is NULL, it means that there is no user
 * agent, so we are being called from Bacula core. In
 * that case direct the messages to the Job.
 */
#ifdef HAVE_VA_COPY
void bmsg(UAContext *ua, const char *fmt, va_list arg_ptr)
{
   BSOCK *bs = ua->UA_sock;
   int maxlen, len;
   POOLMEM *msg = NULL;
   va_list ap;

   if (bs) {
      msg = bs->msg;
   }
   if (!msg) {
      msg = get_pool_memory(PM_EMSG);
   }

again:
   maxlen = sizeof_pool_memory(msg) - 1;
   va_copy(ap, arg_ptr);
   len = bvsnprintf(msg, maxlen, fmt, ap);
   va_end(ap);
   if (len < 0 || len >= maxlen) {
      msg = realloc_pool_memory(msg, maxlen + maxlen/2);
      goto again;
   }

   if (bs) {
      bs->msg = msg;
      bs->msglen = len;
      bs->send();
   } else {                           /* No UA, send to Job */
      Jmsg(ua->jcr, M_INFO, 0, "%s", msg);
      free_pool_memory(msg);
   }

}

#else /* no va_copy() -- brain damaged version of variable arguments */

void bmsg(UAContext *ua, const char *fmt, va_list arg_ptr)
{
   BSOCK *bs = ua->UA_sock;
   int maxlen, len;
   POOLMEM *msg = NULL;

   if (bs) {
      msg = bs->msg;
   }
   if (!msg) {
      msg = get_memory(5000);
   }

   maxlen = sizeof_pool_memory(msg) - 1;
   if (maxlen < 4999) {
      msg = realloc_pool_memory(msg, 5000);
      maxlen = 4999;
   }
   len = bvsnprintf(msg, maxlen, fmt, arg_ptr);
   if (len < 0 || len >= maxlen) {
      pm_strcpy(msg, _("Message too long to display.\n"));
      len = strlen(msg);
   }

   if (bs) {
      bs->msg = msg;
      bs->msglen = len;
      bs->send();
   } else {                           /* No UA, send to Job */
      Jmsg(ua->jcr, M_INFO, 0, "%s", msg);
      free_pool_memory(msg);
   }

}
#endif

void bsendmsg(void *ctx, const char *fmt, ...)
{
   va_list arg_ptr;
   va_start(arg_ptr, fmt);
   bmsg((UAContext *)ctx, fmt, arg_ptr);
   va_end(arg_ptr);
}

/*
 * The following UA methods are mainly intended for GUI
 * programs
 */
/*
 * This is a message that should be displayed on the user's
 *  console.
 */
void UAContext::send_msg(const char *fmt, ...)
{
   va_list arg_ptr;
   va_start(arg_ptr, fmt);
   bmsg(this, fmt, arg_ptr);
   va_end(arg_ptr);
}


/*
 * This is an error condition with a command. The gui should put
 *  up an error or critical dialog box.  The command is aborted.
 */
void UAContext::error_msg(const char *fmt, ...)
{
   BSOCK *bs = UA_sock;
   va_list arg_ptr;

   if (bs && api) bs->signal(BNET_ERROR_MSG);
   va_start(arg_ptr, fmt);
   bmsg(this, fmt, arg_ptr);
   va_end(arg_ptr);
}

/*
 * This is a warning message, that should bring up a warning
 *  dialog box on the GUI. The command is not aborted, but something
 *  went wrong.
 */
void UAContext::warning_msg(const char *fmt, ...)
{
   BSOCK *bs = UA_sock;
   va_list arg_ptr;

   if (bs && api) bs->signal(BNET_WARNING_MSG);
   va_start(arg_ptr, fmt);
   bmsg(this, fmt, arg_ptr);
   va_end(arg_ptr);
}

/*
 * This is an information message that should probably be put
 *  into the status line of a GUI program.
 */
void UAContext::info_msg(const char *fmt, ...)
{
   BSOCK *bs = UA_sock;
   va_list arg_ptr;

   if (bs && api) bs->signal(BNET_INFO_MSG);
   va_start(arg_ptr, fmt);
   bmsg(this, fmt, arg_ptr);
   va_end(arg_ptr);
}
