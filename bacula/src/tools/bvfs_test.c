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
 *  Program to test cache path
 *
 *   Eric Bollengier, August 2009
 *
 *
 */

#include "bacula.h"
#include "cats/cats.h"
#include "cats/bvfs.h"
#include "findlib/find.h"
 
/* Local variables */
static BDB *db;
static const char *file = "COPYRIGHT";
static DBId_t fnid=0;
static const char *db_name = "regress";
static const char *db_user = "regress";
static const char *db_password = "";
static const char *db_host = NULL;
static const char *db_ssl_mode = NULL;
static const char *db_ssl_key = NULL;
static const char *db_ssl_cert = NULL;
static const char *db_ssl_ca = NULL;
static const char *db_ssl_capath = NULL;
static const char *db_ssl_cipher = NULL;

static void usage()
{
   fprintf(stderr, _(
PROG_COPYRIGHT
"\n%sVersion: %s (%s)\n"
"       -d <nn>           set debug level to <nn>\n"
"       -dt               print timestamp in debug output\n"
"       -n <name>         specify the database name (default bacula)\n"
"       -u <user>         specify database user name (default bacula)\n"
"       -P <password      specify database password (default none)\n"
"       -h <host>         specify database host (default NULL)\n"
"       -k <sslkey>       path name to the key file (default NULL)\n"
"       -e <sslcert>      path name to the certificate file (default NULL)\n"
"       -a <sslca>        path name to the CA certificate file (default NULL)\n"
"       -w <working>      specify working directory\n"
"       -j <jobids>       specify jobids\n"
"       -p <path>         specify path\n"
"       -f <file>         specify file\n"
"       -l <limit>        maximum tuple to fetch\n"
"       -T                truncate cache table before starting\n"
"       -v                verbose\n"
"       -?                print this message\n\n"), 2001, "", VERSION, BDATE);
   exit(1);
}

static int result_handler(void *ctx, int fields, char **row)
{
   Bvfs *vfs = (Bvfs *)ctx;
   ATTR *attr = vfs->get_attr();
   char empty[] = "A A A A A A A A A A A A A A";

   memset(&attr->statp, 0, sizeof(struct stat));
   decode_stat((row[BVFS_LStat] && row[BVFS_LStat][0])?row[BVFS_LStat]:empty,
               &attr->statp, sizeof(attr->statp),  &attr->LinkFI);

   if (bvfs_is_dir(row) || bvfs_is_file(row)) {
      /* display clean stuffs */

      if (bvfs_is_dir(row)) {
         pm_strcpy(attr->ofname, bvfs_basename_dir(row[BVFS_Name]));   
      } else {
         /* if we see the requested file, note his filenameid */
         if (bstrcmp(row[BVFS_Name], file)) {
            fnid = str_to_int64(row[BVFS_FilenameId]);
         }
         pm_strcpy(attr->ofname, row[BVFS_Name]);   
      }
      print_ls_output(vfs->get_jcr(), attr);

   } else {
      Pmsg5(0, "JobId=%s FileId=%s\tMd5=%s\tVolName=%s\tVolInChanger=%s\n",
            row[BVFS_JobId], row[BVFS_FileId], row[BVFS_Md5], row[BVFS_VolName],
            row[BVFS_VolInchanger]);

      pm_strcpy(attr->ofname, file);
      print_ls_output(vfs->get_jcr(), attr);
   }
   return 0;
}


/* number of thread started */

int main (int argc, char *argv[])
{
   int ch;
   char *jobids = (char *)"1";
   char *path=NULL, *client=NULL;
   uint64_t limit=0;
   bool clean=false;
   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");
   init_stack_dump();

   Dmsg0(0, "Starting bvfs_test tool\n");
   
   my_name_is(argc, argv, "bvfs_test");
   init_msg(NULL, NULL);

   OSDependentInit();

   while ((ch = getopt(argc, argv, "h:o:k:e:a:c:l:d:n:P:Su:vf:w:?j:p:f:T")) != -1) {
      switch (ch) {
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
      case 'l':
         limit = str_to_int64(optarg);
         break;

      case 'c':
         client = optarg;
         break;

      case 'h':
         db_host = optarg;
         break;

      case 'o':
         db_ssl_mode = optarg;
         break;

      case 'k':
         db_ssl_key= optarg;
         break;

      case 'e':
         db_ssl_cert= optarg;
         break;

      case 'a':
         db_ssl_ca= optarg;
         break;

      case 'n':
         db_name = optarg;
         break;

      case 'w':
         working_directory = optarg;
         break;

      case 'u':
         db_user = optarg;
         break;

      case 'P':
         db_password = optarg;
         break;

      case 'v':
         verbose++;
         break;

      case 'p':
         path = optarg;
         break;

      case 'f':
         file = optarg;
         break;

      case 'j':
         jobids = optarg;
         break;

      case 'T':
         clean = true;
         break;

      case '?':
      default:
         usage();

      }
   }
   argc -= optind;
   argv += optind;

   if (argc != 0) {
      Pmsg0(0, _("Wrong number of arguments: \n"));
      usage();
   }
   JCR *bjcr = new_jcr(sizeof(JCR), NULL);
   bjcr->JobId = getpid();
   bjcr->setJobType(JT_CONSOLE);
   bjcr->setJobLevel(L_FULL);
   bjcr->JobStatus = JS_Running;
   bjcr->client_name = get_pool_memory(PM_FNAME);
   pm_strcpy(bjcr->client_name, "Dummy.Client.Name");
   bstrncpy(bjcr->Job, "bvfs_test", sizeof(bjcr->Job));
   
   if ((db = db_init_database(NULL, NULL, db_name, db_user, db_password, 
                              db_host, 0, NULL,
                              db_ssl_mode, db_ssl_key,
                              db_ssl_cert, db_ssl_ca,
                              db_ssl_capath, db_ssl_cipher,
                              false, false)) == NULL) {
      Emsg0(M_ERROR_TERM, 0, _("Could not init Bacula database\n"));
   }
   Dmsg1(0, "db_type=%s\n", db_get_engine_name(db));

   if (!db_open_database(NULL, db)) {
      Emsg0(M_ERROR_TERM, 0, db_strerror(db));
   }
   Dmsg0(200, "Database opened\n");
   if (verbose) {
      Pmsg2(000, _("Using Database: %s, User: %s\n"), db_name, db_user);
   }
   
   bjcr->db = db;

   if (clean) {
      Pmsg0(0, "Clean old table\n");
      db_sql_query(db, "DELETE FROM PathHierarchy", NULL, NULL);
      db_sql_query(db, "UPDATE Job SET HasCache=0", NULL, NULL);
      db_sql_query(db, "DELETE FROM PathVisibility", NULL, NULL);
      bvfs_update_cache(bjcr, db);
   }

   Bvfs fs(bjcr, db);
   fs.set_handler(result_handler, &fs);

   fs.set_jobids(jobids);
   fs.update_cache();
   if (limit)
      fs.set_limit(limit);

   if (path) {
      fs.ch_dir(path);
      fs.ls_special_dirs();
      fs.ls_dirs();
      while (fs.ls_files()) {
         fs.next_offset();
      }

      if (fnid && client) {
         alist clients(1, not_owned_by_alist);
         clients.append(client);
         Pmsg0(0, "---------------------------------------------\n");
         Pmsg1(0, "Getting file version for %s\n", file);
         fs.get_all_file_versions(fs.get_pwd(), fnid, &clients);
      }

      exit (0);
   }

   
   Pmsg0(0, "list /\n");
   fs.ch_dir("/");
   fs.ls_special_dirs();
   fs.ls_dirs();
   fs.ls_files();

   Pmsg0(0, "list /tmp/\n");
   fs.ch_dir("/tmp/");
   fs.ls_special_dirs();
   fs.ls_dirs();
   fs.ls_files();

   Pmsg0(0, "list /tmp/regress/\n");
   fs.ch_dir("/tmp/regress/");
   fs.ls_special_dirs();
   fs.ls_files();
   fs.ls_dirs();

   Pmsg0(0, "list /tmp/regress/build/\n");
   fs.ch_dir("/tmp/regress/build/");
   fs.ls_special_dirs();
   fs.ls_dirs();
   fs.ls_files();

   fs.get_all_file_versions(1, 347, (char*)"zog4-fd");

   char p[200];
   strcpy(p, "/tmp/toto/rep/");
   bvfs_parent_dir(p);
   if(strcmp(p, "/tmp/toto/")) {
      Pmsg0(000, "Error in bvfs_parent_dir\n");
   }
   bvfs_parent_dir(p);
   if(strcmp(p, "/tmp/")) {
      Pmsg0(000, "Error in bvfs_parent_dir\n");
   }
   bvfs_parent_dir(p);
   if(strcmp(p, "/")) {
      Pmsg0(000, "Error in bvfs_parent_dir\n");
   }
   bvfs_parent_dir(p);
   if(strcmp(p, "")) {
      Pmsg0(000, "Error in bvfs_parent_dir\n");
   }
   bvfs_parent_dir(p);
   if(strcmp(p, "")) {
      Pmsg0(000, "Error in bvfs_parent_dir\n");
   }

   return 0;
}
