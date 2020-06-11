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
 * Routines for writing Cloud drivers
 *
 * Written by Kern Sibbald, May MMXVI
 *
 */

#include "bacula.h"
#include "stored.h"
#include "cloud_parts.h"
#include "cloud_transfer_mgr.h"
#include "lib/bwlimit.h"

#ifndef _CLOUD_DRIVER_H_
#define _CLOUD_DRIVER_H_

#define NUM_UPLOAD_RETRIES 2
class cloud_dev;

/* define the cancel callback type */
typedef bool (cancel_cb_type)(void*);
typedef struct  {
  cancel_cb_type* fct;
   void *arg;
} cancel_callback;

/* Abstract class cannot be instantiated */
class cloud_driver: public SMARTALLOC {
public:
   cloud_driver() : max_upload_retries(NUM_UPLOAD_RETRIES) {};
   virtual ~cloud_driver() {};

   virtual bool copy_cache_part_to_cloud(transfer *xfer) = 0;
   virtual bool copy_cloud_part_to_cache(transfer *xfer) = 0;
   virtual bool truncate_cloud_volume(const char *VolumeName, ilist *trunc_parts, cancel_callback *cancel_cb, POOLMEM *&err) = 0;
   virtual bool init(CLOUD *cloud, POOLMEM *&err) = 0;
   virtual bool term(POOLMEM *&err) = 0;
   virtual bool start_of_job(POOLMEM *&err) = 0;
   virtual bool end_of_job(POOLMEM *&err) = 0;
   virtual bool get_cloud_volume_parts_list(const char* VolumeName, ilist *parts, cancel_callback *cancel_cb, POOLMEM *&err) = 0;
   virtual bool get_cloud_volumes_list(alist *volumes, cancel_callback *cancel_cb, POOLMEM *&err) = 0;
   static void add_vol_and_part(POOLMEM *&filename, const char *VolumeName, const char *name, uint32_t apart)
   {
      char partnumber[20];
      int len = strlen(filename);

      if (len > 0 && !IsPathSeparator((filename)[len-1])) {
         pm_strcat(filename, "/");
      }

      pm_strcat(filename, VolumeName);
      bsnprintf(partnumber, sizeof(partnumber), "/%s.%d", name, apart);
      pm_strcat(filename, partnumber);
   }

   bwlimit upload_limit;
   bwlimit download_limit;
   uint32_t max_upload_retries;
};

#endif /* _CLOUD_DRIVER_H_ */
