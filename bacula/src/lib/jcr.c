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
 * Manipulation routines for Job Control Records and
 *  handling of last_jobs_list.
 *
 *  Kern E. Sibbald, December 2000
 *
 *  These routines are thread safe.
 *
 *  The job list routines were re-written in May 2005 to
 *  eliminate the global lock while traversing the list, and
 *  to use the dlist subroutines.  The locking is now done
 *  on the list each time the list is modified or traversed.
 *  That is it is "micro-locked" rather than globally locked.
 *  The result is that there is one lock/unlock for each entry
 *  in the list while traversing it rather than a single lock
 *  at the beginning of a traversal and one at the end.  This
 *  incurs slightly more overhead, but effectively eliminates
 *  the possibilty of race conditions.  In addition, with the
 *  exception of the global locking of the list during the
 *  re-reading of the config file, no recursion is needed.
 *
 */

#include "bacula.h"
#include "jcr.h"

const int dbglvl = 3400;

/* External variables we reference */

/* External referenced functions */
void free_bregexps(alist *bregexps);

/* Forward referenced functions */
extern "C" void timeout_handler(int sig);
static void jcr_timeout_check(watchdog_t *self);
#ifdef TRACE_JCR_CHAIN
static void b_lock_jcr_chain(const char *filen, int line);
static void b_unlock_jcr_chain(const char *filen, int line);
#define lock_jcr_chain() b_lock_jcr_chain(__FILE__, __LINE__);
#define unlock_jcr_chain() b_unlock_jcr_chain(__FILE__, __LINE__);
#else
static void lock_jcr_chain();
static void unlock_jcr_chain();
#endif


int num_jobs_run;
dlist *last_jobs = NULL;
const int max_last_jobs = 10;

static dlist *jcrs = NULL;            /* JCR chain */
static pthread_mutex_t jcr_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t job_start_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t last_jobs_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_key_t jcr_key;         /* Pointer to jcr for each thread */

pthread_once_t key_once = PTHREAD_ONCE_INIT;

static char Job_status[] = "Status JobId=%ld JobStatus=%d\n";


void lock_jobs()
{
   P(job_start_mutex);
}

void unlock_jobs()
{
   V(job_start_mutex);
}

void init_last_jobs_list()
{
   JCR *jcr = NULL;
   struct s_last_job *job_entry = NULL;
   if (!last_jobs) {
      last_jobs = New(dlist(job_entry, &job_entry->link));
   }
   if (!jcrs) {
      jcrs = New(dlist(jcr, &jcr->link));
   }
}

void term_last_jobs_list()
{
   if (last_jobs) {
      lock_last_jobs_list();
      while (!last_jobs->empty()) {
         void *je = last_jobs->first();
         last_jobs->remove(je);
         free(je);
      }
      delete last_jobs;
      last_jobs = NULL;
      unlock_last_jobs_list();
   }
   if (jcrs) {
      delete jcrs;
      jcrs = NULL;
   }
}

bool read_last_jobs_list(int fd, uint64_t addr)
{
   struct s_last_job *je, job;
   uint32_t num;
   bool ok = true;

   Dmsg1(100, "read_last_jobs seek to %d\n", (int)addr);
   if (addr == 0 || lseek(fd, (boffset_t)addr, SEEK_SET) < 0) {
      return false;
   }
   if (read(fd, &num, sizeof(num)) != sizeof(num)) {
      return false;
   }
   Dmsg1(100, "Read num_items=%d\n", num);
   if (num > 4 * max_last_jobs) {  /* sanity check */
      return false;
   }
   lock_last_jobs_list();
   for ( ; num; num--) {
      if (read(fd, &job, sizeof(job)) != sizeof(job)) {
         berrno be;
         Pmsg1(000, "Read job entry. ERR=%s\n", be.bstrerror());
         ok = false;
         break;
      }
      if (job.JobId > 0) {
         je = (struct s_last_job *)malloc(sizeof(struct s_last_job));
         memcpy((char *)je, (char *)&job, sizeof(job));
         if (!last_jobs) {
            init_last_jobs_list();
         }
         last_jobs->append(je);
         if (last_jobs->size() > max_last_jobs) {
            je = (struct s_last_job *)last_jobs->first();
            last_jobs->remove(je);
            free(je);
         }
      }
   }
   unlock_last_jobs_list();
   return ok;
}

uint64_t write_last_jobs_list(int fd, uint64_t addr)
{
   struct s_last_job *je;
   uint32_t num;
   ssize_t stat;

   Dmsg1(100, "write_last_jobs seek to %d\n", (int)addr);
   if (lseek(fd, (boffset_t)addr, SEEK_SET) < 0) {
      return 0;
   }
   if (last_jobs) {
      lock_last_jobs_list();
      /* First record is number of entires */
      num = last_jobs->size();
      if (write(fd, &num, sizeof(num)) != sizeof(num)) {
         berrno be;
         Pmsg1(000, "Error writing num_items: ERR=%s\n", be.bstrerror());
         goto bail_out;
      }
      foreach_dlist(je, last_jobs) {
         if (write(fd, je, sizeof(struct s_last_job)) != sizeof(struct s_last_job)) {
            berrno be;
            Pmsg1(000, "Error writing job: ERR=%s\n", be.bstrerror());
            goto bail_out;
         }
      }
      unlock_last_jobs_list();
   }
   /* Return current address */
   stat = lseek(fd, 0, SEEK_CUR);
   if (stat < 0) {
      stat = 0;
   }
   return stat;

bail_out:
   unlock_last_jobs_list();
   return 0;
}

void lock_last_jobs_list()
{
   P(last_jobs_mutex);
}

void unlock_last_jobs_list()
{
   V(last_jobs_mutex);
}

/* Get an ASCII representation of the Operation being performed as an english Noun */
const char *JCR::get_OperationName()
{
   switch(m_JobType) {
   case JT_BACKUP:
      return _("Backup");
   case JT_VERIFY:
      return _("Verifying");
   case JT_RESTORE:
      return _("Restoring");
   case JT_ARCHIVE:
      return _("Archiving");
   case JT_COPY:
      return _("Copying");
   case JT_MIGRATE:
      return _("Migration");
   case JT_SCAN:
      return _("Scanning");
   default:
      return _("Unknown operation");
   }
}

/* Get an ASCII representation of the Action being performed either an english Verb or Adjective */
const char *JCR::get_ActionName(bool past)
{
   switch(m_JobType) {
   case JT_BACKUP:
      return _("backup");
   case JT_VERIFY:
      return (past == true) ? _("verified") : _("verify");
   case JT_RESTORE:
      return (past == true) ? _("restored") : _("restore");
   case JT_ARCHIVE:
      return (past == true) ? _("archived") : _("archive");
   case JT_COPY:
      return (past == true) ? _("copied") : _("copy");
   case JT_MIGRATE:
      return (past == true) ? _("migrated") : _("migrate");
   case JT_SCAN:
      return (past == true) ? _("scanned") : _("scan");
   default:
      return _("unknown action");
   }
}

bool JCR::JobReads()
{
   switch (m_JobType) {
   case JT_VERIFY:
   case JT_RESTORE:
   case JT_COPY:
   case JT_MIGRATE:
      return true;
   case JT_BACKUP:
      if (m_JobLevel == L_VIRTUAL_FULL) {
         return true;
      }
      break;
   default:
      break;
   }
   return false;
}

/* We can stop only Backup jobs connected to a client. It doesn't make sens at
 * this time to stop a copy, migraton, restore or a verify job. The specific
 * code should be implemented first.
 */
bool JCR::can_be_stopped()
{
   bool ok=true;
   if (getJobType() == JT_BACKUP) {          /* Is a Backup */
      if (getJobLevel() == L_VIRTUAL_FULL) { /* Is a VirtualFull */
         ok = false;
      }
   } else {   /* Is not a backup (so, copy, migration, admin, verify, ... */
      ok = false;
   }
   return ok;
}

/*
 * Push a subroutine address into the job end callback stack
 */
void job_end_push(JCR *jcr, void job_end_cb(JCR *jcr,void *), void *ctx)
{
   jcr->job_end_push.append((void *)job_end_cb);
   jcr->job_end_push.append(ctx);
}

/* DELETE ME when bugs in MA1512, MA1632 MA1639 are fixed */
void (*MA1512_reload_job_end_cb)(JCR *,void *) = NULL;

/* Pop each job_end subroutine and call it */
static void job_end_pop(JCR *jcr)
{
   void (*job_end_cb)(JCR *jcr, void *ctx);
   void *ctx;
   for (int i=jcr->job_end_push.size()-1; i > 0; ) {
      ctx = jcr->job_end_push.get(i--);
      job_end_cb = (void (*)(JCR *,void *))jcr->job_end_push.get(i--);
      /* check for bug MA1512, MA1632 MA1639,
       * today, job_end_cb can only be reload_job_end_cb() from DIR */
      if (job_end_cb != MA1512_reload_job_end_cb && MA1512_reload_job_end_cb != NULL) {
         Tmsg2(0, "Bug 'job_end_pop' detected, skip ! job_end_cb=0x%p ctx=0x%p\n", job_end_cb, ctx);
         Tmsg0(0, "Display job_end_push list\n");
         for (int j=jcr->job_end_push.size()-1; j > 0; ) {
            void *ctx2 = jcr->job_end_push.get(j--);
            void *job_end_cb2 = jcr->job_end_push.get(j--);
            Tmsg3(0, "Bug 'job_end_pop' entry[%d] job_end_cb=0x%p ctx=0x%p\n", j+1, job_end_cb2, ctx2);
         }
      } else
      {
         job_end_cb(jcr, ctx);
      }
   }
}

/*
 * Create thread key for thread specific data
 */
void create_jcr_key()
{
   int status = pthread_key_create(&jcr_key, NULL);
   if (status != 0) {
      berrno be;
      Jmsg1(NULL, M_ABORT, 0, _("pthread key create failed: ERR=%s\n"),
            be.bstrerror(status));
   }
}

/*
 * Create a Job Control Record and link it into JCR chain
 * Returns newly allocated JCR
 * Note, since each daemon has a different JCR, he passes
 *  us the size.
 */
JCR *new_jcr(int size, JCR_free_HANDLER *daemon_free_jcr)
{
   JCR *jcr;
   MQUEUE_ITEM *item = NULL;
   int status;

   Dmsg0(dbglvl, "Enter new_jcr\n");
   status = pthread_once(&key_once, create_jcr_key);
   if (status != 0) {
      berrno be;
      Jmsg1(NULL, M_ABORT, 0, _("pthread_once failed. ERR=%s\n"), be.bstrerror(status));
   }
   jcr = (JCR *)malloc(size);
   bmemzero(jcr, size);
   /* Note for the director, this value is changed in jobq.c */
   jcr->my_thread_id = pthread_self();
   jcr->msg_queue = New(dlist(item, &item->link));
   if ((status = pthread_mutex_init(&jcr->msg_queue_mutex, NULL)) != 0) {
      berrno be;
      Jmsg(NULL, M_ABORT, 0, _("Could not init msg_queue mutex. ERR=%s\n"),
         be.bstrerror(status));
   }
   jcr->job_end_push.init(1, false);
   jcr->sched_time = time(NULL);
   jcr->initial_sched_time = jcr->sched_time;
   jcr->daemon_free_jcr = daemon_free_jcr;    /* plug daemon free routine */
   jcr->init_mutex();
   jcr->inc_use_count();
   jcr->VolumeName = get_pool_memory(PM_FNAME);
   jcr->VolumeName[0] = 0;
   jcr->errmsg = get_pool_memory(PM_MESSAGE);
   jcr->errmsg[0] = 0;
   jcr->comment = get_pool_memory(PM_FNAME);
   jcr->comment[0] = 0;
   jcr->StatusErrMsg = get_pool_memory(PM_FNAME);
   jcr->StatusErrMsg[0] = 0;
   jcr->job_uid = -1;
   /* Setup some dummy values */
   bstrncpy(jcr->Job, "*System*", sizeof(jcr->Job));
   jcr->JobId = 0;
   jcr->setJobType(JT_SYSTEM);           /* internal job until defined */
   jcr->setJobLevel(L_NONE);
   jcr->setJobStatus(JS_Created);        /* ready to run */
#ifndef HAVE_WIN32
   struct sigaction sigtimer;
   sigtimer.sa_flags = 0;
   sigtimer.sa_handler = timeout_handler;
   sigfillset(&sigtimer.sa_mask);
   sigaction(TIMEOUT_SIGNAL, &sigtimer, NULL);
#endif

   /*
    * Locking jobs is a global lock that is needed
    * so that the Director can stop new jobs from being
    * added to the jcr chain while it processes a new
    * conf file and does the job_end_push().
    */
   lock_jobs();
   lock_jcr_chain();
   if (!jcrs) {
      jcrs = New(dlist(jcr, &jcr->link));
   }
   jcrs->append(jcr);
   unlock_jcr_chain();
   unlock_jobs();

   return jcr;
}


/*
 * Remove a JCR from the chain
 * NOTE! The chain must be locked prior to calling
 *       this routine.
 */
static void remove_jcr(JCR *jcr)
{
   Dmsg0(dbglvl, "Enter remove_jcr\n");
   if (!jcr) {
      Emsg0(M_ABORT, 0, _("NULL jcr.\n"));
   }
   jcrs->remove(jcr);
   Dmsg0(dbglvl, "Leave remove_jcr\n");
}

/*
 * Free stuff common to all JCRs.  N.B. Be careful to include only
 *  generic stuff in the common part of the jcr.
 */
static void free_common_jcr(JCR *jcr)
{
   /* Uses jcr lock/unlock */
   remove_jcr_from_tsd(jcr);
   jcr->set_killable(false);

   jcr->destroy_mutex();

   if (jcr->msg_queue) {
      delete jcr->msg_queue;
      jcr->msg_queue = NULL;
      pthread_mutex_destroy(&jcr->msg_queue_mutex);
   }

   /* do this after closing messages */
   free_and_null_pool_memory(jcr->JobIds);
   free_and_null_pool_memory(jcr->client_name);
   free_and_null_pool_memory(jcr->attr);
   free_and_null_pool_memory(jcr->VolumeName);
   free_and_null_pool_memory(jcr->errmsg);
   free_and_null_pool_memory(jcr->StatusErrMsg);

   if (jcr->sd_auth_key) {
      free(jcr->sd_auth_key);
      jcr->sd_auth_key = NULL;
   }

   free_bsock(jcr->dir_bsock);

   if (jcr->where) {
      free(jcr->where);
      jcr->where = NULL;
   }
   if (jcr->RegexWhere) {
      free(jcr->RegexWhere);
      jcr->RegexWhere = NULL;
   }
   if (jcr->where_bregexp) {
      free_bregexps(jcr->where_bregexp);
      delete jcr->where_bregexp;
      jcr->where_bregexp = NULL;
   }
   if (jcr->cached_path) {
      free_pool_memory(jcr->cached_path);
      jcr->cached_path = NULL;
      jcr->cached_pnl = 0;
   }
   if (jcr->id_list) {
      free_guid_list(jcr->id_list);
      jcr->id_list = NULL;
   }
   if (jcr->comment) {
      free_pool_memory(jcr->comment);
      jcr->comment = NULL;
   }
   free(jcr);
}

/*
 * Global routine to free a jcr
 */
#ifdef DEBUG
void b_free_jcr(const char *file, int line, JCR *jcr)
{
   struct s_last_job *je;

   Dmsg3(dbglvl, "Enter free_jcr jid=%u from %s:%d\n", jcr->JobId, file, line);

#else

void free_jcr(JCR *jcr)
{
   struct s_last_job *je;

   Dmsg3(dbglvl, "Enter free_jcr jid=%u use_count=%d Job=%s\n",
         jcr->JobId, jcr->use_count(), jcr->Job);

#endif

   lock_jcr_chain();
   jcr->dec_use_count();              /* decrement use count */
   ASSERT2(jcr->use_count() >= 0, "JCR use_count < 0");
   //    Jmsg2(jcr, M_ERROR, 0, _("JCR use_count=%d JobId=%d\n"),
   //      jcr->use_count(), jcr->JobId);
   //}
   if (jcr->JobId > 0) {
      Dmsg3(dbglvl, "Dec free_jcr jid=%u use_count=%d Job=%s\n",
         jcr->JobId, jcr->use_count(), jcr->Job);
   }
   if (jcr->use_count() > 0) {          /* if in use */
      unlock_jcr_chain();
      return;
   }
   if (jcr->JobId > 0) {
      Dmsg3(dbglvl, "remove jcr jid=%u use_count=%d Job=%s\n",
            jcr->JobId, jcr->use_count(), jcr->Job);
   }
   jcr->exiting = true;
   remove_jcr(jcr);                   /* remove Jcr from chain */
   unlock_jcr_chain();

   if (jcr->JobId > 0) {
      dequeue_messages(jcr);
      dequeue_daemon_messages(jcr);
   }
   close_msg(jcr);                    /* close messages for this job */
   job_end_pop(jcr);                  /* pop and call hooked routines */

   Dmsg1(dbglvl, "End job=%d\n", jcr->JobId);

   /* Keep some statistics */
   switch (jcr->getJobType()) {
   case JT_BACKUP:
   case JT_VERIFY:
   case JT_RESTORE:
   case JT_MIGRATE:
   case JT_COPY:
   case JT_ADMIN:
      /* Keep list of last jobs, but not Console where JobId==0 */
      if (jcr->JobId > 0) {
         lock_last_jobs_list();
         num_jobs_run++;
         je = (struct s_last_job *)malloc(sizeof(struct s_last_job));
         memset(je, 0, sizeof(struct s_last_job));  /* zero in case unset fields */
         je->Errors = jcr->JobErrors;
         je->JobType = jcr->getJobType();
         je->JobId = jcr->JobId;
         je->VolSessionId = jcr->VolSessionId;
         je->VolSessionTime = jcr->VolSessionTime;
         bstrncpy(je->Job, jcr->Job, sizeof(je->Job));
         je->JobFiles = jcr->JobFiles;
         je->JobBytes = jcr->JobBytes;
         je->JobStatus = jcr->JobStatus;
         je->JobLevel = jcr->getJobLevel();
         je->start_time = jcr->start_time;
         je->end_time = time(NULL);

         if (!last_jobs) {
            init_last_jobs_list();
         }
         last_jobs->append(je);
         if (last_jobs->size() > max_last_jobs) {
            je = (struct s_last_job *)last_jobs->first();
            last_jobs->remove(je);
            free(je);
         }
         unlock_last_jobs_list();
      }
      break;
   default:
      break;
   }

   if (jcr->daemon_free_jcr) {
      jcr->daemon_free_jcr(jcr);      /* call daemon free routine */
   }

   free_common_jcr(jcr);
   close_msg(NULL);                   /* flush any daemon messages */
   Dmsg0(dbglvl, "Exit free_jcr\n");
}

/*
 * Remove jcr from thread specific data, but
 *   but make sure it is us who are attached.
 */
void remove_jcr_from_tsd(JCR *jcr)
{
   JCR *tjcr = get_jcr_from_tsd();
   if (tjcr == jcr) {
      set_jcr_in_tsd(INVALID_JCR);
   }
}

void JCR::set_killable(bool killable)
{
   lock();
   my_thread_killable = killable;
   unlock();
}

/*
 * Put this jcr in the thread specifc data
 *  if update_thread_info is true and the jcr is valide,
 *  we update the my_thread_id in the JCR
 */
void set_jcr_in_tsd(JCR *jcr)
{
   int status = pthread_setspecific(jcr_key, (void *)jcr);
   if (status != 0) {
      berrno be;
      Jmsg1(jcr, M_ABORT, 0, _("pthread_setspecific failed: ERR=%s\n"),
            be.bstrerror(status));
   }
}

void JCR::my_thread_send_signal(int sig)
{
   lock_jcr_chain();   /* use global lock */
   this->lock();
   if (this->exiting) {
      goto get_out;
   }
   if (this->is_killable() &&
       !pthread_equal(this->my_thread_id, pthread_self()))
   {
      Dmsg1(800, "Send kill to jid=%d\n", this->JobId);
      pthread_kill(this->my_thread_id, sig);
      this->exiting = true;

   } else if (!this->is_killable()) {
      Dmsg1(10, "Warning, cannot send kill to jid=%d marked not killable.\n", this->JobId);
   }
get_out:
   this->unlock();
   unlock_jcr_chain();
}

/*
 * Give me the jcr that is attached to this thread
 */
JCR *get_jcr_from_tsd()
{
   JCR *jcr = (JCR *)pthread_getspecific(jcr_key);
// printf("get_jcr_from_tsd: jcr=%p\n", jcr);
   /* set any INVALID_JCR to NULL which the rest of Bacula understands */
   if (jcr == INVALID_JCR) {
      jcr = NULL;
   }
   return jcr;
}


/*
 * Find which JobId corresponds to the current thread
 */
uint32_t get_jobid_from_tsd()
{
   JCR *jcr;
   uint32_t JobId = 0;
   jcr = get_jcr_from_tsd();
// printf("get_jobid_from_tsr: jcr=%p\n", jcr);
   if (jcr) {
      JobId = (uint32_t)jcr->JobId;
   }
   return JobId;
}

/*
 * Given a JobId, find the JCR
 *   Returns: jcr on success
 *            NULL on failure
 */
JCR *get_jcr_by_id(uint32_t JobId)
{
   JCR *jcr;

   foreach_jcr(jcr) {
      if (jcr->JobId == JobId) {
         jcr->inc_use_count();
         Dmsg3(dbglvl, "Inc get_jcr jid=%u use_count=%d Job=%s\n",
            jcr->JobId, jcr->use_count(), jcr->Job);
         break;
      }
   }
   endeach_jcr(jcr);
   return jcr;
}

/*
 * Given a thread id, find the JobId
 *   Returns: JobId on success
 *            0 on failure
 */
uint32_t get_jobid_from_tid(pthread_t tid)
{
   JCR *jcr = NULL;
   bool found = false;

   foreach_jcr(jcr) {
      if (pthread_equal(jcr->my_thread_id, tid)) {
         found = true;
         break;
      }
   }
   endeach_jcr(jcr);
   if (found) {
      return jcr->JobId;
   }
   return 0;
}


/*
 * Given a SessionId and SessionTime, find the JCR
 *   Returns: jcr on success
 *            NULL on failure
 */
JCR *get_jcr_by_session(uint32_t SessionId, uint32_t SessionTime)
{
   JCR *jcr;

   foreach_jcr(jcr) {
      if (jcr->VolSessionId == SessionId &&
          jcr->VolSessionTime == SessionTime) {
         jcr->inc_use_count();
         Dmsg3(dbglvl, "Inc get_jcr jid=%u use_count=%d Job=%s\n",
            jcr->JobId, jcr->use_count(), jcr->Job);
         break;
      }
   }
   endeach_jcr(jcr);
   return jcr;
}


/*
 * Given a Job, find the JCR
 *  compares on the number of characters in Job
 *  thus allowing partial matches.
 *   Returns: jcr on success
 *            NULL on failure
 */
JCR *get_jcr_by_partial_name(char *Job)
{
   JCR *jcr;
   int len;

   if (!Job) {
      return NULL;
   }
   len = strlen(Job);
   foreach_jcr(jcr) {
      if (strncmp(Job, jcr->Job, len) == 0) {
         jcr->inc_use_count();
         Dmsg3(dbglvl, "Inc get_jcr jid=%u use_count=%d Job=%s\n",
            jcr->JobId, jcr->use_count(), jcr->Job);
         break;
      }
   }
   endeach_jcr(jcr);
   return jcr;
}


/*
 * Given a Job, find the JCR
 *  requires an exact match of names.
 *   Returns: jcr on success
 *            NULL on failure
 */
JCR *get_jcr_by_full_name(char *Job)
{
   JCR *jcr;

   if (!Job) {
      return NULL;
   }
   foreach_jcr(jcr) {
      if (strcmp(jcr->Job, Job) == 0) {
         jcr->inc_use_count();
         Dmsg3(dbglvl, "Inc get_jcr jid=%u use_count=%d Job=%s\n",
            jcr->JobId, jcr->use_count(), jcr->Job);
         break;
      }
   }
   endeach_jcr(jcr);
   return jcr;
}

static void update_wait_time(JCR *jcr, int newJobStatus)
{
   bool enter_in_waittime;
   int oldJobStatus = jcr->JobStatus;

   switch (newJobStatus) {
   case JS_WaitFD:
   case JS_WaitSD:
   case JS_WaitMedia:
   case JS_WaitMount:
   case JS_WaitStoreRes:
   case JS_WaitJobRes:
   case JS_WaitClientRes:
   case JS_WaitMaxJobs:
   case JS_WaitPriority:
      enter_in_waittime = true;
      break;
   default:
      enter_in_waittime = false; /* not a Wait situation */
      break;
   }

   /*
    * If we were previously waiting and are not any more
    *   we want to update the wait_time variable, which is
    *   the start of waiting.
    */
   switch (oldJobStatus) {
   case JS_WaitFD:
   case JS_WaitSD:
   case JS_WaitMedia:
   case JS_WaitMount:
   case JS_WaitStoreRes:
   case JS_WaitJobRes:
   case JS_WaitClientRes:
   case JS_WaitMaxJobs:
   case JS_WaitPriority:
      if (!enter_in_waittime) { /* we get out the wait time */
         jcr->wait_time_sum += (time(NULL) - jcr->wait_time);
         jcr->wait_time = 0;
      }
      break;

   /* if wait state is new, we keep current time for watchdog MaxWaitTime */
   default:
      if (enter_in_waittime) {
         jcr->wait_time = time(NULL);
      }
      break;
   }
}

/*
 * Priority runs from 0 (lowest) to 10 (highest)
 */
static int get_status_priority(int JobStatus)
{
   int priority = 0;
   switch (JobStatus) {
   case JS_Incomplete:
      priority = 10;
      break;
   case JS_ErrorTerminated:
   case JS_FatalError:
   case JS_Canceled:
      priority = 9;
      break;
   case JS_Error:
      priority = 8;
      break;
   case JS_Differences:
      priority = 7;
      break;
   }
   return priority;
}

/*
 * Send Job status to Director
 */
bool JCR::sendJobStatus()
{
   if (dir_bsock) {
      return dir_bsock->fsend(Job_status, JobId, JobStatus);
   }
   return true;
}

/*
 * Set and send Job status to Director
 */
bool JCR::sendJobStatus(int aJobStatus)
{
   if (!is_JobStatus(aJobStatus)) {
      setJobStatus(aJobStatus);
      if (dir_bsock) {
         return dir_bsock->fsend(Job_status, JobId, JobStatus);
      }
   }
   return true;
}

void JCR::setJobStarted()
{
   job_started = true;
   job_started_time = time(NULL);
}

static pthread_mutex_t status_lock = PTHREAD_MUTEX_INITIALIZER;

void JCR::setJobStatus(int newJobStatus)
{
   int priority, old_priority;
   int oldJobStatus = JobStatus;

   P(status_lock);
   priority = get_status_priority(newJobStatus);
   old_priority = get_status_priority(oldJobStatus);

   Dmsg2(800, "set_jcr_job_status(%ld, %c)\n", JobId, newJobStatus);

   /* Update wait_time depending on newJobStatus and oldJobStatus */
   update_wait_time(this, newJobStatus);

   /*
    * For a set of errors, ... keep the current status
    *   so it isn't lost. For all others, set it.
    */
   Dmsg2(800, "OnEntry JobStatus=%c newJobstatus=%c\n", (oldJobStatus==0)?'0':oldJobStatus, newJobStatus);
   /*
    * If status priority is > than proposed new status, change it.
    * If status priority == new priority and both are zero, take
    *   the new status.
    * If it is not zero, then we keep the first non-zero "error" that
    *   occurred.
    */
   if (priority > old_priority || (
       priority == 0 && old_priority == 0)) {
      Dmsg4(800, "Set new stat. old: %c,%d new: %c,%d\n",
            (oldJobStatus==0)?'0':oldJobStatus, old_priority, newJobStatus, priority);
      JobStatus = newJobStatus;     /* replace with new status */
   }

   if (oldJobStatus != JobStatus) {
      Dmsg2(800, "leave setJobStatus old=%c new=%c\n", (oldJobStatus==0)?'0':oldJobStatus, newJobStatus);
//    generate_plugin_event(this, bEventStatusChange, NULL);
   }
   V(status_lock);
}

#ifdef TRACE_JCR_CHAIN
static int lock_count = 0;
#endif

/*
 * Lock the chain
 */
#ifdef TRACE_JCR_CHAIN
static void b_lock_jcr_chain(const char *fname, int line)
#else
static void lock_jcr_chain()
#endif
{
#ifdef TRACE_JCR_CHAIN
   Dmsg3(dbglvl, "Lock jcr chain %d from %s:%d\n", ++lock_count, fname, line);
#endif
   P(jcr_lock);
}

/*
 * Unlock the chain
 */
#ifdef TRACE_JCR_CHAIN
static void b_unlock_jcr_chain(const char *fname, int line)
#else
static void unlock_jcr_chain()
#endif
{
#ifdef TRACE_JCR_CHAIN
   Dmsg3(dbglvl, "Unlock jcr chain %d from %s:%d\n", lock_count--, fname, line);
#endif
   V(jcr_lock);
}

/*
 * Start walk of jcr chain
 * The proper way to walk the jcr chain is:
 *    JCR *jcr;
 *    foreach_jcr(jcr) {
 *      ...
 *    }
 *    endeach_jcr(jcr);
 *
 *  It is possible to leave out the endeach_jcr(jcr), but
 *   in that case, the last jcr referenced must be explicitly
 *   released with:
 *
 *    free_jcr(jcr);
 *
 */
JCR *jcr_walk_start()
{
   JCR *jcr;
   lock_jcr_chain();
   jcr = (JCR *)jcrs->first();
   if (jcr) {
      jcr->inc_use_count();
      if (jcr->JobId > 0) {
         Dmsg3(dbglvl, "Inc walk_start jid=%u use_count=%d Job=%s\n",
            jcr->JobId, jcr->use_count(), jcr->Job);
      }
   }
   unlock_jcr_chain();
   return jcr;
}

/*
 * Get next jcr from chain, and release current one
 */
JCR *jcr_walk_next(JCR *prev_jcr)
{
   JCR *jcr;

   lock_jcr_chain();
   jcr = (JCR *)jcrs->next(prev_jcr);
   if (jcr) {
      jcr->inc_use_count();
      if (jcr->JobId > 0) {
         Dmsg3(dbglvl, "Inc walk_next jid=%u use_count=%d Job=%s\n",
            jcr->JobId, jcr->use_count(), jcr->Job);
      }
   }
   unlock_jcr_chain();
   if (prev_jcr) {
      free_jcr(prev_jcr);
   }
   return jcr;
}

/*
 * Release last jcr referenced
 */
void jcr_walk_end(JCR *jcr)
{
   if (jcr) {
      if (jcr->JobId > 0) {
         Dmsg3(dbglvl, "Free walk_end jid=%u use_count=%d Job=%s\n",
            jcr->JobId, jcr->use_count(), jcr->Job);
      }
      free_jcr(jcr);
   }
}

/*
 * Return number of Jobs
 */
int job_count()
{
   JCR *jcr;
   int count = 0;

   lock_jcr_chain();
   for (jcr = (JCR *)jcrs->first(); jcr ; jcr = (JCR *)jcrs->next(jcr)) {
      if (jcr->JobId > 0) {
         count++;
      }
   }
   unlock_jcr_chain();
   return count;
}


/*
 * Setup to call the timeout check routine every 30 seconds
 *  This routine will check any timers that have been enabled.
 */
bool init_jcr_subsystem(void)
{
   watchdog_t *wd = new_watchdog();

   wd->one_shot = false;
   wd->interval = 30;   /* FIXME: should be configurable somewhere, even
                         if only with a #define */
   wd->callback = jcr_timeout_check;

   register_watchdog(wd);

   return true;
}

static void jcr_timeout_check(watchdog_t *self)
{
   JCR *jcr;
   BSOCK *bs;
   time_t timer_start;

   Dmsg0(dbglvl, "Start JCR timeout checks\n");

   /* Walk through all JCRs checking if any one is
    * blocked for more than specified max time.
    */
   foreach_jcr(jcr) {
      Dmsg2(dbglvl, "jcr_timeout_check JobId=%u jcr=0x%x\n", jcr->JobId, jcr);
      if (jcr->JobId == 0) {
         continue;
      }
      bs = jcr->store_bsock;
      if (bs) {
         timer_start = bs->timer_start;
         if (timer_start && (watchdog_time - timer_start) > bs->timeout) {
            bs->timer_start = 0;      /* turn off timer */
            bs->set_timed_out();
            Qmsg(jcr, M_ERROR, 0, _(
"Watchdog sending kill after %d secs to thread stalled reading Storage daemon.\n"),
                 (int)(watchdog_time - timer_start));
            jcr->my_thread_send_signal(TIMEOUT_SIGNAL);
         }
      }
      bs = jcr->file_bsock;
      if (bs) {
         timer_start = bs->timer_start;
         if (timer_start && (watchdog_time - timer_start) > bs->timeout) {
            bs->timer_start = 0;      /* turn off timer */
            bs->set_timed_out();
            Qmsg(jcr, M_ERROR, 0, _(
"Watchdog sending kill after %d secs to thread stalled reading File daemon.\n"),
                 (int)(watchdog_time - timer_start));
            jcr->my_thread_send_signal(TIMEOUT_SIGNAL);
         }
      }
      bs = jcr->dir_bsock;
      if (bs) {
         timer_start = bs->timer_start;
         if (timer_start && (watchdog_time - timer_start) > bs->timeout) {
            bs->timer_start = 0;      /* turn off timer */
            bs->set_timed_out();
            Qmsg(jcr, M_ERROR, 0, _(
"Watchdog sending kill after %d secs to thread stalled reading Director.\n"),
                 (int)(watchdog_time - timer_start));
            jcr->my_thread_send_signal(TIMEOUT_SIGNAL);
         }
      }
   }
   endeach_jcr(jcr);

   Dmsg0(dbglvl, "Finished JCR timeout checks\n");
}

/*
 * Return next JobId from comma separated list
 *
 * Returns:
 *   1 if next JobId returned
 *   0 if no more JobIds are in list
 *  -1 there is an error
 */
int get_next_jobid_from_list(char **p, uint32_t *JobId)
{
   const int maxlen = 30;
   char jobid[maxlen+1];
   char *q = *p;

   jobid[0] = 0;
   for (int i=0; i<maxlen; i++) {
      if (*q == 0) {
         break;
      } else if (*q == ',') {
         q++;
         break;
      }
      jobid[i] = *q++;
      jobid[i+1] = 0;
   }
   if (jobid[0] == 0) {
      return 0;
   } else if (!is_a_number(jobid)) {
      return -1;                      /* error */
   }
   *p = q;
   *JobId = str_to_int64(jobid);
   return 1;
}

/*
 * Timeout signal comes here
 */
extern "C" void timeout_handler(int sig)
{
   return;                            /* thus interrupting the function */
}

/* Used to display specific daemon information after a fatal signal
 * (like BDB in the director)
 */
#define MAX_DBG_HOOK 10
static dbg_jcr_hook_t *dbg_jcr_hooks[MAX_DBG_HOOK];
static int dbg_jcr_handler_count=0;

void dbg_jcr_add_hook(dbg_jcr_hook_t *hook)
{
   ASSERT(dbg_jcr_handler_count < MAX_DBG_HOOK);
   dbg_jcr_hooks[dbg_jcr_handler_count++] = hook;
}

/* on win32, the pthread_t is a struct, so we don't display it */
#ifdef HAVE_MINGW_W64
# define get_threadid(a) (void *)0
#else
# define get_threadid(a) (void *)(a)
#endif
/*
 * !!! WARNING !!!
 *
 * This function should be used ONLY after a fatal signal. We walk through the
 * JCR chain without doing any lock, Bacula should not be running.
 */
void dbg_print_jcr(FILE *fp)
{
   char buf1[128], buf2[128], buf3[128], buf4[128];

   if (!jcrs) {
      return;
   }

   fprintf(fp, "Attempt to dump current JCRs. njcrs=%d\n", jcrs->size());

   for (JCR *jcr = (JCR *)jcrs->first(); jcr ; jcr = (JCR *)jcrs->next(jcr)) {
      fprintf(fp, "threadid=%p JobId=%d JobStatus=%c jcr=%p name=%s\n",
              get_threadid(jcr->my_thread_id), (int)jcr->JobId, jcr->JobStatus, jcr, jcr->Job);
      fprintf(fp, "\tuse_count=%i killable=%d\n",
              jcr->use_count(), jcr->is_killable());
      fprintf(fp, "\tJobType=%c JobLevel=%c\n",
              jcr->getJobType(), jcr->getJobLevel());
      bstrftime(buf1, sizeof(buf1), jcr->sched_time);
      bstrftime(buf2, sizeof(buf2), jcr->start_time);
      bstrftime(buf3, sizeof(buf3), jcr->end_time);
      bstrftime(buf4, sizeof(buf4), jcr->wait_time);
      fprintf(fp, "\tsched_time=%s start_time=%s\n\tend_time=%s wait_time=%s\n",
              buf1, buf2, buf3, buf4);
      fprintf(fp, "\tdb=%p db_batch=%p batch_started=%i\n",
              jcr->db, jcr->db_batch, jcr->batch_started);

      /*
       * Call all the jcr debug hooks
       */
      for(int i=0; i < dbg_jcr_handler_count; i++) {
         dbg_jcr_hook_t *hook = dbg_jcr_hooks[i];
         hook(jcr, fp);
      }
   }
}
