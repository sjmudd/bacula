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
 *
 *  Program to check a Bacula database for consistency and to
 *   make repairs
 *
 *   Kern E. Sibbald, August 2002
 *
 */

#include "bacula.h"
#include "cats/cats.h"
#include "lib/runscript.h"
#include "dird/dird_conf.h"

extern bool parse_dir_config(CONFIG *config, const char *configfile, int exit_code);

typedef struct s_id_ctx {
   int64_t *Id;                       /* ids to be modified */
   int num_ids;                       /* ids stored */
   int max_ids;                       /* size of array */
   int num_del;                       /* number deleted */
   int tot_ids;                       /* total to process */
} ID_LIST;

typedef struct s_name_ctx {
   char **name;                       /* list of names */
   int num_ids;                       /* ids stored */
   int max_ids;                       /* size of array */
   int num_del;                       /* number deleted */
   int tot_ids;                       /* total to process */
} NAME_LIST;

/* Global variables */ 

static bool fix = false;
static bool batch = false;
static BDB *db;
static ID_LIST id_list;
static NAME_LIST name_list;
static char buf[20000];
static bool quit = false;
static CONFIG *config;
static const char *idx_tmp_name; 

#define MAX_ID_LIST_LEN 10000000

/* Forward referenced functions */ 
static int make_id_list(const char *query, ID_LIST *id_list);
static int delete_id_list(const char *query, ID_LIST *id_list);
static int make_name_list(const char *query, NAME_LIST *name_list);
static void print_name_list(NAME_LIST *name_list);
static void free_name_list(NAME_LIST *name_list);
static char *get_cmd(const char *prompt);
static void eliminate_duplicate_filenames();
static void eliminate_duplicate_paths();
static void eliminate_orphaned_jobmedia_records();
static void eliminate_orphaned_file_records();
static void eliminate_orphaned_path_records();
static void eliminate_orphaned_filename_records();
static void eliminate_orphaned_fileset_records();
static void eliminate_orphaned_client_records();
static void eliminate_orphaned_job_records();
static void eliminate_admin_records();
static void eliminate_restore_records();
static void eliminate_verify_records();
static void repair_bad_paths();
static void repair_bad_filenames();
static void do_interactive_mode();
static bool yes_no(const char *prompt);
static bool check_idx(const char *col_name);
static bool create_tmp_idx(const char *idx_name, const char *table_name, 
               const char *col_name);
static bool drop_tmp_idx(const char *idx_name, const char *table_name);
static int check_idx_handler(void *ctx, int num_fields, char **row);

static void usage()
{
   fprintf(stderr,
PROG_COPYRIGHT
"\n%sVersion: %s (%s)\n\n"
"Usage: dbcheck [-c config ] [-B] [-C catalog name] [-d debug_level] <working-directory> <bacula-database> <user> <password> [<dbhost>] [<dbport>] [<dbport>] [<dbsslmode>] [<dbsslkey>] [<dbsslcert>] [<dbsslca>]\n"
"       -b              batch mode\n"
"       -C              catalog name in the director conf file\n"
"       -c              Director conf filename\n"
"       -B              print catalog configuration and exit\n"
"       -d <nn>         set debug level to <nn>\n"
"       -dt             print a timestamp in debug output\n"
"       -f              fix inconsistencies\n"
"       -v              verbose\n"
"       -?              print this message\n"
"\n", 2002, "", VERSION, BDATE);

   exit(1);
}

int main (int argc, char *argv[])
{
   int ch;
   const char *user, *password, *db_name, *dbhost;
   const char *dbsslmode = NULL, *dbsslkey = NULL, *dbsslcert = NULL, *dbsslca = NULL;
   const char *dbsslcapath = NULL, *dbsslcipher = NULL;
   int dbport = 0;
   bool print_catalog=false;
   char *configfile = NULL;
   char *catalogname = NULL;
   char *endptr;

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");
   lmgr_init_thread();

   my_name_is(argc, argv, "dbcheck");
   init_msg(NULL, NULL);             /* setup message handler */

   memset(&id_list, 0, sizeof(id_list));
   memset(&name_list, 0, sizeof(name_list));

   while ((ch = getopt(argc, argv, "bc:C:d:fvB?")) != -1) { 
      switch (ch) {
      case 'B':
         print_catalog = true;     /* get catalog information from config */
         break;
      case 'b':                    /* batch */
         batch = true;
         break;
      case 'C':                    /* CatalogName */
          catalogname = optarg;
         break;
      case 'c':                    /* configfile */
          configfile = optarg;
         break;
      case 'd':                    /* debug level */
         if (*optarg == 't') {
            dbg_timestamp = true;
         } else {
            debug_level = atoi(optarg);
            if (debug_level <= 0) {
               debug_level = 1;
            }
         }
         break;
      case 'f':                    /* fix inconsistencies */
         fix = true;
         break;
      case 'v':
         verbose++;
         break;
      case '?':
      default:
         usage();
      }
   }
   argc -= optind;
   argv += optind;

   OSDependentInit();

   if (configfile) {
      CAT *catalog = NULL;
      int found = 0;
      if (argc > 0) {
         Pmsg0(0, _("Warning skipping the additional parameters for working directory/dbname/user/password/host.\n"));
      }
      config = New(CONFIG());
      parse_dir_config(config, configfile, M_ERROR_TERM);
      LockRes();
      foreach_res(catalog, R_CATALOG) {
         if (catalogname && !strcmp(catalog->hdr.name, catalogname)) {
            ++found;
            break;
         } else if (!catalogname) { // stop on first if no catalogname is given
           ++found;
           break;
         }
      }
      UnlockRes();
      if (!found) {
         if (catalogname) {
            Pmsg2(0, _("Error can not find the Catalog name[%s] in the given config file [%s]\n"), catalogname, configfile);
         } else {
            Pmsg1(0, _("Error there is no Catalog section in the given config file [%s]\n"), configfile);
         }
         exit(1);
      } else {
         DIRRES *director;
         LockRes();
         director = (DIRRES *)GetNextRes(R_DIRECTOR, NULL);
         UnlockRes();
         if (!director) {
            Pmsg0(0, _("Error no Director resource defined.\n"));
            exit(1);
         }
         set_working_directory(director->working_directory);

         /* Print catalog information and exit (-B) */
         if (print_catalog) {

            POOLMEM *catalog_details = get_pool_memory(PM_MESSAGE);
            db = db_init_database(NULL, catalog->db_driver, catalog->db_name, catalog->db_user,
                    catalog->db_password, catalog->db_address,
                    catalog->db_port, catalog->db_socket,
                    catalog->db_ssl_mode,
                    catalog->db_ssl_key, catalog->db_ssl_cert, 
                    catalog->db_ssl_ca,
                    catalog->db_ssl_capath, catalog->db_ssl_cipher,
                    catalog->mult_db_connections,
                    catalog->disable_batch_insert);
            if (db) {
               printf("%sdb_type=%s\nworking_dir=%s\n", catalog->display(catalog_details),
                  db_get_engine_name(db), working_directory);
               db_close_database(NULL, db);
            }
            free_pool_memory(catalog_details);
            exit(0);
         }

         db_name = catalog->db_name;
         user = catalog->db_user;
         password = catalog->db_password;
         dbhost = catalog->db_address;
         if (dbhost && dbhost[0] == 0) {
            dbhost = NULL;
         }
         dbport = catalog->db_port;
         dbsslmode = catalog->db_ssl_mode;
         dbsslkey = catalog->db_ssl_key;
         dbsslcert = catalog->db_ssl_cert;
         dbsslca = catalog->db_ssl_ca;
         dbsslcapath = catalog->db_ssl_capath;
         dbsslcipher = catalog->db_ssl_cipher;
      }
   } else {
      if (argc > 10) {
         Pmsg0(0, _("Wrong number of arguments.\n"));
         usage();
      }

      if (argc < 1) {
         Pmsg0(0, _("Working directory not supplied.\n"));
         usage();
      }

      /* This is needed by SQLite to find the db */
      working_directory = argv[0];
      db_name = "bacula";
      user = db_name;
      password = "";
      dbhost = NULL;

      if (argc >= 2) {
         db_name = argv[1];
         user = db_name;
         if (argc >= 3) {
            user = argv[2];
            if (argc >= 4) {
               password = argv[3];
               if (argc >= 5) {
                  dbhost = argv[4];
                  if (argc >= 6) {
                     errno = 0;
                     dbport = strtol(argv[5], &endptr, 10);
                     if (*endptr != '\0') {
                        Pmsg0(0, _("Database port must be a numeric value.\n"));
                        exit(1);
                     } else if (errno == ERANGE) {
                        Pmsg0(0, _("Database port must be a int value.\n"));
                        exit(1);
                     }
                     if (argc >= 7) {
                        dbsslmode = argv[6];
                        if (argc >= 8) {
                           dbsslkey = argv[7];
                           dbsslcert = argv[8];
                           if (argc == 10) {
                              dbsslca = argv[9];
                           } /* if (argc == 10) */
                        } /* if (argc >= 8) */
                     } /* if (argc >= 7) */
                  } /* if (argc >= 6) */
               } /* if (argc >= 5) */
            } /* if (argc >= 4) */
         } /* if (argc >= 3) */
      } /* if (argc >= 2) */
   }

   /* Open database */
   db = db_init_database(NULL, NULL, db_name, user, password, dbhost,
          dbport, NULL, dbsslmode, dbsslkey, dbsslcert, dbsslca,
           dbsslcapath, dbsslcipher, false, false);

   if (!db || !db_open_database(NULL, db)) {
      Emsg1(M_FATAL, 0, "%s", db_strerror(db));
          return 1;
   }

   /* Drop temporary index idx_tmp_name if it already exists */
   drop_tmp_idx("idxPIchk", "File");

   if (batch) {
      repair_bad_paths();
      repair_bad_filenames();
      eliminate_duplicate_filenames();
      eliminate_duplicate_paths();
      eliminate_orphaned_jobmedia_records();
      eliminate_orphaned_file_records();
      eliminate_orphaned_path_records();
      eliminate_orphaned_filename_records();
      eliminate_orphaned_fileset_records();
      eliminate_orphaned_client_records();
      eliminate_orphaned_job_records();
      eliminate_admin_records();
      eliminate_restore_records();
   } else {
      do_interactive_mode();
   }

   /* Drop temporary index idx_tmp_name */
   drop_tmp_idx("idxPIchk", "File");

   if (db) db_close_database(NULL, db);
   close_msg(NULL);
   term_msg();
   lmgr_cleanup_main();
   return 0;
}

void print_catalog_details(CAT *catalog, const char *working_dir)
{
   POOLMEM *catalog_details = get_pool_memory(PM_MESSAGE);

   /*
    * Instantiate a BDB class and see what db_type gets assigned to it.
    */
   db = db_init_database(NULL, catalog->db_driver, catalog->db_name, catalog->db_user,
                         catalog->db_password, catalog->db_address,
                         catalog->db_port, catalog->db_socket,
                         catalog->db_ssl_mode, catalog->db_ssl_key, 
                         catalog->db_ssl_cert, catalog->db_ssl_ca,
                         catalog->db_ssl_capath, catalog->db_ssl_cipher,
                         catalog->mult_db_connections,
                         catalog->disable_batch_insert);
   if (db) {
      printf("%sdb_type=%s\nworking_dir=%s\n", catalog->display(catalog_details),
             db_get_engine_name(db), working_directory);
      db_close_database(NULL, db);
   }
   free_pool_memory(catalog_details);
}

static void do_interactive_mode()
{
   const char *cmd;

   printf(_("Hello, this is the database check/correct program.\n"));
   if (fix)
      printf(_("Modify database is on."));
   else
      printf(_("Modify database is off."));
   if (verbose)
      printf(_(" Verbose is on.\n"));
   else
      printf(_(" Verbose is off.\n"));

   printf(_("Please select the function you want to perform.\n"));

   while (!quit) {
      if (fix) {
         printf(_("\n"
"     1) Toggle modify database flag\n"
"     2) Toggle verbose flag\n"
"     3) Repair bad Filename records\n"
"     4) Repair bad Path records\n"
"     5) Eliminate duplicate Filename records\n"
"     6) Eliminate duplicate Path records\n"
"     7) Eliminate orphaned Jobmedia records\n"
"     8) Eliminate orphaned File records\n"
"     9) Eliminate orphaned Path records\n"
"    10) Eliminate orphaned Filename records\n"
"    11) Eliminate orphaned FileSet records\n"
"    12) Eliminate orphaned Client records\n"
"    13) Eliminate orphaned Job records\n"
"    14) Eliminate all Admin records\n"
"    15) Eliminate all Restore records\n"
"    16) Eliminate all Verify records\n"
"    17) All (3-16)\n"
"    18) Quit\n"));
       } else {
         printf(_("\n"
"     1) Toggle modify database flag\n"
"     2) Toggle verbose flag\n"
"     3) Check for bad Filename records\n"
"     4) Check for bad Path records\n"
"     5) Check for duplicate Filename records\n"
"     6) Check for duplicate Path records\n"
"     7) Check for orphaned Jobmedia records\n"
"     8) Check for orphaned File records\n"
"     9) Check for orphaned Path records\n"
"    10) Check for orphaned Filename records\n"
"    11) Check for orphaned FileSet records\n"
"    12) Check for orphaned Client records\n"
"    13) Check for orphaned Job records\n"
"    14) Check for all Admin records\n"
"    15) Check for all Restore records\n"
"    16) Check for all Verify records\n"
"    17) All (3-16)\n"
"    18) Quit\n"));
       }

      cmd = get_cmd(_("Select function number: "));
      if (cmd) {
         int item = atoi(cmd);
         switch (item) {
         case 1:
            fix = !fix;
            if (fix)
               printf(_("Database will be modified.\n"));
            else
               printf(_("Database will NOT be modified.\n"));
            break;
         case 2:
            verbose = verbose?0:1;
            if (verbose)
               printf(_(" Verbose is on.\n"));
            else
               printf(_(" Verbose is off.\n"));
            break;
         case 3:
            repair_bad_filenames();
            break;
         case 4:
            repair_bad_paths();
            break;
         case 5:
            eliminate_duplicate_filenames();
            break;
         case 6:
            eliminate_duplicate_paths();
            break;
         case 7:
            eliminate_orphaned_jobmedia_records();
            break;
         case 8:
            eliminate_orphaned_file_records();
            break;
         case 9:
            eliminate_orphaned_path_records();
            break;
         case 10:
            eliminate_orphaned_filename_records();
            break;
         case 11:
            eliminate_orphaned_fileset_records();
            break;
         case 12:
            eliminate_orphaned_client_records();
            break;
         case 13:
            eliminate_orphaned_job_records();
            break;
         case 14:
            eliminate_admin_records();
            break;
         case 15:
            eliminate_restore_records();
            break;
         case 16:
            eliminate_verify_records();
            break;
         case 17:
            repair_bad_filenames();
            repair_bad_paths();
            eliminate_duplicate_filenames();
            eliminate_duplicate_paths();
            eliminate_orphaned_jobmedia_records();
            eliminate_orphaned_file_records();
            eliminate_orphaned_path_records();
            eliminate_orphaned_filename_records();
            eliminate_orphaned_fileset_records();
            eliminate_orphaned_client_records();
            eliminate_orphaned_job_records();
            eliminate_admin_records();
            eliminate_restore_records();
            eliminate_verify_records();
            break;
         case 18:
            quit = true;
            break;
         }
      }
   }
}

static int print_name_handler(void *ctx, int num_fields, char **row)
{
   if (row[0]) {
      printf("%s\n", row[0]);
   }
   return 0;
}

static int get_name_handler(void *ctx, int num_fields, char **row)
{
   POOLMEM *buf = (POOLMEM *)ctx;
   if (row[0]) {
      pm_strcpy(&buf, row[0]);
   }
   return 0;
}

static int print_job_handler(void *ctx, int num_fields, char **row)
{
   printf(_("JobId=%s Name=\"%s\" StartTime=%s\n"),
              NPRT(row[0]), NPRT(row[1]), NPRT(row[2]));
   return 0;
}

static int print_jobmedia_handler(void *ctx, int num_fields, char **row)
{
   printf(_("Orphaned JobMediaId=%s JobId=%s Volume=\"%s\"\n"),
              NPRT(row[0]), NPRT(row[1]), NPRT(row[2]));
   return 0;
}

static int print_file_handler(void *ctx, int num_fields, char **row)
{
   printf(_("Orphaned FileId=%s JobId=%s Volume=\"%s\"\n"),
              NPRT(row[0]), NPRT(row[1]), NPRT(row[2]));
   return 0;
}

static int print_fileset_handler(void *ctx, int num_fields, char **row)
{
   printf(_("Orphaned FileSetId=%s FileSet=\"%s\" MD5=%s\n"),
              NPRT(row[0]), NPRT(row[1]), NPRT(row[2]));
   return 0;
}

static int print_client_handler(void *ctx, int num_fields, char **row)
{
   printf(_("Orphaned ClientId=%s Name=\"%s\"\n"),
              NPRT(row[0]), NPRT(row[1]));
   return 0;
}

/*
 * Called here with each id to be added to the list
 */
static int id_list_handler(void *ctx, int num_fields, char **row)
{
   ID_LIST *lst = (ID_LIST *)ctx;

   if (lst->num_ids == MAX_ID_LIST_LEN) {
      return 1;
   }
   if (lst->num_ids == lst->max_ids) {
      if (lst->max_ids == 0) {
         lst->max_ids = 10000;
         lst->Id = (int64_t *)bmalloc(sizeof(int64_t) * lst->max_ids);
      } else {
         lst->max_ids = (lst->max_ids * 3) / 2;
         lst->Id = (int64_t *)brealloc(lst->Id, sizeof(int64_t) * lst->max_ids);
      }
   }
   lst->Id[lst->num_ids++] = str_to_int64(row[0]);
   return 0;
}

/*
 * Construct record id list
 */
static int make_id_list(const char *query, ID_LIST *id_list)
{
   id_list->num_ids = 0;
   id_list->num_del = 0;
   id_list->tot_ids = 0;

   if (!db_sql_query(db, query, id_list_handler, (void *)id_list)) {
      printf("%s", db_strerror(db));
      return 0;
   }
   return 1;
}

/*
 * Delete all entries in the list
 */
static int delete_id_list(const char *query, ID_LIST *id_list)
{
   char ed1[50];
   for (int i=0; i < id_list->num_ids; i++) {
      bsnprintf(buf, sizeof(buf), query, edit_int64(id_list->Id[i], ed1));
      if (verbose) {
         printf(_("Deleting: %s\n"), buf);
      }
      db_sql_query(db, buf, NULL, NULL);
   }
   return 1;
}

/*
 * Called here with each name to be added to the list
 */
static int name_list_handler(void *ctx, int num_fields, char **row)
{
   NAME_LIST *name = (NAME_LIST *)ctx;

   if (name->num_ids == MAX_ID_LIST_LEN) {
      return 1;
   }
   if (name->num_ids == name->max_ids) {
      if (name->max_ids == 0) {
         name->max_ids = 10000;
         name->name = (char **)bmalloc(sizeof(char *) * name->max_ids);
      } else {
         name->max_ids = (name->max_ids * 3) / 2;
         name->name = (char **)brealloc(name->name, sizeof(char *) * name->max_ids);
      }
   }
   name->name[name->num_ids++] = bstrdup(row[0]);
   return 0;
}

/*
 * Construct name list
 */
static int make_name_list(const char *query, NAME_LIST *name_list)
{
   name_list->num_ids = 0;
   name_list->num_del = 0;
   name_list->tot_ids = 0;

   if (!db_sql_query(db, query, name_list_handler, (void *)name_list)) {
      printf("%s", db_strerror(db));
      return 0;
   }
   return 1;
}

/*
 * Print names in the list
 */
static void print_name_list(NAME_LIST *name_list)
{
   for (int i=0; i < name_list->num_ids; i++) {
      printf("%s\n", name_list->name[i]);
   }
}

/*
 * Free names in the list
 */
static void free_name_list(NAME_LIST *name_list)
{
   for (int i=0; i < name_list->num_ids; i++) {
      free(name_list->name[i]);
   }
   name_list->num_ids = 0;
}

static void eliminate_duplicate_filenames()
{
   const char *query;
   char esc_name[5000];

   printf(_("Checking for duplicate Filename entries.\n"));

   /* Make list of duplicated names */
   query = "SELECT Name, count(Name) as Count FROM Filename GROUP BY  Name "
           "HAVING count(Name) > 1";

   if (!make_name_list(query, &name_list)) {
      exit(1);
   }
   printf(_("Found %d duplicate Filename records.\n"), name_list.num_ids);
   if (name_list.num_ids && verbose && yes_no(_("Print the list? (yes/no): "))) {
      print_name_list(&name_list);
   }
   if (quit) {
      return;
   }
   if (fix) {
      /* Loop through list of duplicate names */
      for (int i=0; i<name_list.num_ids; i++) {
         /* Get all the Ids of each name */
         db_escape_string(NULL, db, esc_name, name_list.name[i], strlen(name_list.name[i]));
         bsnprintf(buf, sizeof(buf), "SELECT FilenameId FROM Filename WHERE Name='%s'", esc_name);
         if (verbose > 1) {
            printf("%s\n", buf);
         }
         if (!make_id_list(buf, &id_list)) {
            exit(1);
         }
         if (verbose) {
            printf(_("Found %d for: %s\n"), id_list.num_ids, name_list.name[i]);
         }
         /* Force all records to use the first id then delete the other ids */
         for (int j=1; j<id_list.num_ids; j++) {
            char ed1[50], ed2[50];
            bsnprintf(buf, sizeof(buf), "UPDATE File SET FilenameId=%s WHERE FilenameId=%s",
               edit_int64(id_list.Id[0], ed1), edit_int64(id_list.Id[j], ed2));
            if (verbose > 1) {
               printf("%s\n", buf);
            }
            db_sql_query(db, buf, NULL, NULL);
            bsnprintf(buf, sizeof(buf), "DELETE FROM Filename WHERE FilenameId=%s",
               ed2);
            if (verbose > 2) {
               printf("%s\n", buf);
            }
            db_sql_query(db, buf, NULL, NULL);
         }
      }
   }
   free_name_list(&name_list);
}

static void eliminate_duplicate_paths()
{
   const char *query;
   char esc_name[5000];

   printf(_("Checking for duplicate Path entries.\n"));

   /* Make list of duplicated names */
   query = "SELECT Path, count(Path) as Count FROM Path "
           "GROUP BY Path HAVING count(Path) > 1";

   if (!make_name_list(query, &name_list)) {
      exit(1);
   }
   printf(_("Found %d duplicate Path records.\n"), name_list.num_ids);
   if (name_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
      print_name_list(&name_list);
   }
   if (quit) {
      return;
   }
   if (fix) {
      /* Loop through list of duplicate names */
      for (int i=0; i<name_list.num_ids; i++) {
         /* Get all the Ids of each name */
         db_escape_string(NULL, db,  esc_name, name_list.name[i], strlen(name_list.name[i]));
         bsnprintf(buf, sizeof(buf), "SELECT PathId FROM Path WHERE Path='%s'", esc_name);
         if (verbose > 1) {
            printf("%s\n", buf);
         }
         if (!make_id_list(buf, &id_list)) {
            exit(1);
         }
         if (verbose) {
            printf(_("Found %d for: %s\n"), id_list.num_ids, name_list.name[i]);
         }
         /* Force all records to use the first id then delete the other ids */
         for (int j=1; j<id_list.num_ids; j++) {
            char ed1[50], ed2[50];
            bsnprintf(buf, sizeof(buf), "UPDATE File SET PathId=%s WHERE PathId=%s",
               edit_int64(id_list.Id[0], ed1), edit_int64(id_list.Id[j], ed2));
            if (verbose > 1) {
               printf("%s\n", buf);
            }
            db_sql_query(db, buf, NULL, NULL);
            bsnprintf(buf, sizeof(buf), "DELETE FROM Path WHERE PathId=%s", ed2);
            if (verbose > 2) {
               printf("%s\n", buf);
            }
            db_sql_query(db, buf, NULL, NULL);
         }
      }
   }
   free_name_list(&name_list);
}

static void eliminate_orphaned_jobmedia_records()
{
   const char *query = "SELECT JobMedia.JobMediaId,Job.JobId FROM JobMedia "
                "LEFT OUTER JOIN Job ON (JobMedia.JobId=Job.JobId) "
                "WHERE Job.JobId IS NULL LIMIT 300000";

   printf(_("Checking for orphaned JobMedia entries.\n"));
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   /* Loop doing 300000 at a time */
   while (id_list.num_ids != 0) {
      printf(_("Found %d orphaned JobMedia records.\n"), id_list.num_ids);
      if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
         for (int i=0; i < id_list.num_ids; i++) {
            char ed1[50];
            bsnprintf(buf, sizeof(buf),
"SELECT JobMedia.JobMediaId,JobMedia.JobId,Media.VolumeName FROM JobMedia,Media "
        "WHERE JobMedia.JobMediaId=%s AND Media.MediaId=JobMedia.MediaId",
               edit_int64(id_list.Id[i], ed1));
            if (!db_sql_query(db, buf, print_jobmedia_handler, NULL)) {
               printf("%s\n", db_strerror(db));
            }
         }
      }
      if (quit) {
         return;
      }

      if (fix && id_list.num_ids > 0) {
         printf(_("Deleting %d orphaned JobMedia records.\n"), id_list.num_ids);
         delete_id_list("DELETE FROM JobMedia WHERE JobMediaId=%s", &id_list);
      } else {
         break;                       /* get out if not updating db */
      }
      if (!make_id_list(query, &id_list)) {
         exit(1);
      }
   }
}

static void eliminate_orphaned_file_records()
{
   const char *query = "SELECT File.FileId,Job.JobId FROM File "
                "LEFT OUTER JOIN Job ON (File.JobId=Job.JobId) "
               "WHERE Job.JobId IS NULL LIMIT 300000";

   printf(_("Checking for orphaned File entries. This may take some time!\n"));
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   /* Loop doing 300000 at a time */
   while (id_list.num_ids != 0) {
      printf(_("Found %d orphaned File records.\n"), id_list.num_ids);
      if (name_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
         for (int i=0; i < id_list.num_ids; i++) {
            char ed1[50];
            bsnprintf(buf, sizeof(buf),
"SELECT File.FileId,File.JobId,Filename.Name FROM File,Filename "
   "WHERE File.FileId=%s AND File.FilenameId=Filename.FilenameId",
               edit_int64(id_list.Id[i], ed1));
            if (!db_sql_query(db, buf, print_file_handler, NULL)) {
               printf("%s\n", db_strerror(db));
            }
         }
      }
      if (quit) {
         return;
      }
      if (fix && id_list.num_ids > 0) {
         printf(_("Deleting %d orphaned File records.\n"), id_list.num_ids);
         delete_id_list("DELETE FROM File WHERE FileId=%s", &id_list);
      } else {
         break;                       /* get out if not updating db */
      }
      if (!make_id_list(query, &id_list)) {
         exit(1);
      }
   }
}

static void eliminate_orphaned_path_records()
{
   db_int64_ctx lctx;
   lctx.count=0;
   db_sql_query(db, "SELECT 1 FROM Job WHERE HasCache=1 LIMIT 1", 
                db_int64_handler, &lctx);

   /* The BVFS code uses Path records that are not in the File table, for
    * example if a Job has /home/test/ BVFS will need to create a Path record /
    * and /home/ to work correctly
    */
   if (lctx.count == 1) {
      printf(_("To prune orphaned Path entries, it is necessary to clear the BVFS Cache first with the bconsole \".bvfs_clear_cache yes\" command.\n"));
      return;
   }

   idx_tmp_name = NULL;
   /* Check the existence of the required "one column" index */
   if (!check_idx("PathId"))  {
      if (yes_no(_("Create temporary index? (yes/no): "))) {
         /* create temporary index PathId */
         create_tmp_idx("idxPIchk", "File", "PathId");
      }
   }

   const char *query = "SELECT DISTINCT Path.PathId,File.PathId FROM Path "
               "LEFT OUTER JOIN File ON (Path.PathId=File.PathId) "
               "WHERE File.PathId IS NULL LIMIT 300000";

   printf(_("Checking for orphaned Path entries. This may take some time!\n"));
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   /* Loop doing 300000 at a time */
   while (id_list.num_ids != 0) {
      printf(_("Found %d orphaned Path records.\n"), id_list.num_ids);
      if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
         for (int i=0; i < id_list.num_ids; i++) {
            char ed1[50];
            bsnprintf(buf, sizeof(buf), "SELECT Path FROM Path WHERE PathId=%s", 
               edit_int64(id_list.Id[i], ed1));
            db_sql_query(db, buf, print_name_handler, NULL);
         }
      }
      if (quit) {
         return;
      }
      if (fix && id_list.num_ids > 0) {
         printf(_("Deleting %d orphaned Path records.\n"), id_list.num_ids);
         delete_id_list("DELETE FROM Path WHERE PathId=%s", &id_list);
      } else {
         break;                       /* get out if not updating db */
      }
      if (!make_id_list(query, &id_list)) {
         exit(1);
      }
   } 
   /* Drop temporary index idx_tmp_name */
   drop_tmp_idx("idxPIchk", "File");
}

static void eliminate_orphaned_filename_records()
{
   idx_tmp_name = NULL;
   /* Check the existence of the required "one column" index */
   if (!check_idx("FilenameId") )      {
      if (yes_no(_("Create temporary index? (yes/no): "))) {
         /* Create temporary index FilenameId */
         create_tmp_idx("idxFIchk", "File", "FilenameId");
      }
   }

   const char *query = "SELECT Filename.FilenameId,File.FilenameId FROM Filename "
                "LEFT OUTER JOIN File ON (Filename.FilenameId=File.FilenameId) "
                "WHERE File.FilenameId IS NULL LIMIT 300000";

   printf(_("Checking for orphaned Filename entries. This may take some time!\n"));
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   /* Loop doing 300000 at a time */
   while (id_list.num_ids != 0) {
      printf(_("Found %d orphaned Filename records.\n"), id_list.num_ids);
      if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
         for (int i=0; i < id_list.num_ids; i++) {
            char ed1[50];
            bsnprintf(buf, sizeof(buf), "SELECT Name FROM Filename WHERE FilenameId=%s", 
               edit_int64(id_list.Id[i], ed1));
            db_sql_query(db, buf, print_name_handler, NULL);
         }
      }
      if (quit) {
         return;
      }
      if (fix && id_list.num_ids > 0) {
         printf(_("Deleting %d orphaned Filename records.\n"), id_list.num_ids);
         delete_id_list("DELETE FROM Filename WHERE FilenameId=%s", &id_list);
      } else {
         break;                       /* get out if not updating db */
      }
      if (!make_id_list(query, &id_list)) {
         exit(1);
      }
   }
   /* Drop temporary index idx_tmp_name */
   drop_tmp_idx("idxFIchk", "File");

}

static void eliminate_orphaned_fileset_records()
{
   const char *query;

   printf(_("Checking for orphaned FileSet entries. This takes some time!\n"));
   query = "SELECT FileSet.FileSetId,Job.FileSetId FROM FileSet "
           "LEFT OUTER JOIN Job ON (FileSet.FileSetId=Job.FileSetId) "
           "WHERE Job.FileSetId IS NULL";
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   printf(_("Found %d orphaned FileSet records.\n"), id_list.num_ids);
   if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
      for (int i=0; i < id_list.num_ids; i++) {
         char ed1[50];
         bsnprintf(buf, sizeof(buf), "SELECT FileSetId,FileSet,MD5 FROM FileSet "
                      "WHERE FileSetId=%s", edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, print_fileset_handler, NULL)) {
            printf("%s\n", db_strerror(db));
         }
      }
   }
   if (quit) {
      return;
   }
   if (fix && id_list.num_ids > 0) {
      printf(_("Deleting %d orphaned FileSet records.\n"), id_list.num_ids);
      delete_id_list("DELETE FROM FileSet WHERE FileSetId=%s", &id_list);
   }
}

static void eliminate_orphaned_client_records()
{
   const char *query;

   printf(_("Checking for orphaned Client entries.\n"));
   /* In English:
    *   Wiffle through Client for every Client
    *   joining with the Job table including every Client even if
    *   there is not a match in Job (left outer join), then
    *   filter out only those where no Job points to a Client
    *   i.e. Job.Client is NULL
    */
   query = "SELECT Client.ClientId,Client.Name FROM Client "
           "LEFT OUTER JOIN Job ON (Client.ClientId=Job.ClientId) "
           "WHERE Job.ClientId IS NULL";
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   printf(_("Found %d orphaned Client records.\n"), id_list.num_ids);
   if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
      for (int i=0; i < id_list.num_ids; i++) {
         char ed1[50];
         bsnprintf(buf, sizeof(buf), "SELECT ClientId,Name FROM Client "
                      "WHERE ClientId=%s", edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, print_client_handler, NULL)) {
            printf("%s\n", db_strerror(db));
         }
      }
   }
   if (quit) {
      return;
   }
   if (fix && id_list.num_ids > 0) {
      printf(_("Deleting %d orphaned Client records.\n"), id_list.num_ids);
      delete_id_list("DELETE FROM Client WHERE ClientId=%s", &id_list);
   }
}

static void eliminate_orphaned_job_records()
{
   const char *query;

   printf(_("Checking for orphaned Job entries.\n"));
   /* In English:
    *   Wiffle through Job for every Job
    *   joining with the Client table including every Job even if
    *   there is not a match in Client (left outer join), then
    *   filter out only those where no Client exists
    *   i.e. Client.Name is NULL
    */
   query = "SELECT Job.JobId,Job.Name FROM Job "
           "LEFT OUTER JOIN Client ON (Job.ClientId=Client.ClientId) "
           "WHERE Client.Name IS NULL";
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   printf(_("Found %d orphaned Job records.\n"), id_list.num_ids);
   if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
      for (int i=0; i < id_list.num_ids; i++) {
         char ed1[50];
         bsnprintf(buf, sizeof(buf), "SELECT JobId,Name,StartTime FROM Job "
                      "WHERE JobId=%s", edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, print_job_handler, NULL)) {
            printf("%s\n", db_strerror(db));
         }
      }
   }
   if (quit) {
      return;
   }
   if (fix && id_list.num_ids > 0) {
      printf(_("Deleting %d orphaned Job records.\n"), id_list.num_ids);
      delete_id_list("DELETE FROM Job WHERE JobId=%s", &id_list);
      printf(_("Deleting JobMedia records of orphaned Job records.\n"));
      delete_id_list("DELETE FROM JobMedia WHERE JobId=%s", &id_list);
      printf(_("Deleting Log records of orphaned Job records.\n"));
      delete_id_list("DELETE FROM Log WHERE JobId=%s", &id_list);
   }
}

static void eliminate_admin_records()
{
   const char *query;

   printf(_("Checking for Admin Job entries.\n"));
   query = "SELECT Job.JobId FROM Job "
           "WHERE Job.Type='D'";
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   printf(_("Found %d Admin Job records.\n"), id_list.num_ids);
   if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
      for (int i=0; i < id_list.num_ids; i++) {
         char ed1[50];
         bsnprintf(buf, sizeof(buf), "SELECT JobId,Name,StartTime FROM Job "
                      "WHERE JobId=%s", edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, print_job_handler, NULL)) {
            printf("%s\n", db_strerror(db));
         }
      }
   }
   if (quit) {
      return;
   }
   if (fix && id_list.num_ids > 0) {
      printf(_("Deleting %d Admin Job records.\n"), id_list.num_ids);
      delete_id_list("DELETE FROM Job WHERE JobId=%s", &id_list);
   }
}

static void eliminate_restore_records()
{
   const char *query;

   printf(_("Checking for Restore Job entries.\n"));
   query = "SELECT Job.JobId FROM Job "
           "WHERE Job.Type='R'";
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   printf(_("Found %d Restore Job records.\n"), id_list.num_ids);
   if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
      for (int i=0; i < id_list.num_ids; i++) {
         char ed1[50];
         bsnprintf(buf, sizeof(buf), "SELECT JobId,Name,StartTime FROM Job "
                      "WHERE JobId=%s", edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, print_job_handler, NULL)) {
            printf("%s\n", db_strerror(db));
         }
      }
   }
   if (quit) {
      return;
   }
   if (fix && id_list.num_ids > 0) {
      printf(_("Deleting %d Restore Job records.\n"), id_list.num_ids);
      delete_id_list("DELETE FROM Job WHERE JobId=%s", &id_list);
   }
}

static void eliminate_verify_records()
{
   const char *query;

   printf(_("Checking for Verify Job entries.\n"));
   query = "SELECT Job.JobId FROM Job "
           "WHERE Job.Type='V'";
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   printf(_("Found %d Verify Job records.\n"), id_list.num_ids);
   if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
      for (int i=0; i < id_list.num_ids; i++) {
         char ed1[50];
         bsnprintf(buf, sizeof(buf), "SELECT JobId,Name,StartTime FROM Job "
                      "WHERE JobId=%s", edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, print_job_handler, NULL)) {
            printf("%s\n", db_strerror(db));
         }
      }
   }
   if (quit) {
      return;
   }
   if (fix && id_list.num_ids > 0) {
      printf(_("Deleting %d Verify Job records.\n"), id_list.num_ids);
      delete_id_list("DELETE FROM Job WHERE JobId=%s", &id_list);
   }
}

static void repair_bad_filenames()
{
   const char *query;
   int i;

   printf(_("Checking for Filenames with a trailing slash\n"));
   query = "SELECT FilenameId,Name from Filename "
           "WHERE Name LIKE '%/'";
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   printf(_("Found %d bad Filename records.\n"), id_list.num_ids);
   if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
      for (i=0; i < id_list.num_ids; i++) {
         char ed1[50];
         bsnprintf(buf, sizeof(buf),
            "SELECT Name FROM Filename WHERE FilenameId=%s", 
                edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, print_name_handler, NULL)) {
            printf("%s\n", db_strerror(db));
         }
      }
   }
   if (quit) {
      return;
   }
   if (fix && id_list.num_ids > 0) {
      POOLMEM *name = get_pool_memory(PM_FNAME);
      char esc_name[5000];
      printf(_("Reparing %d bad Filename records.\n"), id_list.num_ids);
      for (i=0; i < id_list.num_ids; i++) {
         int len;
         char ed1[50];
         bsnprintf(buf, sizeof(buf),
            "SELECT Name FROM Filename WHERE FilenameId=%s", 
               edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, get_name_handler, name)) {
            printf("%s\n", db_strerror(db));
         }
         /* Strip trailing slash(es) */
         for (len=strlen(name); len > 0 && IsPathSeparator(name[len-1]); len--)
            {  }
         if (len == 0) {
            len = 1;
            esc_name[0] = ' ';
            esc_name[1] = 0;
         } else {
            name[len-1] = 0;
            db_escape_string(NULL, db, esc_name, name, len);
         }
         bsnprintf(buf, sizeof(buf),
            "UPDATE Filename SET Name='%s' WHERE FilenameId=%s",
            esc_name, edit_int64(id_list.Id[i], ed1));
         if (verbose > 1) {
            printf("%s\n", buf);
         }
         db_sql_query(db, buf, NULL, NULL);
      }
      free_pool_memory(name); 
   }
}

static void repair_bad_paths()
{
   const char *query;
   int i;

   printf(_("Checking for Paths without a trailing slash\n"));
   query = "SELECT PathId,Path from Path "
           "WHERE Path NOT LIKE '%/'";
   if (verbose > 1) {
      printf("%s\n", query);
   }
   if (!make_id_list(query, &id_list)) {
      exit(1);
   }
   printf(_("Found %d bad Path records.\n"), id_list.num_ids);
   if (id_list.num_ids && verbose && yes_no(_("Print them? (yes/no): "))) {
      for (i=0; i < id_list.num_ids; i++) {
         char ed1[50];
         bsnprintf(buf, sizeof(buf),
            "SELECT Path FROM Path WHERE PathId=%s", edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, print_name_handler, NULL)) {
            printf("%s\n", db_strerror(db));
         }
      }
   }
   if (quit) {
      return;
   }
   if (fix && id_list.num_ids > 0) {
      POOLMEM *name = get_pool_memory(PM_FNAME);
      char esc_name[5000];
      printf(_("Reparing %d bad Filename records.\n"), id_list.num_ids);
      for (i=0; i < id_list.num_ids; i++) {
         int len;
         char ed1[50];
         bsnprintf(buf, sizeof(buf),
            "SELECT Path FROM Path WHERE PathId=%s", edit_int64(id_list.Id[i], ed1));
         if (!db_sql_query(db, buf, get_name_handler, name)) {
            printf("%s\n", db_strerror(db));
         }
         /* Strip trailing blanks */
         for (len=strlen(name); len > 0 && name[len-1]==' '; len--) {
            name[len-1] = 0;
         }
         /* Add trailing slash */
         len = pm_strcat(&name, "/");
         db_escape_string(NULL, db,  esc_name, name, len);
         bsnprintf(buf, sizeof(buf), "UPDATE Path SET Path='%s' WHERE PathId=%s",
            esc_name, edit_int64(id_list.Id[i], ed1));
         if (verbose > 1) {
            printf("%s\n", buf);
         }
         db_sql_query(db, buf, NULL, NULL);
      }
      free_pool_memory(name); 
   }
}

/*
 * Gen next input command from the terminal
 */
static char *get_cmd(const char *prompt)
{
   static char cmd[1000];

   printf("%s", prompt);
   if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
      printf("\n");
      quit = true;
      return NULL;
   }
   strip_trailing_junk(cmd);
   return cmd;
}

static bool yes_no(const char *prompt)
{
   char *cmd;
   cmd = get_cmd(prompt);
   if (!cmd) {
      quit = true;
      return false;
   }
   return (strcasecmp(cmd, "yes") == 0) || (strcasecmp(cmd, _("yes")) == 0);
}

bool python_set_prog(JCR*, char const*) { return false; }

/*
 * The code below to add indexes is needed only for MySQL, and
 *  that to improve the performance.
 */

#define MAXIDX          100
typedef struct s_idx_list {
   char *key_name;
   int  count_key; /* how many times the index meets *key_name */
   int  count_col; /* how many times meets the desired column name */
} IDX_LIST;

static IDX_LIST idx_list[MAXIDX];

/* 
 * Called here with each table index to be added to the list
 */
static int check_idx_handler(void *ctx, int num_fields, char **row)
{
   /* 
    * Table | Non_unique | Key_name | Seq_in_index | Column_name |... 
    * File  |          0 | PRIMARY  |            1 | FileId      |... 
    */ 
   char *name, *key_name, *col_name;
   int i, len;
   int found = false;
 
   name = (char *)ctx;
   key_name = row[2];
   col_name = row[4];
   for(i = 0; (idx_list[i].key_name != NULL) && (i < MAXIDX); i++) {
      if (strcasecmp(idx_list[i].key_name, key_name) == 0 ) {
         idx_list[i].count_key++;
         found = true;
         if (strcasecmp(col_name, name) == 0) {
            idx_list[i].count_col++;
         }
         break;
      }
   }
   /* If the new Key_name, add it to the list */
   if (!found) {
      len = strlen(key_name) + 1;
      idx_list[i].key_name = (char *)malloc(len);
      bstrncpy(idx_list[i].key_name, key_name, len);
      idx_list[i].count_key = 1;
      if (strcasecmp(col_name, name) == 0) {
         idx_list[i].count_col = 1;
      } else {
         idx_list[i].count_col = 0;
      }
   }
   return 0;
}

/*
 * Return TRUE if "one column" index over *col_name exists
 */
static bool check_idx(const char *col_name)
{
   int i;
   int found = false;
   const char *query = "SHOW INDEX FROM File";

   if (db_get_type_index(db) != SQL_TYPE_MYSQL) {
      return true;
   }
   /* Continue for MySQL */
   memset(&idx_list, 0, sizeof(idx_list));
   if (!db_sql_query(db, query, check_idx_handler, (void *)col_name)) {
      printf("%s\n", db_strerror(db));
   }
   for (i = 0; (idx_list[i].key_name != NULL) && (i < MAXIDX) ; i++) {
      /*
       * NOTE : if (idx_list[i].count_key > 1) then index idx_list[i].key_name is "multiple-column" index
       */
      if ((idx_list[i].count_key == 1) && (idx_list[i].count_col == 1)) {
         /* "one column" index over *col_name found */
         found = true;
      }
   }
   if (found) {
      if (verbose) {
         printf(_("Ok. Index over the %s column already exists and dbcheck will work faster.\n"), col_name);
      }
   } else {
      printf(_("Note. Index over the %s column not found, that can greatly slow down dbcheck.\n"), col_name);
   }
   return found;
}

/*
 * Create temporary one-column index
 */
static bool create_tmp_idx(const char *idx_name, const char *table_name, 
                           const char *col_name) 
{
   idx_tmp_name = NULL;
   printf(_("Create temporary index... This may take some time!\n"));
   bsnprintf(buf, sizeof(buf), "CREATE INDEX %s ON %s (%s)", idx_name, table_name, col_name); 
   if (verbose) {
      printf("%s\n", buf);
   }
   if (db_sql_query(db, buf, NULL, NULL)) {
      idx_tmp_name = idx_name;
      if (verbose) {
         printf(_("Temporary index created.\n"));
      }
   } else {
      printf("%s\n", db_strerror(db));
      return false;
   }
   return true;
}

/*
 * Drop temporary index
 */
static bool drop_tmp_idx(const char *idx_name, const char *table_name)
{
   if (idx_tmp_name != NULL) {
      printf(_("Drop temporary index.\n"));
      bsnprintf(buf, sizeof(buf), "DROP INDEX %s ON %s", idx_name, table_name);
      if (verbose) {
         printf("%s\n", buf);
      }
      if (!db_sql_query(db, buf, NULL, NULL)) {
         printf("%s\n", db_strerror(db));
         return false;
      } else {
         if (verbose) {
            printf(_("Temporary index %s deleted.\n"), idx_tmp_name);
         }
      }
   }
   idx_tmp_name = NULL;
   return true;
}
