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
 *   Bacula Director -- User Agent Database restore Command
 *      Creates a bootstrap file for restoring files and
 *      starts the restore job.
 *
 *      Tree handling routines split into ua_tree.c July MMIII.
 *      BSR (bootstrap record) handling routines split into
 *        bsr.c July MMIII
 *
 *     Kern Sibbald, July MMII
 */


#include "bacula.h"
#include "dird.h"

/* Imported functions */
extern void print_bsr(UAContext *ua, RBSR *bsr);


/* Forward referenced functions */
static int last_full_handler(void *ctx, int num_fields, char **row);
static int jobid_handler(void *ctx, int num_fields, char **row);
static int user_select_jobids_or_files(UAContext *ua, RESTORE_CTX *rx);
static int fileset_handler(void *ctx, int num_fields, char **row);
static void free_name_list(NAME_LIST *name_list);
static bool select_backups_before_date(UAContext *ua, RESTORE_CTX *rx, char *date);
static bool build_directory_tree(UAContext *ua, RESTORE_CTX *rx);
static void split_path_and_filename(UAContext *ua, RESTORE_CTX *rx, char *fname);
static int jobid_fileindex_handler(void *ctx, int num_fields, char **row);
static bool insert_file_into_findex_list(UAContext *ua, RESTORE_CTX *rx, char *file,
                                         char *date);
static bool insert_dir_into_findex_list(UAContext *ua, RESTORE_CTX *rx, char *dir,
                                        char *date);
static void insert_one_file_or_dir(UAContext *ua, RESTORE_CTX *rx, char *date, bool dir);
static int get_client_name(UAContext *ua, RESTORE_CTX *rx);
static int get_restore_client_name(UAContext *ua, RESTORE_CTX &rx, char * RestoreClient);
static bool get_date(UAContext *ua, char *date, int date_len);
static int restore_count_handler(void *ctx, int num_fields, char **row);
static void get_and_display_basejobs(UAContext *ua, RESTORE_CTX *rx);

void new_rx(RESTORE_CTX *rx)
{
   RBSR *bsr = NULL;
   memset(rx, 0, sizeof(*rx));
   rx->path = get_pool_memory(PM_FNAME);
   rx->path[0] = 0;

   rx->fname = get_pool_memory(PM_FNAME);
   rx->fname[0] = 0;

   rx->JobIds = get_pool_memory(PM_FNAME);
   rx->JobIds[0] = 0;

   rx->component_fname = get_pool_memory(PM_FNAME);
   rx->component_fname[0] = 0;

   rx->BaseJobIds = get_pool_memory(PM_FNAME);
   rx->BaseJobIds[0] = 0;

   rx->query = get_pool_memory(PM_FNAME);
   rx->query[0] = 0;

   rx->bsr_list = New(rblist(bsr, &bsr->link));
   rx->hardlinks_in_mem = true;
}


/*
 *   Restore files
 *
 */
int restore_cmd(UAContext *ua, const char *cmd)
{
   RESTORE_CTX rx;                    /* restore context */
   POOL_MEM buf;
   JOB *job;
   int i;
   JCR *jcr = ua->jcr;
   char *escaped_bsr_name = NULL;
   char *escaped_where_name = NULL;
   char *strip_prefix, *add_prefix, *add_suffix, *regexp;
   strip_prefix = add_prefix = add_suffix = regexp = NULL;

   new_rx(&rx);                 /* Initialize RESTORE_CTX */
   
   if (!open_new_client_db(ua)) {
      goto bail_out;
   }

   for (i = 0; i < ua->argc ; i++) {
      if (strcasecmp(ua->argk[i], "fdcalled") == 0) {
         rx.fdcalled = true;

      } else if (strcasecmp(ua->argk[i], "noautoparent") == 0) {
         rx.no_auto_parent = true;
      }
      if (!ua->argv[i]) {
         continue;           /* skip if no value given */
      }
      if (strcasecmp(ua->argk[i], "comment") == 0) {
         rx.comment = ua->argv[i];
         if (!is_comment_legal(ua, rx.comment)) {
            goto bail_out;
         }

      } else if (strcasecmp(ua->argk[i], "where") == 0) {
         rx.where = ua->argv[i];

      } else if (strcasecmp(ua->argk[i], "when") == 0) {
         rx.when = ua->argv[i];

      } else if (strcasecmp(ua->argk[i], "replace") == 0) {
         rx.replace = ua->argv[i];

      } else if (strcasecmp(ua->argk[i], "strip_prefix") == 0) {
         strip_prefix = ua->argv[i];

      } else if (strcasecmp(ua->argk[i], "add_prefix") == 0) {
         add_prefix = ua->argv[i];

      } else if (strcasecmp(ua->argk[i], "add_suffix") == 0) {
         add_suffix = ua->argv[i];

      } else if (strcasecmp(ua->argk[i], "regexwhere") == 0) {
         rx.RegexWhere = ua->argv[i];

      } else if (strcasecmp(ua->argk[i], "optimizespeed") == 0) {
         if (strcasecmp(ua->argv[i], "0") || strcasecmp(ua->argv[i], "no") ||
             strcasecmp(ua->argv[i], "false")) {
            rx.hardlinks_in_mem = false;
         }
     }
   }

   if (strip_prefix || add_suffix || add_prefix) {
      int len = bregexp_get_build_where_size(strip_prefix, add_prefix, add_suffix);
      regexp = (char *)bmalloc(len * sizeof(char));

      bregexp_build_where(regexp, len, strip_prefix, add_prefix, add_suffix);
      rx.RegexWhere = regexp;
   }

   /* TODO: add acl for regexwhere ? */

   if (rx.RegexWhere) {
      if (!acl_access_ok(ua, Where_ACL, rx.RegexWhere)) {
         ua->error_msg(_("\"RegexWhere\" specification not authorized.\n"));
         goto bail_out;
      }
   }

   if (rx.where) {
      if (!acl_access_ok(ua, Where_ACL, rx.where)) {
         ua->error_msg(_("\"where\" specification not authorized.\n"));
         goto bail_out;
      }
   }

   /* Ensure there is at least one Restore Job */
   LockRes();
   foreach_res(job, R_JOB) {
      if (job->JobType == JT_RESTORE) {
         if (!rx.restore_job) {
            rx.restore_job = job;
         }
         rx.restore_jobs++;
      }
   }
   UnlockRes();
   if (!rx.restore_jobs) {
      ua->error_msg(_(
         "No Restore Job Resource found in bacula-dir.conf.\n"
         "You must create at least one before running this command.\n"));
      goto bail_out;
   }

   /*
    * Request user to select JobIds or files by various different methods
    *  last 20 jobs, where File saved, most recent backup, ...
    *  In the end, a list of files are pumped into
    *  add_findex()
    */
   switch (user_select_jobids_or_files(ua, &rx)) {
   case 0:                            /* error */
      goto bail_out;
   case 1:                            /* selected by jobid */
      get_and_display_basejobs(ua, &rx);
      if (!build_directory_tree(ua, &rx)) {
         ua->send_msg(_("Restore not done.\n"));
         goto bail_out;
      }
      break;
   case 2:                            /* selected by filename, no tree needed */
      break;
   }

   if (rx.bsr_list->size() > 0) {
      char ed1[50];
      if (!complete_bsr(ua, rx.bsr_list)) {   /* find Vol, SessId, SessTime from JobIds */
         ua->error_msg(_("Unable to construct a valid BSR. Cannot continue.\n"));
         goto bail_out;
      }
      if (!(rx.selected_files = write_bsr_file(ua, rx))) {
         ua->warning_msg(_("No files selected to be restored.\n"));
         goto bail_out;
      }

      ua->send_msg(_("Bootstrap records written to %s\n"), ua->jcr->RestoreBootstrap);
      display_bsr_info(ua, rx);          /* display vols needed, etc */

      if (rx.selected_files==1) {
         ua->info_msg(_("\n1 file selected to be restored.\n\n"));
      } else {
         ua->info_msg(_("\n%s files selected to be restored.\n\n"),
            edit_uint64_with_commas(rx.selected_files, ed1));
      }
   } else {
      ua->warning_msg(_("No files selected to be restored.\n"));
      goto bail_out;
   }

   if (rx.restore_jobs == 1) {
      job = rx.restore_job;
   } else {
      job = get_restore_job(ua);
   }
   if (!job) {
      goto bail_out;
   }

   get_client_name(ua, &rx);
   if (!rx.ClientName[0]) {
      ua->error_msg(_("No Client resource found!\n"));
      goto bail_out;
   }
   get_restore_client_name(ua, rx, job->RestoreClient);

   escaped_bsr_name = escape_filename(jcr->RestoreBootstrap);

   Mmsg(ua->cmd,
        "run job=\"%s\" client=\"%s\" restoreclient=\"%s\" storage=\"%s\""
        " bootstrap=\"%s\" files=%u catalog=\"%s\"",
        job->name(), rx.ClientName, rx.RestoreClientName,
        rx.store?rx.store->name():"",
        escaped_bsr_name ? escaped_bsr_name : jcr->RestoreBootstrap,
        rx.selected_files, ua->catalog->name());

   /* Build run command */
   pm_strcpy(buf, "");
   if (rx.RestoreMediaType[0]) {
      Mmsg(buf, " mediatype=\"%s\"", rx.RestoreMediaType);
      pm_strcat(ua->cmd, buf);
      pm_strcpy(buf, "");
   }
   if (rx.RegexWhere) {
      escaped_where_name = escape_filename(rx.RegexWhere);
      Mmsg(buf, " regexwhere=\"%s\"",
           escaped_where_name ? escaped_where_name : rx.RegexWhere);

   } else if (rx.where) {
      escaped_where_name = escape_filename(rx.where);
      Mmsg(buf," where=\"%s\"",
           escaped_where_name ? escaped_where_name : rx.where);
   }
   pm_strcat(ua->cmd, buf);

   if (rx.replace) {
      Mmsg(buf, " replace=%s", rx.replace);
      pm_strcat(ua->cmd, buf);
   }

   if (rx.fdcalled) {
      pm_strcat(ua->cmd, " fdcalled=yes");
   }

   if (rx.when) {
      Mmsg(buf, " when=\"%s\"", rx.when);
      pm_strcat(ua->cmd, buf);
   }

   if (rx.comment) {
      Mmsg(buf, " comment=\"%s\"", rx.comment);
      pm_strcat(ua->cmd, buf);
   }

   if (escaped_bsr_name != NULL) {
      bfree(escaped_bsr_name);
   }

   if (escaped_where_name != NULL) {
      bfree(escaped_where_name);
   }

   if (regexp) {
      bfree(regexp);
   }

   if (find_arg(ua, NT_("yes")) > 0) {
      pm_strcat(ua->cmd, " yes");    /* pass it on to the run command */
   }
   Dmsg1(200, "Submitting: %s\n", ua->cmd);
   /*
    * Transfer jobids, component stuff to jcr to
    *  pass to run_cmd().  Note, these are fields and
    *  other things that are not passed on the command
    *  line.
    */
   /* ***FIXME*** pass jobids on command line */
   if (jcr->JobIds) {
      free_pool_memory(jcr->JobIds);
   }
   jcr->JobIds = rx.JobIds;
   rx.JobIds = NULL;
   jcr->component_fname = rx.component_fname;
   rx.component_fname = NULL;
   jcr->component_fd = rx.component_fd;
   rx.component_fd = NULL;
   parse_ua_args(ua);
   run_cmd(ua, ua->cmd);
   free_rx(&rx);
   garbage_collect_memory();       /* release unused memory */
   return 1;

bail_out:
   if (escaped_bsr_name != NULL) {
      bfree(escaped_bsr_name);
   }

   if (escaped_where_name != NULL) {
      bfree(escaped_where_name);
   }

   if (regexp) {
      bfree(regexp);
   }

   /* Free the plugin config if needed, we don't want to re-use
    * this part of the next try
    */
   free_plugin_config_items(jcr->plugin_config);
   jcr->plugin_config = NULL;

   free_rx(&rx);
   garbage_collect_memory();       /* release unused memory */
   return 0;

}

/*
 * Fill the rx->BaseJobIds and display the list
 */
static void get_and_display_basejobs(UAContext *ua, RESTORE_CTX *rx)
{
   db_list_ctx jobids;

   if (!db_get_used_base_jobids(ua->jcr, ua->db, rx->JobIds, &jobids)) {
      ua->warning_msg("%s", db_strerror(ua->db));
   }

   if (jobids.count) {
      POOL_MEM q;
      Mmsg(q, uar_print_jobs, jobids.list);
      ua->send_msg(_("The restore will use the following job(s) as Base\n"));
      db_list_sql_query(ua->jcr, ua->db, q.c_str(), prtit, ua, 1, HORZ_LIST);
   }
   pm_strcpy(rx->BaseJobIds, jobids.list);
}

void free_rx(RESTORE_CTX *rx)
{
   free_bsr(rx->bsr_list);
   rx->bsr_list = NULL;
   free_and_null_pool_memory(rx->JobIds);
   free_and_null_pool_memory(rx->BaseJobIds);
   free_and_null_pool_memory(rx->fname);
   free_and_null_pool_memory(rx->path);
   free_and_null_pool_memory(rx->query);
   if (rx->fileregex) {
      free(rx->fileregex);
      rx->fileregex = NULL;
   }
   if (rx->component_fd) {
      fclose(rx->component_fd);
      rx->component_fd = NULL;
   }
   if (rx->component_fname) {
      unlink(rx->component_fname);
   }
   free_and_null_pool_memory(rx->component_fname);
   free_name_list(&rx->name_list);
}

static bool has_value(UAContext *ua, int i)
{
   if (!ua->argv[i]) {
      ua->error_msg(_("Missing value for keyword: %s\n"), ua->argk[i]);
      return false;
   }
   return true;
}

/*
 * This gets the client name from which the backup was made
 */
static int get_client_name(UAContext *ua, RESTORE_CTX *rx)
{
   /* If no client name specified yet, get it now */
   if (!rx->ClientName[0]) {
      CLIENT_DBR cr;
      /* try command line argument */
      int i = find_arg_with_value(ua, NT_("client"));
      if (i < 0) {
         i = find_arg_with_value(ua, NT_("backupclient"));
      }
      if (i >= 0) {
         if (!is_name_valid(ua->argv[i], &ua->errmsg)) {
            ua->error_msg("%s argument: %s", ua->argk[i], ua->errmsg);
            return 0;
         }
         bstrncpy(rx->ClientName, ua->argv[i], sizeof(rx->ClientName));
         return 1;
      }
      memset(&cr, 0, sizeof(cr));
      /* We want the name of the client where the backup was made */
      if (!get_client_dbr(ua, &cr, JT_BACKUP_RESTORE)) {
         return 0;
      }
      bstrncpy(rx->ClientName, cr.Name, sizeof(rx->ClientName));
   }
   return 1;
}

/*
 * This is where we pick up a client name to restore to.
 */
static int get_restore_client_name(UAContext *ua, RESTORE_CTX &rx, char * RestoreClient)
{
   /* Start with same name as backup client or set in RestoreClient */
   if (!RestoreClient){
      bstrncpy(rx.RestoreClientName, rx.ClientName, sizeof(rx.RestoreClientName));
   } else {
      bstrncpy(rx.RestoreClientName, RestoreClient, sizeof(rx.RestoreClientName));
   }

   /* try command line argument */
   int i = find_arg_with_value(ua, NT_("restoreclient"));
   if (i >= 0) {
      if (!is_name_valid(ua->argv[i], &ua->errmsg)) {
         ua->error_msg("%s argument: %s", ua->argk[i], ua->errmsg);
         return 0;
      }
      bstrncpy(rx.RestoreClientName, ua->argv[i], sizeof(rx.RestoreClientName));
      return 1;
   }
   return 1;
}



/*
 * The first step in the restore process is for the user to
 *  select a list of JobIds from which he will subsequently
 *  select which files are to be restored.
 *
 *  Returns:  2  if filename list made
 *            1  if jobid list made
 *            0  on error
 */
static int user_select_jobids_or_files(UAContext *ua, RESTORE_CTX *rx)
{
   char *p;
   char date[MAX_TIME_LENGTH];
   bool have_date = false;
   /* Include current second if using current time */
   utime_t now = time(NULL) + 1;
   JobId_t JobId;
   JOB_DBR jr = { (JobId_t)-1 };
   bool done = false;
   int i, j;
   const char *list[] = {
      _("List last 20 Jobs run"),
      _("List Jobs where a given File is saved"),
      _("Enter list of comma separated JobIds to select"),
      _("Enter SQL list command"),
      _("Select the most recent backup for a client"),
      _("Select backup for a client before a specified time"),
      _("Enter a list of files to restore"),
      _("Enter a list of files to restore before a specified time"),
      _("Find the JobIds of the most recent backup for a client"),
      _("Find the JobIds for a backup for a client before a specified time"),
      _("Enter a list of directories to restore for found JobIds"),
      _("Select full restore to a specified Job date"),
      _("Cancel"),
      NULL };

   const char *kw[] = {
       /* These keywords are handled in a for loop */
      "jobid",       /* 0 */
      "current",     /* 1 */
      "before",      /* 2 */
      "file",        /* 3 */
      "directory",   /* 4 */
      "select",      /* 5 */
      "pool",        /* 6 */
      "all",         /* 7 */

      /* The keyword below are handled by individual arg lookups */
      "client",       /* 8 */
      "storage",      /* 9 */
      "fileset",      /* 10 */
      "where",        /* 11 */
      "yes",          /* 12 */
      "bootstrap",    /* 13 */
      "done",         /* 14 */
      "strip_prefix", /* 15 */
      "add_prefix",   /* 16 */
      "add_suffix",   /* 17 */
      "regexwhere",   /* 18 */
      "restoreclient", /* 19 */
      "copies",        /* 20 */
      "comment",       /* 21 */
      "restorejob",    /* 22 */
      "replace",       /* 23 */
      "xxxxxxxxx",     /* 24 */
      "fdcalled",      /* 25 */
      "when",          /* 26 */
      "noautoparent",  /* 27 */
      NULL
   };

   rx->JobIds[0] = 0;

   for (i=1; i<ua->argc; i++) {       /* loop through arguments */
      bool found_kw = false;
      for (j=0; kw[j]; j++) {         /* loop through keywords */
         if (strcasecmp(kw[j], ua->argk[i]) == 0) {
            found_kw = true;
            break;
         }
      }

      if (!found_kw) {
         ua->error_msg(_("Unknown keyword: %s\n"), ua->argk[i]);
         return 0;
      }
      /* Found keyword in kw[] list, process it */
      switch (j) {
      case 0:                            /* jobid */
         if (!has_value(ua, i)) {
            return 0;
         }
         if (*rx->JobIds != 0) {
            pm_strcat(rx->JobIds, ",");
         }
         pm_strcat(rx->JobIds, ua->argv[i]);
         done = true;
         break;
      case 1:                            /* current */
         /*
          * Note, we add one second here just to include any job
          *  that may have finished within the current second,
          *  which happens a lot in scripting small jobs.
          */
         bstrutime(date, sizeof(date), now);
         have_date = true;
         break;
      case 2:                            /* before */
         if (have_date || !has_value(ua, i)) {
            return 0;
         }
         if (str_to_utime(ua->argv[i]) == 0) {
            ua->error_msg(_("Improper date format: %s\n"), ua->argv[i]);
            return 0;
         }
         bstrncpy(date, ua->argv[i], sizeof(date));
         have_date = true;
         break;
      case 3:                            /* file */
      case 4:                            /* dir */
         if (!has_value(ua, i)) {
            return 0;
         }
         if (!have_date) {
            bstrutime(date, sizeof(date), now);
         }
         if (!get_client_name(ua, rx)) {
            return 0;
         }
         pm_strcpy(ua->cmd, ua->argv[i]);
         insert_one_file_or_dir(ua, rx, date, j==4);
         return 2;
      case 5:                            /* select */
         if (!have_date) {
            bstrutime(date, sizeof(date), now);
         }
         if (!select_backups_before_date(ua, rx, date)) {
            return 0;
         }
         done = true;
         break;
      case 6:                            /* pool specified */
         if (!has_value(ua, i)) {
            return 0;
         }
         rx->pool = (POOL *)GetResWithName(R_POOL, ua->argv[i]);
         if (!rx->pool) {
            ua->error_msg(_("Error: Pool resource \"%s\" does not exist.\n"), ua->argv[i]);
            return 0;
         }
         if (!acl_access_ok(ua, Pool_ACL, ua->argv[i])) {
            rx->pool = NULL;
            ua->error_msg(_("Error: Pool resource \"%s\" access not allowed.\n"), ua->argv[i]);
            return 0;
         }
         break;
      case 7:                         /* all specified */
         rx->all = true;
         break;
      /*
       * All keywords 7 or greater are ignored or handled by a select prompt
       */
      default:
         break;
      }
   }

   if (!done) {
      ua->send_msg(_("\nFirst you select one or more JobIds that contain files\n"
                  "to be restored. You will be presented several methods\n"
                  "of specifying the JobIds. Then you will be allowed to\n"
                  "select which files from those JobIds are to be restored.\n\n"));
   }

   /* If choice not already made above, prompt */
   for ( ; !done; ) {
      char *fname;
      int len;
      bool gui_save;
      db_list_ctx jobids;

      start_prompt(ua, _("To select the JobIds, you have the following choices:\n"));
      for (int i=0; list[i]; i++) {
         add_prompt(ua, list[i]);
      }
      done = true;
      switch (do_prompt(ua, "", _("Select item: "), NULL, 0)) {
      case -1:                        /* error or cancel */
         return 0;
      case 0:                         /* list last 20 Jobs run */
         if (!acl_access_ok(ua, Command_ACL, NT_("sqlquery"), 8)) {
            ua->error_msg(_("SQL query not authorized.\n"));
            return 0;
         }
         gui_save = ua->jcr->gui;
         ua->jcr->gui = true;
         db_list_sql_query(ua->jcr, ua->db, uar_list_jobs, prtit, ua, 1, HORZ_LIST);
         ua->jcr->gui = gui_save;
         done = false;
         break;
      case 1:                         /* list where a file is saved */
         if (!get_client_name(ua, rx)) {
            return 0;
         }
         if (!get_cmd(ua, _("Enter Filename (no path):"))) {
            return 0;
         }
         len = strlen(ua->cmd);
         fname = (char *)malloc(len * 2 + 1);
         db_escape_string(ua->jcr, ua->db, fname, ua->cmd, len);
         Mmsg(rx->query, uar_file[db_get_type_index(ua->db)], rx->ClientName, fname);
         free(fname);
         gui_save = ua->jcr->gui;
         ua->jcr->gui = true;
         db_list_sql_query(ua->jcr, ua->db, rx->query, prtit, ua, 1, HORZ_LIST);
         ua->jcr->gui = gui_save;
         done = false;
         break;
      case 2:                         /* enter a list of JobIds */
         if (!get_cmd(ua, _("Enter JobId(s), comma separated, to restore: "))) {
            return 0;
         }
         pm_strcpy(rx->JobIds, ua->cmd);
         break;
      case 3:                         /* Enter an SQL list command */
         if (!acl_access_ok(ua, Command_ACL, NT_("sqlquery"), 8)) {
            ua->error_msg(_("SQL query not authorized.\n"));
            return 0;
         }
         if (!get_cmd(ua, _("Enter SQL list command: "))) {
            return 0;
         }
         gui_save = ua->jcr->gui;
         ua->jcr->gui = true;
         db_list_sql_query(ua->jcr, ua->db, ua->cmd, prtit, ua, 1, HORZ_LIST);
         ua->jcr->gui = gui_save;
         done = false;
         break;
      case 4:                         /* Select the most recent backups */
         if (!have_date) {
            bstrutime(date, sizeof(date), now);
         }
         if (!select_backups_before_date(ua, rx, date)) {
            return 0;
         }
         break;
      case 5:                         /* select backup at specified time */
         if (!have_date) {
            if (!get_date(ua, date, sizeof(date))) {
               return 0;
            }
         }
         if (!select_backups_before_date(ua, rx, date)) {
            return 0;
         }
         break;
      case 6:                         /* Enter files */
         if (!have_date) {
            bstrutime(date, sizeof(date), now);
         }
         if (!get_client_name(ua, rx)) {
            return 0;
         }
         ua->send_msg(_("Enter file names with paths, or < to enter a filename\n"
                        "containing a list of file names with paths, and terminate\n"
                        "them with a blank line.\n"));
         for ( ;; ) {
            if (!get_cmd(ua, _("Enter full filename: "))) {
               return 0;
            }
            len = strlen(ua->cmd);
            if (len == 0) {
               break;
            }
            insert_one_file_or_dir(ua, rx, date, false);
         }
         return 2;
       case 7:                        /* enter files backed up before specified time */
         if (!have_date) {
            if (!get_date(ua, date, sizeof(date))) {
               return 0;
            }
         }
         if (!get_client_name(ua, rx)) {
            return 0;
         }
         ua->send_msg(_("Enter file names with paths, or < to enter a filename\n"
                        "containing a list of file names with paths, and terminate\n"
                        "them with a blank line.\n"));
         for ( ;; ) {
            if (!get_cmd(ua, _("Enter full filename: "))) {
               return 0;
            }
            len = strlen(ua->cmd);
            if (len == 0) {
               break;
            }
            insert_one_file_or_dir(ua, rx, date, false);
         }
         return 2;

      case 8:                         /* Find JobIds for current backup */
         if (!have_date) {
            bstrutime(date, sizeof(date), now);
         }
         if (!select_backups_before_date(ua, rx, date)) {
            return 0;
         }
         done = false;
         break;

      case 9:                         /* Find JobIds for give date */
         if (!have_date) {
            if (!get_date(ua, date, sizeof(date))) {
               return 0;
            }
         }
         if (!select_backups_before_date(ua, rx, date)) {
            return 0;
         }
         done = false;
         break;

      case 10:                        /* Enter directories */
         if (*rx->JobIds != 0) {
            ua->send_msg(_("You have already selected the following JobIds: %s\n"),
               rx->JobIds);
         } else if (get_cmd(ua, _("Enter JobId(s), comma separated, to restore: "))) {
            if (*rx->JobIds != 0 && *ua->cmd) {
               pm_strcat(rx->JobIds, ",");
            }
            pm_strcat(rx->JobIds, ua->cmd);
         }
         if (*rx->JobIds == 0 || *rx->JobIds == '.') {
            *rx->JobIds = 0;
            return 0;                 /* nothing entered, return */
         }
         if (!have_date) {
            bstrutime(date, sizeof(date), now);
         }
         if (!get_client_name(ua, rx)) {
            return 0;
         }
         ua->send_msg(_("Enter full directory names or start the name\n"
                        "with a < to indicate it is a filename containing a list\n"
                        "of directories and terminate them with a blank line.\n"));
         for ( ;; ) {
            if (!get_cmd(ua, _("Enter directory name: "))) {
               return 0;
            }
            len = strlen(ua->cmd);
            if (len == 0) {
               break;
            }
            /* Add trailing slash to end of directory names */
            if (ua->cmd[0] != '<' && !IsPathSeparator(ua->cmd[len-1])) {
               strcat(ua->cmd, "/");
            }
            insert_one_file_or_dir(ua, rx, date, true);
         }
         return 2;

      case 11:                        /* Choose a jobid and select jobs */
         if (!get_cmd(ua, _("Enter JobId to get the state to restore: ")) ||
             !is_an_integer(ua->cmd))
         {
            return 0;
         }

         memset(&jr, 0, sizeof(JOB_DBR));
         jr.JobId = str_to_int64(ua->cmd);
         if (!db_get_job_record(ua->jcr, ua->db, &jr)) {
            ua->error_msg(_("Unable to get Job record for JobId=%s: ERR=%s\n"),
                          ua->cmd, db_strerror(ua->db));
            return 0;
         }
         ua->send_msg(_("Selecting jobs to build the Full state at %s\n"),
                      jr.cStartTime);
         jr.JobLevel = L_INCREMENTAL; /* Take Full+Diff+Incr */
         if (!db_get_accurate_jobids(ua->jcr, ua->db, &jr, &jobids)) {
            return 0;
         }
         pm_strcpy(rx->JobIds, jobids.list);
         Dmsg1(30, "Item 12: jobids = %s\n", rx->JobIds);
         break;
      case 12:                        /* Cancel or quit */
         return 0;
      }
   }

   memset(&jr, 0, sizeof(JOB_DBR));
   POOLMEM *JobIds = get_pool_memory(PM_FNAME);
   *JobIds = 0;
   rx->TotalFiles = 0;
   /*
    * Find total number of files to be restored, and filter the JobId
    *  list to contain only ones permitted by the ACL conditions.
    */
   for (p=rx->JobIds; ; ) {
      char ed1[50];
      int stat = get_next_jobid_from_list(&p, &JobId);
      if (stat < 0) {
         ua->error_msg(_("Invalid JobId in list.\n"));
         free_pool_memory(JobIds);
         return 0;
      }
      if (stat == 0) {
         break;
      }
      if (jr.JobId == JobId) {
         continue;                    /* duplicate of last JobId */
      }
      memset(&jr, 0, sizeof(JOB_DBR));
      jr.JobId = JobId;
      if (!db_get_job_record(ua->jcr, ua->db, &jr)) {
         ua->error_msg(_("Unable to get Job record for JobId=%s: ERR=%s\n"),
            edit_int64(JobId, ed1), db_strerror(ua->db));
         free_pool_memory(JobIds);
         return 0;
      }
      if (!acl_access_ok(ua, Job_ACL, jr.Name)) {
         ua->error_msg(_("Access to JobId=%s (Job \"%s\") not authorized. Not selected.\n"),
            edit_int64(JobId, ed1), jr.Name);
         continue;
      }
      if (*JobIds != 0) {
         pm_strcat(JobIds, ",");
      }
      pm_strcat(JobIds, edit_int64(JobId, ed1));
      rx->TotalFiles += jr.JobFiles;
   }
   pm_strcpy(rx->JobIds, JobIds);     /* Set ACL filtered list */
   free_pool_memory(JobIds);
   if (*rx->JobIds == 0) {
      ua->warning_msg(_("No Jobs selected.\n"));
      return 0;
   }

   if (strchr(rx->JobIds,',')) {
      ua->info_msg(_("You have selected the following JobIds: %s\n"), rx->JobIds);
   } else {
      ua->info_msg(_("You have selected the following JobId: %s\n"), rx->JobIds);
   }
   return 1;
}

/*
 * Get date from user
 */
static bool get_date(UAContext *ua, char *date, int date_len)
{
   ua->send_msg(_("The restored files will the most current backup\n"
                  "BEFORE the date you specify below.\n\n"));
   for ( ;; ) {
      if (!get_cmd(ua, _("Enter date as YYYY-MM-DD HH:MM:SS :"))) {
         return false;
      }
      if (str_to_utime(ua->cmd) != 0) {
         break;
      }
      ua->error_msg(_("Improper date format.\n"));
   }
   bstrncpy(date, ua->cmd, date_len);
   return true;
}

/*
 * Insert a single file, or read a list of files from a file
 */
static void insert_one_file_or_dir(UAContext *ua, RESTORE_CTX *rx, char *date, bool dir)
{
   FILE *ffd;
   char file[5000];
   char *p = ua->cmd;
   int line = 0;

   switch (*p) {
   case '<':
      p++;
      if ((ffd = bfopen(p, "rb")) == NULL) {
         berrno be;
         ua->error_msg(_("Cannot open file %s: ERR=%s\n"),
            p, be.bstrerror());
         break;
      }
      while (fgets(file, sizeof(file), ffd)) {
         line++;
         if (dir) {
            if (!insert_dir_into_findex_list(ua, rx, file, date)) {
               ua->error_msg(_("Error occurred on line %d of file \"%s\"\n"), line, p);
            }
         } else {
            if (!insert_file_into_findex_list(ua, rx, file, date)) {
               ua->error_msg(_("Error occurred on line %d of file \"%s\"\n"), line, p);
            }
         }
      }
      fclose(ffd);
      break;
   case '?':
      p++;
      insert_table_into_findex_list(ua, rx, p);
      break;
   default:
      if (dir) {
         insert_dir_into_findex_list(ua, rx, ua->cmd, date);
      } else {
         insert_file_into_findex_list(ua, rx, ua->cmd, date);
      }
      break;
   }
}

/*
 * For a given file (path+filename), split into path and file, then
 *   lookup the most recent backup in the catalog to get the JobId
 *   and FileIndex, then insert them into the findex list.
 */
static bool insert_file_into_findex_list(UAContext *ua, RESTORE_CTX *rx, char *file,
                                        char *date)
{
   strip_trailing_newline(file);
   split_path_and_filename(ua, rx, file);
   if (*rx->JobIds == 0) {
      Mmsg(rx->query, uar_jobid_fileindex, date, rx->path, rx->fname,
           rx->ClientName);
   } else {
      Mmsg(rx->query, uar_jobids_fileindex, rx->JobIds, date,
           rx->path, rx->fname, rx->ClientName);
      /*
       * Note: we have just edited the JobIds into the query, so
       *  we need to clear JobIds, or they will be added
       *  back into JobIds with the query below, and then
       *  restored twice.  Fixes bug #2212.
       */
      rx->JobIds[0] = 0;
   }
   rx->found = false;
   /* Find and insert jobid and File Index */
   if (!db_sql_query(ua->db, rx->query, jobid_fileindex_handler, (void *)rx)) {
      ua->error_msg(_("Query failed: %s. ERR=%s\n"),
         rx->query, db_strerror(ua->db));
   }
   if (!rx->found) {
      ua->error_msg(_("No database record found for: %s\n"), file);
//    ua->error_msg("Query=%s\n", rx->query);
      return true;
   }
   return true;
}

/*
 * For a given path lookup the most recent backup in the catalog
 * to get the JobId and FileIndexes of all files in that directory.
 */
static bool insert_dir_into_findex_list(UAContext *ua, RESTORE_CTX *rx, char *dir,
                                        char *date)
{
   strip_trailing_junk(dir);
   if (*rx->JobIds == 0) {
      ua->error_msg(_("No JobId specified cannot continue.\n"));
      return false;
   } else {
      Mmsg(rx->query, uar_jobid_fileindex_from_dir[db_get_type_index(ua->db)], rx->JobIds, dir, rx->ClientName);
   }
   rx->found = false;
   /* Find and insert jobid and File Index */
   if (!db_sql_query(ua->db, rx->query, jobid_fileindex_handler, (void *)rx)) {
      ua->error_msg(_("Query failed: %s. ERR=%s\n"),
         rx->query, db_strerror(ua->db));
   }
   if (!rx->found) {
      ua->error_msg(_("No database record found for: %s\n"), dir);
      return true;
   }
   return true;
}

/*
 * Get the JobId and FileIndexes of all files in the specified table
 */
bool insert_table_into_findex_list(UAContext *ua, RESTORE_CTX *rx, char *table)
{
   strip_trailing_junk(table);
   Mmsg(rx->query, uar_jobid_fileindex_from_table, table);

   rx->found = false;
   /* Find and insert jobid and File Index */
   if (!db_sql_query(ua->db, rx->query, jobid_fileindex_handler, (void *)rx)) {
      ua->error_msg(_("Query failed: %s. ERR=%s\n"),
         rx->query, db_strerror(ua->db));
   }
   if (!rx->found) {
      ua->error_msg(_("No table found: %s\n"), table);
      return true;
   }
   return true;
}

static void split_path_and_filename(UAContext *ua, RESTORE_CTX *rx, char *name)
{
   char *p, *f;

   /* Find path without the filename.
    * I.e. everything after the last / is a "filename".
    * OK, maybe it is a directory name, but we treat it like
    * a filename. If we don't find a / then the whole name
    * must be a path name (e.g. c:).
    */
   for (p=f=name; *p; p++) {
      if (IsPathSeparator(*p)) {
         f = p;                       /* set pos of last slash */
      }
   }
   if (IsPathSeparator(*f)) {         /* did we find a slash? */
      f++;                            /* yes, point to filename */
   } else {                           /* no, whole thing must be path name */
      f = p;
   }

   /* If filename doesn't exist (i.e. root directory), we
    * simply create a blank name consisting of a single
    * space. This makes handling zero length filenames
    * easier.
    */
   rx->fnl = p - f;
   if (rx->fnl > 0) {
      rx->fname = check_pool_memory_size(rx->fname, 2*(rx->fnl)+1);
      db_escape_string(ua->jcr, ua->db, rx->fname, f, rx->fnl);
   } else {
      rx->fname[0] = 0;
      rx->fnl = 0;
   }

   rx->pnl = f - name;
   if (rx->pnl > 0) {
      rx->path = check_pool_memory_size(rx->path, 2*(rx->pnl)+1);
      db_escape_string(ua->jcr, ua->db, rx->path, name, rx->pnl);
   } else {
      rx->path[0] = 0;
      rx->pnl = 0;
   }

   Dmsg2(100, "split path=%s file=%s\n", rx->path, rx->fname);
}

static bool can_restore_all_files(UAContext *ua)
{
   alist *lst;
   if (ua->cons) {
      lst = ua->cons->ACL_lists[Directory_ACL];
      /* ACL not defined, or the first entry is not *all* */
      /* TODO: See if we search for *all* in all the list */
      if (!lst || strcasecmp((char*)lst->get(0), "*all*") != 0) {
         return false;
      }
      if (!lst || strcasecmp((char *)lst->get(0), "*all*") != 0) {
         return false;
      }
   }
   return true;
}

static bool ask_for_fileregex(UAContext *ua, RESTORE_CTX *rx)
{
   bool can_restore=can_restore_all_files(ua);

   if (can_restore && find_arg(ua, NT_("all")) >= 0) {  /* if user enters all on command line */
      return true;                       /* select everything */
   }

   ua->send_msg(_("\n\nFor one or more of the JobIds selected, no files were found,\n"
                 "so file selection is not possible.\n"
                 "Most likely your retention policy pruned the files.\n"));

   if (!can_restore) {
      ua->error_msg(_("\nThe current Console has UserId or Directory restrictions. "
                      "The full restore is not allowed.\n"));
      return false;
   }

   if (get_yesno(ua, _("\nDo you want to restore all the files? (yes|no): "))) {
      if (ua->pint32_val == 1)
         return true;
      while (get_cmd(ua, _("\nRegexp matching files to restore? (empty to abort): "))) {
         if (ua->cmd[0] == '\0') {
            break;
         } else {
            regex_t *fileregex_re = NULL;
            int rc;
            char errmsg[500] = "";

            fileregex_re = (regex_t *)bmalloc(sizeof(regex_t));
            rc = regcomp(fileregex_re, ua->cmd, REG_EXTENDED|REG_NOSUB);
            if (rc != 0) {
               regerror(rc, fileregex_re, errmsg, sizeof(errmsg));
            }
            regfree(fileregex_re);
            free(fileregex_re);
            if (*errmsg) {
               ua->send_msg(_("Regex compile error: %s\n"), errmsg);
            } else {
               rx->fileregex = bstrdup(ua->cmd);
               return true;
            }
         }
      }
   }
   return false;
}

/* Walk on the delta_list of a TREE_NODE item and insert all parts
 * TODO: Optimize for bootstrap creation, remove recursion
 * 6 -> 5 -> 4 -> 3 -> 2 -> 1 -> 0
 * should insert as
 * 0, 1, 2, 3, 4, 5, 6
 */
static void add_delta_list_findex(RESTORE_CTX *rx, struct delta_list *lst)
{
   if (lst == NULL) {
      return;
   }
   if (lst->next) {
      add_delta_list_findex(rx, lst->next);
   }
   add_findex(rx->bsr_list, lst->JobId, lst->FileIndex);
}

/*
 * This is a list of all the files (components) that the
 *  user has requested for restore. It is requested by
 *  the plugin (for now hard coded only for VSS).
 *  In the future, this will be requested by a RestoreObject
 *  and the plugin name will be sent to the FD.
 */
static bool write_component_file(UAContext *ua, RESTORE_CTX *rx, char *fname)
{
   int fd;
   if (!rx->component_fd) {
      Mmsg(rx->component_fname, "%s/%s.restore.sel.XXXXXX", working_directory, my_name);
      fd = mkstemp(rx->component_fname);
      if (fd < 0) {
         berrno be;
         ua->error_msg(_("Unable to create component file %s. ERR=%s\n"),
            rx->component_fname, be.bstrerror());
         return false;
      }
      rx->component_fd = fdopen(fd, "w+");
      if (!rx->component_fd) {
         berrno be;
         ua->error_msg(_("Unable to fdopen component file %s. ERR=%s\n"),
            rx->component_fname, be.bstrerror());
         return false;
      }
   }
   fprintf(rx->component_fd, "%s\n", fname);
   if (ferror(rx->component_fd)) {
      ua->error_msg(_("Error writing component file.\n"));
     fclose(rx->component_fd);
     unlink(rx->component_fname);
     rx->component_fd = NULL;
     return false;
   }
   return true;
}

static bool build_directory_tree(UAContext *ua, RESTORE_CTX *rx)
{
   TREE_CTX tree;
   JobId_t JobId, last_JobId;
   char *p;
   bool OK = true;
   char ed1[50];

   memset(&tree, 0, sizeof(TREE_CTX));
   /*
    * Build the directory tree containing JobIds user selected
    */
   tree.root = new_tree(rx->TotalFiles);
   tree.ua = ua;
   tree.all = rx->all;
   tree.hardlinks_in_mem = rx->hardlinks_in_mem;
   tree.no_auto_parent = rx->no_auto_parent;
   last_JobId = 0;
   tree.last_dir_acl = NULL;
   /*
    * For display purposes, the same JobId, with different volumes may
    * appear more than once, however, we only insert it once.
    */
   p = rx->JobIds;
   tree.FileEstimate = 0;
   if (get_next_jobid_from_list(&p, &JobId) > 0) {
      /* Use first JobId as estimate of the number of files to restore */
      Mmsg(rx->query, uar_count_files, edit_int64(JobId, ed1));
      if (!db_sql_query(ua->db, rx->query, restore_count_handler, (void *)rx)) {
         ua->error_msg("%s\n", db_strerror(ua->db));
      }
      if (rx->found) {
         /* Add about 25% more than this job for over estimate */
         tree.FileEstimate = rx->JobId + (rx->JobId >> 2);
         tree.DeltaCount = rx->JobId/50; /* print 50 ticks */
      }
   }

   ua->info_msg(_("\nBuilding directory tree for JobId(s) %s ...  "),
                rx->JobIds);

#define new_get_file_list
#ifdef new_get_file_list
   if (!db_get_file_list(ua->jcr, ua->db,
                         rx->JobIds, DBL_USE_DELTA,
                         insert_tree_handler, (void *)&tree))
   {
      ua->error_msg("%s", db_strerror(ua->db));
   }
   if (*rx->BaseJobIds) {
      pm_strcat(rx->JobIds, ",");
      pm_strcat(rx->JobIds, rx->BaseJobIds);
   }
#else
   for (p=rx->JobIds; get_next_jobid_from_list(&p, &JobId) > 0; ) {
      char ed1[50];

      if (JobId == last_JobId) {
         continue;                    /* eliminate duplicate JobIds */
      }
      last_JobId = JobId;
      /*
       * Find files for this JobId and insert them in the tree
       */
      Mmsg(rx->query, uar_sel_files, edit_int64(JobId, ed1));
      if (!db_sql_query(ua->db, rx->query, insert_tree_handler, (void *)&tree)) {
         ua->error_msg("%s", db_strerror(ua->db));
      }
   }
#endif
   /*
    * At this point, the tree is built, so we can garbage collect
    * any memory released by the SQL engine that RedHat has
    * not returned to the OS :-(
    */
    garbage_collect_memory();

   /*
    * Look at the first JobId on the list (presumably the oldest) and
    *  if it is marked purged, don't do the manual selection because
    *  the Job was pruned, so the tree is incomplete.
    */
   if (tree.FileCount != 0) {
      /* Find out if any Job is purged */
      Mmsg(rx->query, "SELECT SUM(PurgedFiles) FROM Job WHERE JobId IN (%s)", rx->JobIds);
      if (!db_sql_query(ua->db, rx->query, restore_count_handler, (void *)rx)) {
         ua->error_msg("%s\n", db_strerror(ua->db));
      }
      /* rx->JobId is the PurgedFiles flag */
      if (rx->found && rx->JobId > 0) {
         tree.FileCount = 0;           /* set count to zero, no tree selection */
      }
   }
   if (tree.FileCount == 0) {
      OK = ask_for_fileregex(ua, rx);
      if (OK) {
         last_JobId = 0;
         for (p=rx->JobIds; get_next_jobid_from_list(&p, &JobId) > 0; ) {
             if (JobId == last_JobId) {
                continue;                    /* eliminate duplicate JobIds */
             }
             add_findex_all(rx->bsr_list, JobId, rx->fileregex);
         }
      }
   } else {
      char ec1[50];
      if (tree.all) {
         ua->info_msg(_("\n%s files inserted into the tree and marked for extraction.\n"),
                      edit_uint64_with_commas(tree.FileCount, ec1));
      } else {
         ua->info_msg(_("\n%s files inserted into the tree.\n"),
                      edit_uint64_with_commas(tree.FileCount, ec1));
      }

      if (find_arg(ua, NT_("done")) < 0) {
         /* Let the user interact in selecting which files to restore */
         OK = user_select_files_from_tree(&tree);
      }

      /*
       * Walk down through the tree finding all files marked to be
       *  extracted making a bootstrap file.
       */
      if (OK) {
         char cwd[2000];
         for (TREE_NODE *node=first_tree_node(tree.root); node; node=next_tree_node(node)) {
            Dmsg2(400, "FI=%d node=0x%x\n", node->FileIndex, node);
            if (node->extract || node->extract_dir) {
               Dmsg3(400, "JobId=%lld type=%d FI=%d\n", (uint64_t)node->JobId, node->type, node->FileIndex);
               /* TODO: optimize bsr insertion when jobid are non sorted */
               add_delta_list_findex(rx, node->delta_list);
               add_findex(rx->bsr_list, node->JobId, node->FileIndex);
               /*
                * Special VSS plugin code to return selected
                *   components. For the moment, it is hard coded
                *   for the VSS plugin.
                */
               if (fnmatch(":component_info_*", node->fname, 0) == 0) {
                  tree_getpath(node, cwd, sizeof(cwd));
                  if (!write_component_file(ua, rx, cwd)) {
                     OK = false;
                     break;
                  }
               }
               if (node->extract && node->type != TN_NEWDIR) {
                  rx->selected_files++;  /* count only saved files */
               }
            }
         }
      }
   }
   if (tree.uid_acl) {
      delete tree.uid_acl;
      delete tree.gid_acl;
      delete tree.dir_acl;
   }
   free_tree(tree.root);              /* free the directory tree */
   return OK;
}


/*
 * This routine is used to get the current backup or a backup
 *   before the specified date.
 */
static bool select_backups_before_date(UAContext *ua, RESTORE_CTX *rx, char *date)
{
   bool ok = false;
   FILESET_DBR fsr;
   CLIENT_DBR cr;
   char fileset_name[MAX_NAME_LENGTH];
   char ed1[50], ed2[50];
   char pool_select[MAX_NAME_LENGTH];
   int i;

   /* Create temp tables */
  db_sql_query(ua->db, uar_del_temp, NULL, NULL);
  db_sql_query(ua->db, uar_del_temp1, NULL, NULL);
   if (!db_sql_query(ua->db, uar_create_temp[db_get_type_index(ua->db)], NULL, NULL)) {
      ua->error_msg("%s\n", db_strerror(ua->db));
   }
   if (!db_sql_query(ua->db, uar_create_temp1[db_get_type_index(ua->db)], NULL, NULL)) {
      ua->error_msg("%s\n", db_strerror(ua->db));
   }
   /*
    * Select Client from the Catalog
    */
   memset(&cr, 0, sizeof(cr));
   if (!get_client_dbr(ua, &cr, JT_BACKUP_RESTORE)) {
      goto bail_out;
   }
   bstrncpy(rx->ClientName, cr.Name, sizeof(rx->ClientName));

   /*
    * Get FileSet
    */
   memset(&fsr, 0, sizeof(fsr));
   i = find_arg_with_value(ua, "FileSet");

   if (i >= 0 && is_name_valid(ua->argv[i], &ua->errmsg)) {
      bstrncpy(fsr.FileSet, ua->argv[i], sizeof(fsr.FileSet));
      if (!db_get_fileset_record(ua->jcr, ua->db, &fsr)) {
         ua->error_msg(_("Error getting FileSet \"%s\": ERR=%s\n"), fsr.FileSet,
            db_strerror(ua->db));
         i = -1;
      }
   } else if (i >= 0) {         /* name is invalid */
      ua->error_msg(_("FileSet argument: %s\n"), ua->errmsg);
   }

   if (i < 0) {                       /* fileset not found */
      edit_int64(cr.ClientId, ed1);
      Mmsg(rx->query, uar_sel_fileset, ed1, ed1);
      start_prompt(ua, _("The defined FileSet resources are:\n"));
      if (!db_sql_query(ua->db, rx->query, fileset_handler, (void *)ua)) {
         ua->error_msg("%s\n", db_strerror(ua->db));
      }
      if (do_prompt(ua, _("FileSet"), _("Select FileSet resource"),
                 fileset_name, sizeof(fileset_name)) < 0) {
         ua->error_msg(_("No FileSet found for client \"%s\".\n"), cr.Name);
         goto bail_out;
      }

      bstrncpy(fsr.FileSet, fileset_name, sizeof(fsr.FileSet));
      if (!db_get_fileset_record(ua->jcr, ua->db, &fsr)) {
         ua->warning_msg(_("Error getting FileSet record: %s\n"), db_strerror(ua->db));
         ua->send_msg(_("This probably means you modified the FileSet.\n"
                     "Continuing anyway.\n"));
      }
   }

   /* If Pool specified, add PoolId specification */
   pool_select[0] = 0;
   if (rx->pool) {
      POOL_DBR pr;
      memset(&pr, 0, sizeof(pr));
      bstrncpy(pr.Name, rx->pool->name(), sizeof(pr.Name));
      if (db_get_pool_record(ua->jcr, ua->db, &pr)) {
         bsnprintf(pool_select, sizeof(pool_select), "AND Media.PoolId=%s ",
            edit_int64(pr.PoolId, ed1));
      } else {
         ua->warning_msg(_("Pool \"%s\" not found, using any pool.\n"), pr.Name);
      }
   }

   /* Find JobId of last Full backup for this client, fileset */
   edit_int64(cr.ClientId, ed1);
   Mmsg(rx->query, uar_last_full, ed1, ed1, date, fsr.FileSet,
         pool_select);
   if (!db_sql_query(ua->db, rx->query, NULL, NULL)) {
      ua->error_msg("%s\n", db_strerror(ua->db));
      goto bail_out;
   }

   /* Find all Volumes used by that JobId */
   if (!db_sql_query(ua->db, uar_full, NULL, NULL)) {
      ua->error_msg("%s\n", db_strerror(ua->db));
      goto bail_out;
   }

   /* Note, this is needed because I don't seem to get the callback
    * from the call just above.
    */
   rx->JobTDate = 0;
   if (!db_sql_query(ua->db, uar_sel_all_temp1, last_full_handler, (void *)rx)) {
      ua->warning_msg("%s\n", db_strerror(ua->db));
   }
   if (rx->JobTDate == 0) {
      ua->error_msg(_("No Full backup before %s found.\n"), date);
      goto bail_out;
   }

   /* Now find most recent Differental Job after Full save, if any */
   Mmsg(rx->query, uar_dif, edit_uint64(rx->JobTDate, ed1), date,
        edit_int64(cr.ClientId, ed2), fsr.FileSet, pool_select);
   if (!db_sql_query(ua->db, rx->query, NULL, NULL)) {
      ua->warning_msg("%s\n", db_strerror(ua->db));
   }
   /* Now update JobTDate to look into Differental, if any */
   rx->JobTDate = 0;
   if (!db_sql_query(ua->db, uar_sel_all_temp, last_full_handler, (void *)rx)) {
      ua->warning_msg("%s\n", db_strerror(ua->db));
   }
   if (rx->JobTDate == 0) {
      ua->error_msg(_("No Full backup before %s found.\n"), date);
      goto bail_out;
   }

   /* Now find all Incremental Jobs after Full/dif save */
   Mmsg(rx->query, uar_inc, edit_uint64(rx->JobTDate, ed1), date,
        edit_int64(cr.ClientId, ed2), fsr.FileSet, pool_select);
   if (!db_sql_query(ua->db, rx->query, NULL, NULL)) {
      ua->warning_msg("%s\n", db_strerror(ua->db));
   }

   /* Get the JobIds from that list */
   rx->last_jobid[0] = rx->JobIds[0] = 0;

   if (!db_sql_query(ua->db, uar_sel_jobid_temp, jobid_handler, (void *)rx)) {
      ua->warning_msg("%s\n", db_strerror(ua->db));
   }

   if (rx->JobIds[0] != 0) {
      if (find_arg(ua, NT_("copies")) > 0) {
         /* Display a list of all copies */
         db_list_copies_records(ua->jcr, ua->db, 0, rx->JobIds,
                                prtit, ua, HORZ_LIST);
      }
      /* Display a list of Jobs selected for this restore */
      db_list_sql_query(ua->jcr, ua->db, uar_list_temp, prtit, ua, 1,HORZ_LIST);
      ok = true;

   } else {
      ua->warning_msg(_("No jobs found.\n"));
   }

bail_out:
  db_sql_query(ua->db, uar_del_temp, NULL, NULL);
  db_sql_query(ua->db, uar_del_temp1, NULL, NULL);
   return ok;
}

static int restore_count_handler(void *ctx, int num_fields, char **row)
{
   RESTORE_CTX *rx = (RESTORE_CTX *)ctx;
   rx->JobId = str_to_int64(row[0]);
   rx->found = true;
   return 0;
}

/*
 * Callback handler to get JobId and FileIndex for files
 *   can insert more than one depending on the caller.
 */
static int jobid_fileindex_handler(void *ctx, int num_fields, char **row)
{
   RESTORE_CTX *rx = (RESTORE_CTX *)ctx;
   JobId_t JobId = str_to_int64(row[0]);

   Dmsg3(200, "JobId=%s JobIds=%s FileIndex=%s\n", row[0], rx->JobIds, row[1]);

   /* New JobId, add it to JobIds
    * The list is sorted by JobId, so we need a cache for the previous value
    *
    * It will permit to find restore objects to send during the restore
    */
   if (rx->JobId != JobId) {
      if (*rx->JobIds) {
         pm_strcat(rx->JobIds, ",");
      }
      pm_strcat(rx->JobIds, row[0]);
      rx->JobId = JobId;
   }

   add_findex(rx->bsr_list, rx->JobId, str_to_int64(row[1]));
   rx->found = true;
   rx->selected_files++;
   return 0;
}

/*
 * Callback handler make list of JobIds
 */
static int jobid_handler(void *ctx, int num_fields, char **row)
{
   RESTORE_CTX *rx = (RESTORE_CTX *)ctx;

   if (strcmp(rx->last_jobid, row[0]) == 0) {
      return 0;                       /* duplicate id */
   }
   bstrncpy(rx->last_jobid, row[0], sizeof(rx->last_jobid));
   if (rx->JobIds[0] != 0) {
      pm_strcat(rx->JobIds, ",");
   }
   pm_strcat(rx->JobIds, row[0]);
   return 0;
}


/*
 * Callback handler to pickup last Full backup JobTDate
 */
static int last_full_handler(void *ctx, int num_fields, char **row)
{
   RESTORE_CTX *rx = (RESTORE_CTX *)ctx;

   rx->JobTDate = str_to_int64(row[1]);
   return 0;
}

/*
 * Callback handler build FileSet name prompt list
 */
static int fileset_handler(void *ctx, int num_fields, char **row)
{
   /* row[0] = FileSet (name) */
   if (row[0]) {
      add_prompt((UAContext *)ctx, row[0]);
   }
   return 0;
}

/*
 * Free names in the list
 */
static void free_name_list(NAME_LIST *name_list)
{
   for (int i=0; i < name_list->num_ids; i++) {
      free(name_list->name[i]);
   }
   bfree_and_null(name_list->name);
   name_list->max_ids = 0;
   name_list->num_ids = 0;
}

void find_storage_resource(UAContext *ua, RESTORE_CTX &rx, char *Storage, char *MediaType)
{
   STORE *store;

   if (rx.store) {
      Dmsg1(200, "Already have store=%s\n", rx.store->name());
      return;
   }
   /*
    * Try looking up Storage by name
    */
   LockRes();
   foreach_res(store, R_STORAGE) {
      if (strcmp(Storage, store->name()) == 0) {
         if (acl_access_ok(ua, Storage_ACL, store->name())) {
            rx.store = store;
         }
         break;
      }
   }
   UnlockRes();

   if (rx.store) {
      /* Check if an explicit storage resource is given */
      store = NULL;
      int i = find_arg_with_value(ua, "storage");
      if (i > 0) {
         store = (STORE *)GetResWithName(R_STORAGE, ua->argv[i]);
         if (store && !acl_access_ok(ua, Storage_ACL, store->name())) {
            store = NULL;
         }
      }
      if (store && (store != rx.store)) {
         ua->info_msg(_("\nWarning Storage is overridden by \"%s\" on the command line.\n"),
            store->name());
         rx.store = store;
         bstrncpy(rx.RestoreMediaType, MediaType, sizeof(rx.RestoreMediaType));
         if (strcmp(MediaType, store->media_type) != 0) {
            ua->info_msg(_("This may not work because of two different MediaTypes:\n"
               "  Storage MediaType=\"%s\"\n"
               "  Volume  MediaType=\"%s\".\n\n"),
               store->media_type, MediaType);
         }
         Dmsg2(200, "Set store=%s MediaType=%s\n", rx.store->name(), rx.RestoreMediaType);
      }
      return;
   }

   /* If no storage resource, try to find one from MediaType */
   if (!rx.store) {
      LockRes();
      foreach_res(store, R_STORAGE) {
         if (strcmp(MediaType, store->media_type) == 0) {
            if (acl_access_ok(ua, Storage_ACL, store->name())) {
               rx.store = store;
               Dmsg1(200, "Set store=%s\n", rx.store->name());
               if (Storage == NULL || Storage[0] == 0) {
                  ua->warning_msg(_("Using Storage \"%s\" from MediaType \"%s\".\n"),
                     store->name(), MediaType);
               } else {
                  ua->warning_msg(_("Storage \"%s\" not found, using Storage \"%s\" from MediaType \"%s\".\n"),
                     Storage, store->name(), MediaType);
               }
            }
            UnlockRes();
            return;
         }
      }
      UnlockRes();
      ua->warning_msg(_("\nUnable to find Storage resource for\n"
         "MediaType \"%s\", needed by the Jobs you selected.\n"), MediaType);
   }

   /* Take command line arg, or ask user if none */
   rx.store = get_storage_resource(ua, false /* don't use default */);
   if (rx.store) {
      Dmsg1(200, "Set store=%s\n", rx.store->name());
   }

}
