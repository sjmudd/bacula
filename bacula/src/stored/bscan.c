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
 *  Program to scan a Bacula Volume and compare it with
 *    the catalog and optionally synchronize the catalog
 *    with the tape.
 *
 *   Kern E. Sibbald, December 2001
 */

#include "bacula.h"
#include "stored.h"
#include "findlib/find.h"
#include "cats/cats.h"

extern bool parse_sd_config(CONFIG *config, const char *configfile, int exit_code);

/* Forward referenced functions */
static void do_scan(void);
static bool record_cb(DCR *dcr, DEV_RECORD *rec);
static int  create_file_attributes_record(BDB *db, JCR *mjcr,
                               char *fname, char *lname, int type,
                               char *ap, DEV_RECORD *rec);
static int  create_media_record(BDB *db, MEDIA_DBR *mr, VOLUME_LABEL *vl);
static bool update_media_record(BDB *db, MEDIA_DBR *mr);
static int  create_pool_record(BDB *db, POOL_DBR *pr);
static JCR *create_job_record(BDB *db, JOB_DBR *mr, SESSION_LABEL *label, DEV_RECORD *rec);
static int  update_job_record(BDB *db, JOB_DBR *mr, SESSION_LABEL *elabel,
                              DEV_RECORD *rec);
static int  create_client_record(BDB *db, CLIENT_DBR *cr);
static int  create_fileset_record(BDB *db, FILESET_DBR *fsr);
static int  create_jobmedia_record(BDB *db, JCR *jcr);
static JCR *create_jcr(JOB_DBR *jr, DEV_RECORD *rec, uint32_t JobId);
static int update_digest_record(BDB *db, char *digest, DEV_RECORD *rec, int type);


/* Local variables */
static DEVICE *dev = NULL;
static BDB *db;
static JCR *bjcr;                     /* jcr for bscan */
static BSR *bsr = NULL;
static MEDIA_DBR mr;
static POOL_DBR pr;
static JOB_DBR jr;
static CLIENT_DBR cr;
static FILESET_DBR fsr;
static ATTR_DBR ar;
static FILE_DBR fr;
static SESSION_LABEL label;
static SESSION_LABEL elabel;
static ATTR *attr;

static time_t lasttime = 0;

static const char *db_driver = "NULL";
static const char *db_name = "bacula";
static const char *db_user = "bacula";
static const char *db_password = "";
static const char *db_host = NULL;
static const char *db_ssl_mode = NULL;
static const char *db_ssl_key = NULL;
static const char *db_ssl_cert = NULL;
static const char *db_ssl_ca = NULL;
static const char *db_ssl_capath = NULL;
static const char *db_ssl_cipher = NULL;
static int db_port = 0;
static const char *wd = NULL;
static bool update_db = false;
static bool update_vol_info = false;
static bool list_records = false;
static int ignored_msgs = 0;

static uint64_t currentVolumeSize;
static int last_pct = -1;
static bool showProgress = false;
static int num_jobs = 0;
static int num_pools = 0;
static int num_media = 0;
static int num_files = 0;

static CONFIG *config;
#define CONFIG_FILE "bacula-sd.conf"

void *start_heap;
char *configfile = NULL;

static void usage()
{
   fprintf(stderr, _(
PROG_COPYRIGHT
"\n%sVersion: %s (%s)\n\n"
"Usage: bscan [ options ] <bacula-archive>\n"
"       -b bootstrap      specify a bootstrap file\n"
"       -c <file>         specify configuration file\n"
"       -d <nn>           set debug level to <nn>\n"
"       -dt               print timestamp in debug output\n"
"       -m                update media info in database\n"
"       -D <driver name>  specify the driver database name (default NULL)\n"
"       -n <name>         specify the database name (default bacula)\n"
"       -u <user>         specify database user name (default bacula)\n"
"       -P <password>     specify database password (default none)\n"
"       -h <host>         specify database host (default NULL)\n"
"       -t <port>         specify database port (default 0)\n"
"       -p                proceed inspite of I/O errors\n"
"       -r                list records\n"
"       -s                synchronize or store in database\n"
"       -S                show scan progress periodically\n"
"       -v                verbose\n"
"       -V <Volumes>      specify Volume names (separated by |)\n"
"       -w <dir>          specify working directory (default from conf file)\n"
"       -?                print this message\n\n"),
      2001, "", VERSION, BDATE);
   exit(1);
}

int main (int argc, char *argv[])
{
   int ch;
   struct stat stat_buf;
   char *VolumeName = NULL;
   BtoolsAskDirHandler askdir_handler;

   init_askdir_handler(&askdir_handler);
   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");
   init_stack_dump();
   lmgr_init_thread();

   my_name_is(argc, argv, "bscan");
   init_msg(NULL, NULL);

   OSDependentInit();

   while ((ch = getopt(argc, argv, "b:c:d:D:h:o:k:e:a:p:mn:pP:rsSt:u:vV:w:?")) != -1) {
      switch (ch) {
      case 'S' :
         showProgress = true;
         break;
      case 'b':
         bsr = parse_bsr(NULL, optarg);
         break;

      case 'c':                    /* specify config file */
         if (configfile != NULL) {
            free(configfile);
         }
         configfile = bstrdup(optarg);
         break;

      case 'D':
         db_driver = optarg;
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

      case 'h':
         db_host = optarg;
         break;

      case 'o':
         db_ssl_mode = optarg;
         break;

      case 'k':
         db_ssl_key = optarg;
         break;

      case 'e':
         db_ssl_cert = optarg;
         break;

      case 'a':
         db_ssl_ca = optarg;
         break;

      case 't':
         db_port = atoi(optarg);
         break;

      case 'm':
         update_vol_info = true;
         break;

      case 'n':
         db_name = optarg;
         break;

      case 'u':
         db_user = optarg;
         break;

      case 'P':
         db_password = optarg;
         break;

      case 'p':
         forge_on = true;
         break;

      case 'r':
         list_records = true;
         break;

      case 's':
         update_db = true;
         break;

      case 'v':
         verbose++;
         break;

      case 'V':                    /* Volume name */
         VolumeName = optarg;
         break;

      case 'w':
         wd = optarg;
         break;

      case '?':
      default:
         usage();

      }
   }
   argc -= optind;
   argv += optind;

   if (argc != 1) {
      Pmsg0(0, _("Wrong number of arguments: \n"));
      usage();
   }

   if (configfile == NULL) {
      configfile = bstrdup(CONFIG_FILE);
   }

   config = New(CONFIG());
   parse_sd_config(config, configfile, M_ERROR_TERM);
   setup_me();
   load_sd_plugins(me->plugin_directory);

   /* Check if -w option given, otherwise use resource for working directory */
   if (wd) {
      working_directory = wd;
   } else if (!me->working_directory) {
      Emsg1(M_ERROR_TERM, 0, _("No Working Directory defined in %s. Cannot continue.\n"),
         configfile);
   } else {
      working_directory = me->working_directory;
   }

   /* Check that working directory is good */
   if (stat(working_directory, &stat_buf) != 0) {
      Emsg1(M_ERROR_TERM, 0, _("Working Directory: %s not found. Cannot continue.\n"),
         working_directory);
   }
   if (!S_ISDIR(stat_buf.st_mode)) {
      Emsg1(M_ERROR_TERM, 0, _("Working Directory: %s is not a directory. Cannot continue.\n"),
         working_directory);
   }

   bjcr = setup_jcr("bscan", argv[0], bsr, VolumeName, SD_READ);
   if (!bjcr) {
      exit(1);
   }
   dev = bjcr->read_dcr->dev;
   if (showProgress) {
      char ed1[50];
      struct stat sb;
      fstat(dev->fd(), &sb);
      currentVolumeSize = sb.st_size;
      Pmsg1(000, _("First Volume Size = %s\n"),
         edit_uint64(currentVolumeSize, ed1));
   }

   db = db_init_database(NULL, db_driver, db_name, db_user, db_password,
                         db_host, db_port, NULL,
                         db_ssl_mode, db_ssl_key,
                         db_ssl_cert, db_ssl_ca,
                         db_ssl_capath, db_ssl_cipher,
                         false, false);
   if (!db || !db_open_database(NULL, db)) {
      Pmsg2(000, _("Could not open Catalog \"%s\", database \"%s\".\n"),
           db_driver, db_name);
      if (db) {
         Jmsg(NULL, M_FATAL, 0, _("%s"), db_strerror(db));
         Pmsg1(000, "%s", db_strerror(db));
         db_close_database(NULL, db);
      }
      Jmsg(NULL, M_ERROR_TERM, 0, _("Could not open Catalog \"%s\", database \"%s\".\n"),
           db_driver, db_name);
   }
   Dmsg0(200, "Database opened\n");
   if (verbose) {
      Pmsg2(000, _("Using Database: %s, User: %s\n"), db_name, db_user);
   }

   do_scan();
   if (update_db) {
      printf("Records added or updated in the catalog:\n%7d Media\n%7d Pool\n%7d Job\n%7d File\n",
         num_media, num_pools, num_jobs, num_files);
   } else {
      printf("Records would have been added or updated in the catalog:\n%7d Media\n%7d Pool\n%7d Job\n%7d File\n",
         num_media, num_pools, num_jobs, num_files);
   }

   free_jcr(bjcr);
   dev->term(NULL);
   return 0;
}

/*
 * We are at the end of reading a Volume. Now, we simulate handling
 *   the end of writing a Volume by wiffling through the attached
 *   jcrs creating jobmedia records.
 */
static bool bscan_mount_next_read_volume(DCR *dcr)
{
   DEVICE *dev = dcr->dev;
   DCR *mdcr;
   Dmsg1(100, "Walk attached jcrs. Volume=%s\n", dev->getVolCatName());
   foreach_dlist(mdcr, dev->attached_dcrs) {
      JCR *mjcr = mdcr->jcr;
      Dmsg1(100, "========== JobId=%u ========\n", mjcr->JobId);
      if (mjcr->JobId == 0) {
         continue;
      }
      if (verbose) {
         Pmsg1(000, _("Create JobMedia for Job %s\n"), mjcr->Job);
      }
      mdcr->StartAddr = dcr->StartAddr;
      mdcr->EndAddr = dcr->EndAddr;
      mdcr->VolMediaId = dcr->VolMediaId;
      mjcr->read_dcr->VolLastIndex = dcr->VolLastIndex;
      if( mjcr->bscan_insert_jobmedia_records ) {
         if (!create_jobmedia_record(db, mjcr)) {
            Pmsg2(000, _("Could not create JobMedia record for Volume=%s Job=%s\n"),
               dev->getVolCatName(), mjcr->Job);
         }
      }
   }

   update_media_record(db, &mr);

   /* Now let common read routine get up next tape. Note,
    * we call mount_next... with bscan's jcr because that is where we
    * have the Volume list, but we get attached.
    */
   bool stat = mount_next_read_volume(dcr);

   if (showProgress) {
      char ed1[50];
      struct stat sb;
      fstat(dev->fd(), &sb);
      currentVolumeSize = sb.st_size;
      Pmsg1(000, _("First Volume Size = %s\n"),
         edit_uint64(currentVolumeSize, ed1));
   }
   return stat;
}

static void do_scan()
{
   attr = new_attr(bjcr);

   bmemset(&ar, 0, sizeof(ar));
   bmemset(&pr, 0, sizeof(pr));
   bmemset(&jr, 0, sizeof(jr));
   bmemset(&cr, 0, sizeof(cr));
   bmemset(&fsr, 0, sizeof(fsr));
   bmemset(&fr, 0, sizeof(fr));

   /* Detach bscan's jcr as we are not a real Job on the tape */

   read_records(bjcr->read_dcr, record_cb, bscan_mount_next_read_volume);

   if (update_db) {
      db_write_batch_file_records(bjcr); /* used by bulk batch file insert */
   }
   free_attr(attr);
}

/*
 * Returns: true  if OK
 *          false if error
 */
static bool record_cb(DCR *dcr, DEV_RECORD *rec)
{
   JCR *mjcr;
   char ec1[30];
   DEVICE *dev = dcr->dev;
   JCR *bjcr = dcr->jcr;
   DEV_BLOCK *block = dcr->block;
   POOL_MEM sql_buffer;
   db_int64_ctx jmr_count;

   char digest[BASE64_SIZE(CRYPTO_DIGEST_MAX_SIZE)];

   if (rec->data_len > 0) {
      mr.VolBytes += rec->data_len + WRITE_RECHDR_LENGTH; /* Accumulate Volume bytes */
      if (showProgress && currentVolumeSize > 0) {
         int pct = (mr.VolBytes * 100) / currentVolumeSize;
         if (pct != last_pct) {
            fprintf(stdout, _("done: %d%%\n"), pct);
            fflush(stdout);
            last_pct = pct;
         }
      }
   }

   if (list_records) {
      Pmsg5(000, _("Record: SessId=%u SessTim=%u FileIndex=%d Stream=%d len=%u\n"),
            rec->VolSessionId, rec->VolSessionTime, rec->FileIndex,
            rec->Stream, rec->data_len);
   }
   /*
    * Check for Start or End of Session Record
    *
    */
   if (rec->FileIndex < 0) {
      bool save_update_db = update_db;

      if (verbose > 1) {
         dump_label_record(dev, rec, 1, false);
      }
      switch (rec->FileIndex) {
      case PRE_LABEL:
         Pmsg0(000, _("Volume is prelabeled. This tape cannot be scanned.\n"));
         return false;
         break;

      case VOL_LABEL:
         unser_volume_label(dev, rec);
         /* Check Pool info */
         bstrncpy(pr.Name, dev->VolHdr.PoolName, sizeof(pr.Name));
         bstrncpy(pr.PoolType, dev->VolHdr.PoolType, sizeof(pr.PoolType));
         num_pools++;
         if (db_get_pool_numvols(bjcr, db, &pr)) {
            if (verbose) {
               Pmsg1(000, _("Pool record for %s found in DB.\n"), pr.Name);
            }
         } else {
            if (!update_db) {
               Pmsg1(000, _("VOL_LABEL: Pool record not found for Pool: %s\n"),
                  pr.Name);
            }
            create_pool_record(db, &pr);
         }
         if (strcmp(pr.PoolType, dev->VolHdr.PoolType) != 0) {
            Pmsg2(000, _("VOL_LABEL: PoolType mismatch. DB=%s Vol=%s\n"),
               pr.PoolType, dev->VolHdr.PoolType);
            return true;
         } else if (verbose) {
            Pmsg1(000, _("Pool type \"%s\" is OK.\n"), pr.PoolType);
         }

         /* Check Media Info */
         bmemset(&mr, 0, sizeof(mr));
         bstrncpy(mr.VolumeName, dev->VolHdr.VolumeName, sizeof(mr.VolumeName));
         mr.PoolId = pr.PoolId;
         num_media++;
         if (db_get_media_record(bjcr, db, &mr)) {
            if (verbose) {
               Pmsg1(000, _("Media record for %s found in DB.\n"), mr.VolumeName);
            }
            /* Clear out some volume statistics that will be updated */
            mr.VolJobs = mr.VolFiles = mr.VolBlocks = 0;
            mr.VolBytes = rec->data_len + 20;
         } else {
            if (!update_db) {
               Pmsg1(000, _("VOL_LABEL: Media record not found for Volume: %s\n"),
                  mr.VolumeName);
            }
            bstrncpy(mr.MediaType, dev->VolHdr.MediaType, sizeof(mr.MediaType));
            create_media_record(db, &mr, &dev->VolHdr);
         }
         if (strcmp(mr.MediaType, dev->VolHdr.MediaType) != 0) {
            Pmsg2(000, _("VOL_LABEL: MediaType mismatch. DB=%s Vol=%s\n"),
               mr.MediaType, dev->VolHdr.MediaType);
            return true;              /* ignore error */
         } else if (verbose) {
            Pmsg1(000, _("Media type \"%s\" is OK.\n"), mr.MediaType);
         }
         /* Reset some DCR variables */
         foreach_dlist(dcr, dev->attached_dcrs) {
            dcr->VolFirstIndex = dcr->FileIndex = 0;
            dcr->StartAddr = dcr->EndAddr = 0;
            dcr->VolMediaId = 0;
         }

         Pmsg1(000, _("VOL_LABEL: OK for Volume: %s\n"), mr.VolumeName);
         break;

      case SOS_LABEL:
         mr.VolJobs++;
         num_jobs++;
         if (ignored_msgs > 0) {
            Pmsg1(000, _("%d \"errors\" ignored before first Start of Session record.\n"),
                  ignored_msgs);
            ignored_msgs = 0;
         }
         unser_session_label(&label, rec);
         bmemset(&jr, 0, sizeof(jr));
         bstrncpy(jr.Job, label.Job, sizeof(jr.Job));
         if (db_get_job_record(bjcr, db, &jr)) {
            /* Job record already exists in DB */
            update_db = false;  /* don't change db in create_job_record */
            if (verbose) {
               Pmsg1(000, _("SOS_LABEL: Found Job record for JobId: %d\n"), jr.JobId);
            }
         } else {
            /* Must create a Job record in DB */
            if (!update_db) {
               Pmsg1(000, _("SOS_LABEL: Job record not found for JobId: %d\n"),
                  jr.JobId);
            }
         }

         /* Create Client record if not already there */
         bstrncpy(cr.Name, label.ClientName, sizeof(cr.Name));
         create_client_record(db, &cr);
         jr.ClientId = cr.ClientId;

         /* process label, if Job record exists don't update db */
         mjcr = create_job_record(db, &jr, &label, rec);
         dcr = mjcr->read_dcr;
         update_db = save_update_db;

         jr.PoolId = pr.PoolId;
         mjcr->start_time = jr.StartTime;
         mjcr->setJobLevel(jr.JobLevel);

         mjcr->client_name = get_pool_memory(PM_FNAME);
         pm_strcpy(mjcr->client_name, label.ClientName);
         mjcr->fileset_name = get_pool_memory(PM_FNAME);
         pm_strcpy(mjcr->fileset_name, label.FileSetName);
         bstrncpy(dcr->pool_type, label.PoolType, sizeof(dcr->pool_type));
         bstrncpy(dcr->pool_name, label.PoolName, sizeof(dcr->pool_name));

         /* Look for existing Job Media records for this job.  If there are
            any, no new ones need be created.  This may occur if File
            Retention has expired before Job Retention, or if the volume
            has already been bscan'd */
         Mmsg(sql_buffer, "SELECT count(*) from JobMedia where JobId=%d", jr.JobId);
         db_sql_query(db, sql_buffer.c_str(), db_int64_handler, &jmr_count);
         if( jmr_count.value > 0 ) {
            //FIELD NAME TO BE DEFINED/CONFIRMED (maybe a struct?)
            mjcr->bscan_insert_jobmedia_records = false;
         } else {
            mjcr->bscan_insert_jobmedia_records = true;
         }

         if (rec->VolSessionId != jr.VolSessionId) {
            Pmsg3(000, _("SOS_LABEL: VolSessId mismatch for JobId=%u. DB=%d Vol=%d\n"),
               jr.JobId,
               jr.VolSessionId, rec->VolSessionId);
            return true;              /* ignore error */
         }
         if (rec->VolSessionTime != jr.VolSessionTime) {
            Pmsg3(000, _("SOS_LABEL: VolSessTime mismatch for JobId=%u. DB=%d Vol=%d\n"),
               jr.JobId,
               jr.VolSessionTime, rec->VolSessionTime);
            return true;              /* ignore error */
         }
         if (jr.PoolId != pr.PoolId) {
            Pmsg3(000, _("SOS_LABEL: PoolId mismatch for JobId=%u. DB=%d Vol=%d\n"),
               jr.JobId,
               jr.PoolId, pr.PoolId);
            return true;              /* ignore error */
         }
         break;

      case EOS_LABEL:
         unser_session_label(&elabel, rec);

         /* Create FileSet record */
         bstrncpy(fsr.FileSet, label.FileSetName, sizeof(fsr.FileSet));
         bstrncpy(fsr.MD5, label.FileSetMD5, sizeof(fsr.MD5));
         create_fileset_record(db, &fsr);
         jr.FileSetId = fsr.FileSetId;

         mjcr = get_jcr_by_session(rec->VolSessionId, rec->VolSessionTime);
         if (!mjcr) {
            Pmsg2(000, _("Could not find SessId=%d SessTime=%d for EOS record.\n"),
                  rec->VolSessionId, rec->VolSessionTime);
            break;
         }

         /* Do the final update to the Job record */
         update_job_record(db, &jr, &elabel, rec);

         mjcr->end_time = jr.EndTime;
         mjcr->JobStatus = JS_Terminated;

         /* Create JobMedia record */
         mjcr->read_dcr->VolLastIndex = dcr->VolLastIndex;
         if( mjcr->bscan_insert_jobmedia_records ) {
            create_jobmedia_record(db, mjcr);
         }
         free_dcr(mjcr->read_dcr);
         free_jcr(mjcr);

         break;

      case EOM_LABEL:
         break;

      case EOT_LABEL:              /* end of all tapes */
         /*
          * Wiffle through all jobs still open and close
          *   them.
          */
         if (update_db) {
            DCR *mdcr;
            foreach_dlist(mdcr, dev->attached_dcrs) {
               JCR *mjcr = mdcr->jcr;
               if (!mjcr || mjcr->JobId == 0) {
                  continue;
               }
               jr.JobId = mjcr->JobId;
               /* Mark Job as Error Terimined */
               jr.JobStatus = JS_ErrorTerminated;
               jr.JobFiles = mjcr->JobFiles;
               jr.JobBytes = mjcr->JobBytes;
               jr.VolSessionId = mjcr->VolSessionId;
               jr.VolSessionTime = mjcr->VolSessionTime;
               jr.JobTDate = (utime_t)mjcr->start_time;
               jr.ClientId = mjcr->ClientId;
               if (!db_update_job_end_record(bjcr, db, &jr)) {
                  Pmsg1(0, _("Could not update job record. ERR=%s\n"), db_strerror(db));
               }
               mjcr->read_dcr = NULL;
               free_jcr(mjcr);
            }
         }
         mr.VolFiles = (uint32_t)(rec->Addr >> 32);
         mr.VolBlocks = (uint32_t)rec->Addr;
         mr.VolBytes += mr.VolBlocks * WRITE_BLKHDR_LENGTH; /* approx. */
         mr.VolMounts++;
         update_media_record(db, &mr);
         Pmsg3(0, _("End of all Volumes. VolFiles=%u VolBlocks=%u VolBytes=%s\n"), mr.VolFiles,
                    mr.VolBlocks, edit_uint64_with_commas(mr.VolBytes, ec1));
         break;
      case STREAM_PLUGIN_NAME:
         break;

      default:
         break;
      } /* end switch */
      return true;
   }

   mjcr = get_jcr_by_session(rec->VolSessionId, rec->VolSessionTime);
   if (!mjcr) {
      if (mr.VolJobs > 0) {
         Pmsg2(000, _("Could not find Job for SessId=%d SessTime=%d record.\n"),
                      rec->VolSessionId, rec->VolSessionTime);
      } else {
         ignored_msgs++;
      }
      return true;
   }
   dcr = mjcr->read_dcr;
   if (dcr->VolFirstIndex == 0) {
      dcr->VolFirstIndex = block->FirstIndex;
   }

   /* File Attributes stream */
   switch (rec->maskedStream) {
   case STREAM_UNIX_ATTRIBUTES:
   case STREAM_UNIX_ATTRIBUTES_EX:

      if (!unpack_attributes_record(bjcr, rec->Stream, rec->data, rec->data_len, attr)) {
         Emsg0(M_ERROR_TERM, 0, _("Cannot continue.\n"));
      }

      if (verbose > 1) {
         decode_stat(attr->attr, &attr->statp, sizeof(attr->statp), &attr->LinkFI);
         build_attr_output_fnames(bjcr, attr);
         print_ls_output(bjcr, attr);
      }
      fr.JobId = mjcr->JobId;
      fr.FileId = 0;
      num_files++;
      if (verbose && (num_files & 0x7FFF) == 0) {
         char ed1[30], ed2[30], ed3[30];
         Pmsg3(000, _("%s file records. At addr=%s bytes=%s\n"),
                     edit_uint64_with_commas(num_files, ed1),
                     edit_uint64_with_commas(rec->Addr, ed2),
                     edit_uint64_with_commas(mr.VolBytes, ed3));
      }
      create_file_attributes_record(db, mjcr, attr->fname, attr->lname,
            attr->type, attr->attr, rec);
      free_jcr(mjcr);
      break;

   case STREAM_RESTORE_OBJECT:
   /* ****FIXME*****/
      /* Implement putting into catalog */
      break;

   /* Data stream */
   case STREAM_WIN32_DATA:
   case STREAM_FILE_DATA:
   case STREAM_SPARSE_DATA:
   case STREAM_MACOS_FORK_DATA:
   case STREAM_ENCRYPTED_FILE_DATA:
   case STREAM_ENCRYPTED_WIN32_DATA:
   case STREAM_ENCRYPTED_MACOS_FORK_DATA:
      /*
       * For encrypted stream, this is an approximation.
       * The data must be decrypted to know the correct length.
       */
      mjcr->JobBytes += rec->data_len;
      if (rec->maskedStream == STREAM_SPARSE_DATA) {
         mjcr->JobBytes -= sizeof(uint64_t);
      }

      free_jcr(mjcr);                 /* done using JCR */
      break;

   case STREAM_GZIP_DATA:
   case STREAM_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_FILE_GZIP_DATA:
   case STREAM_ENCRYPTED_FILE_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_WIN32_GZIP_DATA:
   case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
      /* No correct, we should (decrypt and) expand it
         done using JCR
      */
      mjcr->JobBytes += rec->data_len;
      free_jcr(mjcr);
      break;

   case STREAM_SPARSE_GZIP_DATA:
   case STREAM_SPARSE_COMPRESSED_DATA:
      mjcr->JobBytes += rec->data_len - sizeof(uint64_t); /* No correct, we should expand it */
      free_jcr(mjcr);                 /* done using JCR */
      break;

   /* Win32 GZIP stream */
   case STREAM_WIN32_GZIP_DATA:
   case STREAM_WIN32_COMPRESSED_DATA:
      mjcr->JobBytes += rec->data_len;
      free_jcr(mjcr);                 /* done using JCR */
      break;

   case STREAM_MD5_DIGEST:
      bin_to_base64(digest, sizeof(digest), (char *)rec->data, CRYPTO_DIGEST_MD5_SIZE, true);
      if (verbose > 1) {
         Pmsg1(000, _("Got MD5 record: %s\n"), digest);
      }
      update_digest_record(db, digest, rec, CRYPTO_DIGEST_MD5);
      break;

   case STREAM_SHA1_DIGEST:
      bin_to_base64(digest, sizeof(digest), (char *)rec->data, CRYPTO_DIGEST_SHA1_SIZE, true);
      if (verbose > 1) {
         Pmsg1(000, _("Got SHA1 record: %s\n"), digest);
      }
      update_digest_record(db, digest, rec, CRYPTO_DIGEST_SHA1);
      break;

   case STREAM_SHA256_DIGEST:
      bin_to_base64(digest, sizeof(digest), (char *)rec->data, CRYPTO_DIGEST_SHA256_SIZE, true);
      if (verbose > 1) {
         Pmsg1(000, _("Got SHA256 record: %s\n"), digest);
      }
      update_digest_record(db, digest, rec, CRYPTO_DIGEST_SHA256);
      break;

   case STREAM_SHA512_DIGEST:
      bin_to_base64(digest, sizeof(digest), (char *)rec->data, CRYPTO_DIGEST_SHA512_SIZE, true);
      if (verbose > 1) {
         Pmsg1(000, _("Got SHA512 record: %s\n"), digest);
      }
      update_digest_record(db, digest, rec, CRYPTO_DIGEST_SHA512);
      break;

   case STREAM_ENCRYPTED_SESSION_DATA:
      // TODO landonf: Investigate crypto support in bscan
      if (verbose > 1) {
         Pmsg0(000, _("Got signed digest record\n"));
      }
      break;

   case STREAM_SIGNED_DIGEST:
      // TODO landonf: Investigate crypto support in bscan
      if (verbose > 1) {
         Pmsg0(000, _("Got signed digest record\n"));
      }
      break;

   case STREAM_PROGRAM_NAMES:
      if (verbose) {
         Pmsg1(000, _("Got Prog Names Stream: %s\n"), rec->data);
      }
      break;

   case STREAM_PROGRAM_DATA:
      if (verbose > 1) {
         Pmsg0(000, _("Got Prog Data Stream record.\n"));
      }
      break;

   case  STREAM_UNIX_ACCESS_ACL:          /* Deprecated Standard ACL attributes on UNIX */
   case  STREAM_UNIX_DEFAULT_ACL:         /* Deprecated Default ACL attributes on UNIX */
   case  STREAM_HFSPLUS_ATTRIBUTES:
   case  STREAM_XACL_AIX_TEXT:
   case  STREAM_XACL_DARWIN_ACCESS:
   case  STREAM_XACL_FREEBSD_DEFAULT:
   case  STREAM_XACL_FREEBSD_ACCESS:
   case  STREAM_XACL_HPUX_ACL_ENTRY:
   case  STREAM_XACL_IRIX_DEFAULT:
   case  STREAM_XACL_IRIX_ACCESS:
   case  STREAM_XACL_LINUX_DEFAULT:
   case  STREAM_XACL_LINUX_ACCESS:
   case  STREAM_XACL_TRU64_DEFAULT:
   case  STREAM_XACL_TRU64_DEFAULT_DIR:
   case  STREAM_XACL_TRU64_ACCESS:
   case  STREAM_XACL_SOLARIS_POSIX:
   case  STREAM_XACL_SOLARIS_NFS4:
   case  STREAM_XACL_AFS_TEXT:
   case  STREAM_XACL_AIX_AIXC:
   case  STREAM_XACL_AIX_NFS4:
   case  STREAM_XACL_FREEBSD_NFS4:
   case  STREAM_XACL_HURD_DEFAULT:
   case  STREAM_XACL_HURD_ACCESS:
      /* Ignore Unix ACL attributes */
      break;

   case  STREAM_XACL_HURD_XATTR:
   case  STREAM_XACL_IRIX_XATTR:
   case  STREAM_XACL_TRU64_XATTR:
   case  STREAM_XACL_AIX_XATTR:
   case  STREAM_XACL_OPENBSD_XATTR:
   case  STREAM_XACL_SOLARIS_SYS_XATTR:
   case  STREAM_XACL_SOLARIS_XATTR:
   case  STREAM_XACL_DARWIN_XATTR:
   case  STREAM_XACL_FREEBSD_XATTR:
   case  STREAM_XACL_LINUX_XATTR:
   case  STREAM_XACL_NETBSD_XATTR:
      /* Ignore Unix Extended attributes */
      break;

   default:
      Pmsg2(0, _("Unknown stream type!!! stream=%d len=%i\n"), rec->Stream, rec->data_len);
      break;
   }
   return true;
}

/*
 * Free the Job Control Record if no one is still using it.
 *  Called from main free_jcr() routine in src/lib/jcr.c so
 *  that we can do our Director specific cleanup of the jcr.
 */
static void bscan_free_jcr(JCR *jcr)
{
   Dmsg0(200, "Start bscan free_jcr\n");

   free_bsock(jcr->file_bsock);
   free_bsock(jcr->store_bsock);
   if (jcr->RestoreBootstrap) {
      bfree_and_null(jcr->RestoreBootstrap);
   }
   if (jcr->dcr) {
      free_dcr(jcr->dcr);
      jcr->dcr = NULL;
   }
   if (jcr->read_dcr) {
      free_dcr(jcr->read_dcr);
      jcr->read_dcr = NULL;
   }
   Dmsg0(200, "End bscan free_jcr\n");
}

/*
 * We got a File Attributes record on the tape.  Now, lookup the Job
 *   record, and then create the attributes record.
 */
static int create_file_attributes_record(BDB *db, JCR *mjcr,
                               char *fname, char *lname, int type,
                               char *ap, DEV_RECORD *rec)
{
   DCR *dcr = mjcr->read_dcr;
   ar.fname = fname;
   ar.link = lname;
   ar.ClientId = mjcr->ClientId;
   ar.JobId = mjcr->JobId;
   ar.Stream = rec->Stream;
   if (type == FT_DELETED) {
      ar.FileIndex = 0;
   } else {
      ar.FileIndex = rec->FileIndex;
   }
   ar.attr = ap;
   if (dcr->VolFirstIndex == 0) {
      dcr->VolFirstIndex = rec->FileIndex;
   }
   dcr->FileIndex = rec->FileIndex;
   mjcr->JobFiles++;

   if (!update_db) {
      return 1;
   }

   if (!db_create_file_attributes_record(bjcr, db, &ar)) {
      Pmsg1(0, _("Could not create File Attributes record. ERR=%s\n"), db_strerror(db));
      return 0;
   }
   mjcr->FileId = ar.FileId;

   if (verbose > 1) {
      Pmsg1(000, _("Created File record: %s\n"), fname);
   }
   return 1;
}

/*
 * For each Volume we see, we create a Medium record
 */
static int create_media_record(BDB *db, MEDIA_DBR *mr, VOLUME_LABEL *vl)
{
   struct date_time dt;
   struct tm tm;

   /* We mark Vols as Archive to keep them from being re-written */
   bstrncpy(mr->VolStatus, "Archive", sizeof(mr->VolStatus));
   mr->VolRetention = 365 * 3600 * 24; /* 1 year */
   mr->Enabled = 1;
   if (vl->VerNum >= 11) {
      mr->set_first_written = true; /* Save FirstWritten during update_media */
      mr->FirstWritten = btime_to_utime(vl->write_btime);
      mr->LabelDate    = btime_to_utime(vl->label_btime);
   } else {
      /* DEPRECATED DO NOT USE */
      dt.julian_day_number = vl->write_date;
      dt.julian_day_fraction = vl->write_time;
      tm_decode(&dt, &tm);
      mr->FirstWritten = mktime(&tm);
      dt.julian_day_number = vl->label_date;
      dt.julian_day_fraction = vl->label_time;
      tm_decode(&dt, &tm);
      mr->LabelDate = mktime(&tm);
   }
   lasttime = mr->LabelDate;
   if (mr->VolJobs == 0) {
      mr->VolJobs = 1;
   }
   if (mr->VolMounts == 0) {
      mr->VolMounts = 1;
   }

   if (!update_db) {
      return 1;
   }

   if (!db_create_media_record(bjcr, db, mr)) {
      Pmsg1(0, _("Could not create media record. ERR=%s\n"), db_strerror(db));
      return 0;
   }
   if (!db_update_media_record(bjcr, db, mr)) {
      Pmsg1(0, _("Could not update media record. ERR=%s\n"), db_strerror(db));
      return 0;
   }
   if (verbose) {
      Pmsg1(000, _("Created Media record for Volume: %s\n"), mr->VolumeName);
   }
   return 1;

}

/*
 * Called at end of media to update it
 */
static bool update_media_record(BDB *db, MEDIA_DBR *mr)
{
   if (!update_db && !update_vol_info) {
      return true;
   }

   mr->LastWritten = lasttime;
   if (!db_update_media_record(bjcr, db, mr)) {
      Pmsg1(0, _("Could not update media record. ERR=%s\n"), db_strerror(db));
      return false;;
   }
   if (verbose) {
      Pmsg1(000, _("Updated Media record at end of Volume: %s\n"), mr->VolumeName);
   }
   return true;

}


static int create_pool_record(BDB *db, POOL_DBR *pr)
{
   pr->NumVols++;
   pr->UseCatalog = 1;
   pr->VolRetention = 355 * 3600 * 24; /* 1 year */

   if (!update_db) {
      return 1;
   }
   if (!db_create_pool_record(bjcr, db, pr)) {
      Pmsg1(0, _("Could not create pool record. ERR=%s\n"), db_strerror(db));
      return 0;
   }
   if (verbose) {
      Pmsg1(000, _("Created Pool record for Pool: %s\n"), pr->Name);
   }
   return 1;

}


/*
 * Called from SOS to create a client for the current Job
 */
static int create_client_record(BDB *db, CLIENT_DBR *cr)
{
   /*
    * Note, update_db can temporarily be set false while
    * updating the database, so we must ensure that ClientId is non-zero.
    */
   if (!update_db) {
      cr->ClientId = 0;
      if (!db_get_client_record(bjcr, db, cr)) {
        Pmsg1(0, _("Could not get Client record. ERR=%s\n"), db_strerror(db));
        return 0;
      }
      return 1;
   }
   if (!db_create_client_record(bjcr, db, cr)) {
      Pmsg1(0, _("Could not create Client record. ERR=%s\n"), db_strerror(db));
      return 0;
   }
   if (verbose) {
      Pmsg1(000, _("Created Client record for Client: %s\n"), cr->Name);
   }
   return 1;
}

static int create_fileset_record(BDB *db, FILESET_DBR *fsr)
{
   if (!update_db) {
      return 1;
   }
   fsr->FileSetId = 0;
   if (fsr->MD5[0] == 0) {
      fsr->MD5[0] = ' ';              /* Equivalent to nothing */
      fsr->MD5[1] = 0;
   }
   if (db_get_fileset_record(bjcr, db, fsr)) {
      if (verbose) {
         Pmsg1(000, _("Fileset \"%s\" already exists.\n"), fsr->FileSet);
      }
   } else {
      if (!db_create_fileset_record(bjcr, db, fsr)) {
         Pmsg2(0, _("Could not create FileSet record \"%s\". ERR=%s\n"),
            fsr->FileSet, db_strerror(db));
         return 0;
      }
      if (verbose) {
         Pmsg1(000, _("Created FileSet record \"%s\"\n"), fsr->FileSet);
      }
   }
   return 1;
}

/*
 * Simulate the two calls on the database to create
 *  the Job record and to update it when the Job actually
 *  begins running.
 */
static JCR *create_job_record(BDB *db, JOB_DBR *jr, SESSION_LABEL *label,
                             DEV_RECORD *rec)
{
   JCR *mjcr;
   struct date_time dt;
   struct tm tm;

   jr->JobId = label->JobId;
   jr->JobType = label->JobType;
   jr->JobLevel = label->JobLevel;
   jr->JobStatus = JS_Created;
   bstrncpy(jr->Name, label->JobName, sizeof(jr->Name));
   bstrncpy(jr->Job, label->Job, sizeof(jr->Job));
   if (label->VerNum >= 11) {
      jr->SchedTime = btime_to_unix(label->write_btime);
   } else {
      dt.julian_day_number = label->write_date;
      dt.julian_day_fraction = label->write_time;
      tm_decode(&dt, &tm);
      jr->SchedTime = mktime(&tm);
   }

   jr->StartTime = jr->SchedTime;
   jr->JobTDate = (utime_t)jr->SchedTime;
   jr->VolSessionId = rec->VolSessionId;
   jr->VolSessionTime = rec->VolSessionTime;

   /* Now create a JCR as if starting the Job */
   mjcr = create_jcr(jr, rec, label->JobId);

   if (!update_db) {
      return mjcr;
   }

   /* This creates the bare essentials */
   if (!db_create_job_record(bjcr, db, jr)) {
      Pmsg1(0, _("Could not create JobId record. ERR=%s\n"), db_strerror(db));
      return mjcr;
   }

   /* This adds the client, StartTime, JobTDate, ... */
   if (!db_update_job_start_record(bjcr, db, jr)) {
      Pmsg1(0, _("Could not update job start record. ERR=%s\n"), db_strerror(db));
      return mjcr;
   }
   Pmsg2(000, _("Created new JobId=%u record for original JobId=%u\n"), jr->JobId,
         label->JobId);
   mjcr->JobId = jr->JobId;           /* set new JobId */
   return mjcr;
}

/*
 * Simulate the database call that updates the Job
 *  at Job termination time.
 */
static int update_job_record(BDB *db, JOB_DBR *jr, SESSION_LABEL *elabel,
                              DEV_RECORD *rec)
{
   struct date_time dt;
   struct tm tm;
   JCR *mjcr;

   mjcr = get_jcr_by_session(rec->VolSessionId, rec->VolSessionTime);
   if (!mjcr) {
      Pmsg2(000, _("Could not find SessId=%d SessTime=%d for EOS record.\n"),
                   rec->VolSessionId, rec->VolSessionTime);
      return 0;
   }
   if (elabel->VerNum >= 11) {
      jr->EndTime = btime_to_unix(elabel->write_btime);
   } else {
      dt.julian_day_number = elabel->write_date;
      dt.julian_day_fraction = elabel->write_time;
      tm_decode(&dt, &tm);
      jr->EndTime = mktime(&tm);
   }
   lasttime = jr->EndTime;
   mjcr->end_time = jr->EndTime;

   jr->JobId = mjcr->JobId;

   /* The JobStatus can't be 0 */
   if (elabel->JobStatus == 0) {
      Pmsg2(000, _("Could not find JobStatus for SessId=%d SessTime=%d in EOS record.\n"),
                   rec->VolSessionId, rec->VolSessionTime);
   }
   mjcr->JobStatus = jr->JobStatus =
      elabel->JobStatus ? elabel->JobStatus : JS_ErrorTerminated;

   jr->JobFiles = elabel->JobFiles;
   if (jr->JobFiles > 0) {  /* If we found files, force PurgedFiles */
      jr->PurgedFiles = 0;
   }
   jr->JobBytes = elabel->JobBytes;
   jr->VolSessionId = rec->VolSessionId;
   jr->VolSessionTime = rec->VolSessionTime;
   jr->JobTDate = (utime_t)mjcr->start_time;
   jr->ClientId = mjcr->ClientId;

   if (!update_db) {
      free_jcr(mjcr);
      return 1;
   }

   if (!db_update_job_end_record(bjcr, db, jr)) {
      Pmsg2(0, _("Could not update JobId=%u record. ERR=%s\n"), jr->JobId,  db_strerror(db));
      free_jcr(mjcr);
      return 0;
   }
   if (verbose) {
      Pmsg3(000, _("Updated Job termination record for JobId=%u Level=%s TermStat=%c\n"),
         jr->JobId, job_level_to_str(mjcr->getJobLevel()), jr->JobStatus);
   }
   if (verbose > 1) {
      const char *term_msg;
      static char term_code[70];
      char sdt[50], edt[50];
      char ec1[30], ec2[30], ec3[30];

      switch (mjcr->JobStatus) {
      case JS_Terminated:
         term_msg = _("Backup OK");
         break;
      case JS_Warnings:
         term_msg = _("Backup OK -- with warnings");
         break;
      case JS_FatalError:
      case JS_ErrorTerminated:
         term_msg = _("*** Backup Error ***");
         break;
      case JS_Canceled:
         term_msg = _("Backup Canceled");
         break;
      default:
         term_msg = term_code;
         sprintf(term_code, _("Job Termination code: %d"), mjcr->JobStatus);
         break;
      }
      bstrftime(sdt, sizeof(sdt), mjcr->start_time);
      bstrftime(edt, sizeof(edt), mjcr->end_time);
      Pmsg14(000,  _("%s\n"
"JobId:                  %d\n"
"Job:                    %s\n"
"FileSet:                %s\n"
"Backup Level:           %s\n"
"Client:                 %s\n"
"Start time:             %s\n"
"End time:               %s\n"
"Files Written:          %s\n"
"Bytes Written:          %s\n"
"Volume Session Id:      %d\n"
"Volume Session Time:    %d\n"
"Last Volume Bytes:      %s\n"
"Termination:            %s\n\n"),
        edt,
        mjcr->JobId,
        mjcr->Job,
        mjcr->fileset_name,
        job_level_to_str(mjcr->getJobLevel()),
        mjcr->client_name,
        sdt,
        edt,
        edit_uint64_with_commas(mjcr->JobFiles, ec1),
        edit_uint64_with_commas(mjcr->JobBytes, ec2),
        mjcr->VolSessionId,
        mjcr->VolSessionTime,
        edit_uint64_with_commas(mr.VolBytes, ec3),
        term_msg);
   }
   free_jcr(mjcr);
   return 1;
}

static int create_jobmedia_record(BDB *db, JCR *mjcr)
{
   JOBMEDIA_DBR jmr;
   DCR *dcr = mjcr->read_dcr;

   dcr->EndAddr = dev->EndAddr;
   dcr->VolMediaId = dev->VolCatInfo.VolMediaId;

   bmemset(&jmr, 0, sizeof(jmr));
   jmr.JobId = mjcr->JobId;
   jmr.MediaId = mr.MediaId;
   jmr.FirstIndex = dcr->VolFirstIndex;
   jmr.LastIndex = dcr->VolLastIndex;
   jmr.StartBlock = (uint32_t)dcr->StartAddr;
   jmr.StartFile = (uint32_t)(dcr->StartAddr >> 32);
   jmr.EndBlock = (uint32_t)dcr->EndAddr;
   jmr.EndFile = (uint32_t)(dcr->EndAddr >> 32);
   if (!update_db) {
      return 1;
   }

   if (!db_create_jobmedia_record(bjcr, db, &jmr)) {
      Pmsg1(0, _("Could not create JobMedia record. ERR=%s\n"), db_strerror(db));
      return 0;
   }
   if (verbose) {
      Pmsg2(000, _("Created JobMedia record JobId %d, MediaId %d\n"),
                jmr.JobId, jmr.MediaId);
   }
   return 1;
}

/*
 * Simulate the database call that updates the MD5/SHA1 record
 */
static int update_digest_record(BDB *db, char *digest, DEV_RECORD *rec, int type)
{
   JCR *mjcr;

   mjcr = get_jcr_by_session(rec->VolSessionId, rec->VolSessionTime);
   if (!mjcr) {
      if (mr.VolJobs > 0) {
         Pmsg2(000, _("Could not find SessId=%d SessTime=%d for MD5/SHA1 record.\n"),
                      rec->VolSessionId, rec->VolSessionTime);
      } else {
         ignored_msgs++;
      }
      return 0;
   }

   if (!update_db || mjcr->FileId == 0) {
      free_jcr(mjcr);
      return 1;
   }

   if (!db_add_digest_to_file_record(bjcr, db, mjcr->FileId, digest, type)) {
      Pmsg1(0, _("Could not add MD5/SHA1 to File record. ERR=%s\n"), db_strerror(db));
      free_jcr(mjcr);
      return 0;
   }
   if (verbose > 1) {
      Pmsg0(000, _("Updated MD5/SHA1 record\n"));
   }
   free_jcr(mjcr);
   return 1;
}


/*
 * Create a JCR as if we are really starting the job
 */
static JCR *create_jcr(JOB_DBR *jr, DEV_RECORD *rec, uint32_t JobId)
{
   JCR *jobjcr;
   /*
    * Transfer as much as possible to the Job JCR. Most important is
    *   the JobId and the ClientId.
    */
   jobjcr = new_jcr(sizeof(JCR), bscan_free_jcr);
   jobjcr->setJobType(jr->JobType);
   jobjcr->setJobLevel(jr->JobLevel);
   jobjcr->JobStatus = jr->JobStatus;
   bstrncpy(jobjcr->Job, jr->Job, sizeof(jobjcr->Job));
   jobjcr->JobId = JobId;      /* this is JobId on tape */
   jobjcr->sched_time = jr->SchedTime;
   jobjcr->start_time = jr->StartTime;
   jobjcr->VolSessionId = rec->VolSessionId;
   jobjcr->VolSessionTime = rec->VolSessionTime;
   jobjcr->ClientId = jr->ClientId;
   jobjcr->dcr = jobjcr->read_dcr = new_dcr(jobjcr, NULL, dev, SD_READ);

   return jobjcr;
}
