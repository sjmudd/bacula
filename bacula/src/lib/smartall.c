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

                         S M A R T A L L O C
                        Smart Memory Allocator

        Evolved   over   several  years,  starting  with  the  initial
        SMARTALLOC code for AutoSketch in 1986, guided  by  the  Blind
        Watchbreaker,  John  Walker.  Isolated in this general-purpose
        form in  September  of  1989.   Updated  with  be  more  POSIX
        compliant  and  to  include Web-friendly HTML documentation in
        October  of  1998  by  the  same  culprit.    For   additional
        information and the current version visit the Web page:

                  http://www.fourmilab.ch/smartall/

*/

#define LOCKMGR_COMPLIANT

#include "bacula.h"
/* Use the real routines here */
#undef realloc
#undef calloc
#undef malloc
#undef free

/* We normally turn off debugging here.
 *  If you want it, simply #ifdef all the
 *  following off.
 */
#ifdef no_debug_xxxxx
#undef Dmsg1
#undef Dmsg2
#undef Dmsg3
#undef Dmsg4
#define Dmsg1(l,f,a1)
#define Dmsg2(l,f,a1,a2)
#define Dmsg3(l,f,a1,a2,a3)
#define Dmsg4(l,f,a1,a2,a3,a4)
#endif


uint64_t sm_max_bytes = 0;
uint64_t sm_bytes = 0;
uint32_t sm_max_buffers = 0;
uint32_t sm_buffers = 0;

#ifdef SMARTALLOC

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

extern char my_name[];                /* daemon name */

#define EOS      '\0'              /* End of string sentinel */
#define sm_min(a, b) ((a) < (b) ? (a) : (b))

/*  Queue data structures  */

/*  Memory allocation control structures and storage.  */

struct abufhead {
   struct b_queue abq;         /* Links on allocated queue */
   uint32_t ablen;             /* Buffer length in bytes */
   const char *abfname;        /* File name pointer */
   uint32_t ablineno;          /* Line number of allocation */
   bool abin_use;              /* set when malloced and cleared when free */
};

static struct b_queue abqueue = {    /* Allocated buffer queue */
   &abqueue, &abqueue
};


static bool bufimode = false;   /* Buffers not tracked when True */

#define HEAD_SIZE BALIGN(sizeof(struct abufhead))


/*  SMALLOC  --  Allocate buffer, enqueing on the orphaned buffer
                 tracking list.  */

static void *smalloc(const char *fname, int lineno, unsigned int nbytes)
{
   char *buf;

   /* Note:  Unix  MALLOC  actually  permits  a zero length to be
      passed and allocates a valid block with  zero  user  bytes.
      Such  a  block  can  later  be expanded with realloc().  We
      disallow this based on the belief that it's better to  make
      a  special case and allocate one byte in the rare case this
      is desired than to miss all the erroneous occurrences where
      buffer length calculation code results in a zero.  */

   if (nbytes == 0) {
      Tmsg3(0, "Invalid memory allocation. %u bytes %s:%d\n", nbytes, fname, lineno);
      ASSERT(nbytes > 0);
   }

   nbytes += HEAD_SIZE + 1;
   if ((buf = (char *)malloc(nbytes)) != NULL) {
      struct abufhead *head = (struct abufhead *)buf;
      P(mutex);
      /* Enqueue buffer on allocated list */
      qinsert(&abqueue, (struct b_queue *) buf);
      head->ablen = nbytes;
      head->abfname = bufimode ? NULL : fname;
      head->ablineno = (uint32_t)lineno;
      head->abin_use = true;
      /* Emplace end-clobber detector at end of buffer */
      buf[nbytes - 1] = (uint8_t)((((intptr_t) buf) & 0xFF) ^ 0xC5);
      buf += HEAD_SIZE;  /* Increment to user data start */
      if (++sm_buffers > sm_max_buffers) {
         sm_max_buffers = sm_buffers;
      }
      sm_bytes += nbytes;
      if (sm_bytes > sm_max_bytes) {
         sm_max_bytes = sm_bytes;
      }
      V(mutex);
   } else {
      Emsg0(M_ABORT, 0, _("Out of memory\n"));
   }
   Dmsg4(DT_MEMORY|1050, "smalloc %d at %p from %s:%d\n", nbytes, buf, fname, lineno);
#if    SMALLOC_SANITY_CHECK > 0
   if (sm_bytes > SMALLOC_SANITY_CHECK) {
      Emsg0(M_ABORT, 0, _("Too much memory used."));
   }
#endif
   return (void *)buf;
}

/*  SM_NEW_OWNER -- Update the File and line number for a buffer
                    This is to accomodate mem_pool. */

void sm_new_owner(const char *fname, int lineno, char *buf)
{
   buf -= HEAD_SIZE;  /* Decrement to header */
   P(mutex);
   ((struct abufhead *)buf)->abfname = bufimode ? NULL : fname;
   ((struct abufhead *)buf)->ablineno = (uint32_t) lineno;
   ((struct abufhead *)buf)->abin_use = true;
   V(mutex);
   return;
}

/* Print a debug message with the owner of a smartalloc buffer */
void sm_get_owner(int64_t dbglvl, char *buf)
{
   /* Decrement to header */
   struct abufhead *h = (struct abufhead *) (buf - HEAD_SIZE);
   Dmsg3(dbglvl, "%p from %s:%d\n",
         buf + HEAD_SIZE, 
         NPRT(h->abfname),
         h->ablineno);
}

/*  SM_FREE  --  Update free pool availability.  FREE is never called
                 except  through  this interface or by actuallyfree().
                 free(x)  is  defined  to  generate  a  call  to  this
                 routine.  */

void sm_free(const char *file, int line, void *fp)
{
   char *cp = (char *) fp;
   struct b_queue *qp;
   uint32_t lineno = line;

   if (cp == NULL) {
      Emsg2(M_ABORT, 0, _("Attempt to free NULL called from %s:%d\n"), file, lineno);
   }

   cp -= HEAD_SIZE;
   qp = (struct b_queue *)cp;
   struct abufhead *head = (struct abufhead *)cp;

   P(mutex);
   Dmsg4(DT_MEMORY|1050, "sm_free %d at %p from %s:%d\n",
         head->ablen, fp,
         get_basename(head->abfname), head->ablineno);

   if (!head->abin_use) {
      V(mutex);
      Emsg2(M_ABORT, 0, _("in-use bit not set: double free from %s:%d\n"), file, lineno);
   }
   head->abin_use = false;

   /* The following assertions will catch virtually every release
      of an address which isn't an allocated buffer. */
   if (qp->qnext->qprev != qp) {
      V(mutex);
      Emsg2(M_ABORT, 0, _("qp->qnext->qprev != qp called from %s:%d\n"), file, lineno);
   }
   if (qp->qprev->qnext != qp) {
      V(mutex);
      Emsg2(M_ABORT, 0, _("qp->qprev->qnext != qp called from %s:%d\n"), file, lineno);
   }

   /* The following assertion detects storing off the  end  of  the
      allocated  space in the buffer by comparing the end of buffer
      checksum with the address of the buffer.  */

   if (((unsigned char *)cp)[head->ablen - 1] != ((((intptr_t) cp) & 0xFF) ^ 0xC5)) {
      V(mutex);
      Emsg6(M_ABORT, 0, _("Overrun buffer: len=%d addr=%p allocated: %s:%d called from %s:%d\n"),
         head->ablen, fp, get_basename(head->abfname), head->ablineno, file, line);
   }
   if (sm_buffers > 0) {
      sm_buffers--;
      sm_bytes -= head->ablen;
   }

   qdchain(qp);
   V(mutex);

   /* Now we wipe the contents of  the  just-released  buffer  with
      "designer  garbage"  (Duff  Kurland's  phrase) of alternating
      bits.  This is intended to ruin the day for any miscreant who
      attempts to access data through a pointer into storage that's
      been previously released.

      Modified, kes May, 2007 to not zap the header. This allows us
      to check the in_use bit and detect doubly freed buffers.
   */

   memset(cp+HEAD_SIZE, 0xAA, (int)(head->ablen - HEAD_SIZE));

   free(cp);
}

/*  SM_MALLOC  --  Allocate buffer.  NULL is returned if no memory
                   was available.  */

void *sm_malloc(const char *fname, int lineno, unsigned int nbytes)
{
   void *buf;

   if ((buf = smalloc(fname, lineno, nbytes)) != NULL) {

      /* To catch sloppy code that assumes  buffers  obtained  from
         malloc()  are  zeroed,  we  preset  the buffer contents to
         "designer garbage" consisting of alternating bits.

          Removed 10 May 2020 KES

          memset(buf, 0x55, (int) nbytes);
       */
      memset(buf, 0, (int) nbytes);      /* clear the memory */
   } else {
      Emsg0(M_ABORT, 0, _("Out of memory\n"));
   }
   return buf;
}

/*  SM_CALLOC  --  Allocate an array and clear it to zero.  */

void *sm_calloc(const char *fname, int lineno,
                unsigned int nelem, unsigned int elsize)
{
   void *buf;

   if ((buf = smalloc(fname, lineno, nelem * elsize)) != NULL) {
      memset(buf, 0, (int) (nelem * elsize));
   } else {
      Emsg0(M_ABORT, 0, _("Out of memory\n"));
   }
   return buf;
}

/*  SM_REALLOC  --  Adjust the size of a  previously  allocated  buffer.
                    Note  that  the trick of "resurrecting" a previously
                    freed buffer with realloc() is NOT supported by this
                    function.   Further, because of the need to maintain
                    our control storage, SM_REALLOC must always allocate
                    a  new  block  and  copy  the data in the old block.
                    This may result in programs which make heavy use  of
                    realloc() running much slower than normally.  */

void *sm_realloc(const char *fname, int lineno, void *ptr, unsigned int size)
{
   unsigned osize;
   void *buf;
   char *cp = (char *) ptr;

   Dmsg4(DT_MEMORY|1050, "sm_realloc %s:%d %p %d\n", get_basename(fname), (uint32_t)lineno, ptr, size);
   if (size <= 0) {
      e_msg(fname, lineno, M_ABORT, 0, _("sm_realloc size: %d\n"), size);
   }

   /*  If  the  old  block  pointer  is  NULL, treat realloc() as a
      malloc().  SVID is silent  on  this,  but  many  C  libraries
      permit this.  */
   if (ptr == NULL) {
      return sm_malloc(fname, lineno, size);
   }

   /* If the old and new sizes are the same, be a nice guy and just
      return the buffer passed in.  */
   cp -= HEAD_SIZE;
   struct abufhead *head = (struct abufhead *)cp;
   osize = head->ablen - (HEAD_SIZE + 1);
   if (size == osize) {
      return ptr;
   }

   /* Sizes differ.  Allocate a new buffer of the  requested  size.
      If  we  can't  obtain  such a buffer, act as defined in SVID:
      return NULL from  realloc()  and  leave  the  buffer  in  PTR
      intact.  */

// sm_buffers--;
// sm_bytes -= head->ablen;

   if ((buf = smalloc(fname, lineno, size)) != NULL) {
      memcpy(buf, ptr, (int)sm_min(size, osize));
      /* If the new buffer is larger than the old, fill the balance
         of it with "designer garbage". */
      if (size > osize) {
         /*
            Removed 10 May 2020 KES
            memset(((char *) buf) + osize, 0x55, (int) (size - osize));
         */

         memset(((char *) buf) + osize, 0, (int) (size - osize));
      }

      /* All done.  Free and dechain the original buffer. */
      sm_free(fname, lineno, ptr);
   }
   Dmsg4(DT_MEMORY|1060, _("sm_realloc %d at %p from %s:%d\n"), size, buf, get_basename(fname), (uint32_t)lineno);
   return buf;
}

/*  ACTUALLYMALLOC  --  Call the system malloc() function to obtain
                        storage which will eventually be released
                        by system or library routines not compiled
                        using SMARTALLOC.  */

void *actuallymalloc(unsigned int size)
{
   return malloc(size);
}

/*  ACTUALLYCALLOC  --  Call the system calloc() function to obtain
                        storage which will eventually be released
                        by system or library routines not compiled
                        using SMARTALLOC.  */

void *actuallycalloc(unsigned int nelem, unsigned int elsize)
{
   return calloc(nelem, elsize);
}

/*  ACTUALLYREALLOC  --  Call the system realloc() function to obtain
                         storage which will eventually be released
                         by system or library routines not compiled
                         using SMARTALLOC.  */

void *actuallyrealloc(void *ptr, unsigned int size)
{
   Dmsg2(DT_MEMORY|1040, "Actuallyrealloc %p %d\n", ptr, size);
   return realloc(ptr, size);
}

/*  ACTUALLYFREE  --  Interface to system free() function to release
                      buffers allocated by low-level routines. */

void actuallyfree(void *cp)
{
   free(cp);
}

/*  SM_DUMP  --  Print orphaned buffers (and dump them if BUFDUMP is
 *               True).
 */
void sm_dump(bool bufdump, bool in_use)
{
   struct abufhead *ap;

   P(mutex);

   ap = (struct abufhead *)abqueue.qnext;

   while (ap != (struct abufhead *) &abqueue) {

      if ((ap == NULL) ||
          (ap->abq.qnext->qprev != (struct b_queue *) ap) ||
          (ap->abq.qprev->qnext != (struct b_queue *) ap)) {
         Pmsg1(0, _(
            "\nOrphaned buffers exist.  Dump terminated following\n"
            "  discovery of bad links in chain of orphaned buffers.\n"
            "  Buffer address with bad links: %p\n"), ap);
         break;
      }

      if (ap->abfname != NULL) {
         char errmsg[500];
         uint32_t memsize = ap->ablen - (HEAD_SIZE + 1);
         char *cp = ((char *)ap) + HEAD_SIZE;

         Pmsg6(0, "%s buffer: %s %d bytes at %p from %s:%d\n",
            in_use?"In use":"Orphaned",
            my_name, memsize, cp, get_basename(ap->abfname), ap->ablineno);
         if (bufdump) {
            char buf[20];
            unsigned llen = 0;

            errmsg[0] = EOS;
            while (memsize) {
               if (llen >= 16) {
                  bstrncat(errmsg, "\n", sizeof(errmsg));
                  llen = 0;
                  Pmsg1(0, "%s", errmsg);
                  errmsg[0] = EOS;
               }
               bsnprintf(buf, sizeof(buf), " %02X",
                  (*cp++) & 0xFF);
               bstrncat(errmsg, buf, sizeof(errmsg));
               llen++;
               memsize--;
            }
            Pmsg1(0, "%s\n", errmsg);
         }
      }
      ap = (struct abufhead *) ap->abq.qnext;
   }
   V(mutex);
}

#undef sm_check
/*  SM_CHECK --  Check the buffers and dump if any damage exists. */
void sm_check(const char *fname, int lineno, bool bufdump)
{
   if (!sm_check_rtn(fname, lineno, bufdump)) {
      Emsg2(M_ABORT, 0, _("Damaged buffer found. Called from %s:%d\n"),
            get_basename(fname), (uint32_t)lineno);
   }
}

#undef sm_check_rtn
/*  SM_CHECK_RTN -- Check the buffers and return 1 if OK otherwise 0 */
int sm_check_rtn(const char *fname, int lineno, bool bufdump)
{
   struct abufhead *ap;
   int bad, badbuf = 0;

   P(mutex);
   ap = (struct abufhead *) abqueue.qnext;
   while (ap != (struct abufhead *)&abqueue) {
      bad = 0;
      if (ap != NULL) {
         if (ap->abq.qnext->qprev != (struct b_queue *)ap) {
            bad = 0x1;
         }
         if (ap->abq.qprev->qnext != (struct b_queue *)ap) {
            bad |= 0x2;
         }
         if (((unsigned char *) ap)[((struct abufhead *)ap)->ablen - 1] !=
              ((((intptr_t) ap) & 0xFF) ^ 0xC5)) {
            bad |= 0x4;
         }
      } else {
         bad = 0x8;
      }
      badbuf |= bad;
      if (bad) {
         Pmsg2(0,
            _("\nDamaged buffers found at %s:%d\n"), get_basename(fname), (uint32_t)lineno);

         if (bad & 0x1) {
            Pmsg0(0,  _("  discovery of bad prev link.\n"));
         }
         if (bad & 0x2) {
            Pmsg0(0, _("  discovery of bad next link.\n"));
         }
         if (bad & 0x4) {
            Pmsg0(0, _("  discovery of data overrun.\n"));
         }
         if (bad & 0x8) {
            Pmsg0(0, _("  NULL pointer.\n"));
         }

         if (!ap) {
            goto get_out;
         }
         Pmsg1(0, _("  Buffer address: %p\n"), ap);

         if (ap->abfname != NULL) {
            uint32_t memsize = ap->ablen - (HEAD_SIZE + 1);
            char errmsg[80];

            Pmsg4(0,
              _("Damaged buffer:  %6u bytes allocated at line %d of %s %s\n"),
               memsize, ap->ablineno, my_name, get_basename(ap->abfname)
            );
            if (bufdump) {
               unsigned llen = 0;
               char *cp = ((char *) ap) + HEAD_SIZE;

               errmsg[0] = EOS;
               while (memsize) {
                  if (llen >= 16) {
                     strcat(errmsg, "\n");
                     llen = 0;
                     Pmsg1(0, "%s", errmsg);
                     errmsg[0] = EOS;
                  }
                  if (*cp < 0x20) {
                     sprintf(errmsg + strlen(errmsg), " %02X",
                        (*cp++) & 0xFF);
                  } else {
                     sprintf(errmsg + strlen(errmsg), " %c ",
                        (*cp++) & 0xFF);
                  }
                  llen++;
                  memsize--;
               }
               Pmsg1(0, "%s\n", errmsg);
            }
         }
      }
      ap = (struct abufhead *)ap->abq.qnext;
   }
get_out:
   V(mutex);
   return badbuf ? 0 : 1;
}


/*  SM_STATIC  --  Orphaned buffer detection can be disabled  (for  such
                   items  as buffers allocated during initialisation) by
                   calling   sm_static(1).    Normal   orphaned   buffer
                   detection  can be re-enabled with sm_static(0).  Note
                   that all the other safeguards still apply to  buffers
                   allocated  when  sm_static(1)  mode is in effect.  */

void sm_static(bool mode)
{
   bufimode = mode;
}

/*
 * Here we overload C++'s global new and delete operators
 *  so that the memory is allocated through smartalloc.
 */

#ifdef xxx
void * operator new(size_t size)
{
// Dmsg1(000, "new called %d\n", size);
   return sm_malloc(__FILE__, __LINE__, size);
}

void operator delete(void *buf)
{
// Dmsg1(000, "free called %p\n", buf);
   sm_free(__FILE__, __LINE__, buf);
}
#endif

#endif

/* Avoid aggressive optimization */
void *bmemset(void *s, int c, size_t n)
{
   void *ret = memset(s, c, n);
   return ret;
}
