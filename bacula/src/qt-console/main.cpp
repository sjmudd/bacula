/*
   Bacula® - The Network Backup Solution

   Copyright (C) 2007-2007 Free Software Foundation Europe e.V.

   The main author of Bacula is Kern Sibbald, with contributions from
   many others, a complete list can be found in the file AUTHORS.
   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version two of the GNU General Public
   License as published by the Free Software Foundation plus additions
   that are listed in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.

   Bacula® is a registered trademark of John Walker.
   The licensor of Bacula is the Free Software Foundation Europe
   (FSFE), Fiduciary Program, Sumatrastrasse 25, 8006 Zürich,
   Switzerland, email:ftf@fsfeurope.org.
*/
/*
 *   Version $Id$
 *
 *  Main program for bat (qt-console)
 *
 *   Kern Sibbald, January MMVII
 *
 */ 


#include <QApplication>
#include "bat.h"

bool commDebug = false;
MainWin *mainWin;
QApplication *app;


/* Forward referenced functions */
void terminate_console(int sig);                                
static void usage();
static int check_resources();

#define CONFIG_FILE "./bat.conf"   /* default configuration file */

/* Static variables */
static char *configfile = NULL;

int main(int argc, char *argv[])
{

   int ch;
   bool no_signals = true;
   bool test_config = false;


   app = new QApplication(argc, argv);        
   app->setQuitOnLastWindowClosed(true);


#ifdef ENABLE_NLS
   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");
#endif

   init_stack_dump();
   my_name_is(argc, argv, "gnome-console");
   init_msg(NULL, NULL);
   working_directory  = "/tmp";

   struct sigaction sigignore;
   sigignore.sa_flags = 0;
   sigignore.sa_handler = SIG_IGN;
   sigfillset(&sigignore.sa_mask);
   sigaction(SIGPIPE, &sigignore, NULL);

   while ((ch = getopt(argc, argv, "bc:d:r:st?")) != -1) {
      switch (ch) {
      case 'c':                    /* configuration file */
         if (configfile != NULL) {
            free(configfile);
         }
         configfile = bstrdup(optarg);
         break;

      case 'd':
         debug_level = atoi(optarg);
         if (debug_level <= 0)
            debug_level = 1;
         break;

      case 's':                    /* turn off signals */
         no_signals = true;
         break;

      case 't':
         test_config = true;
         break;

      case '?':
      default:
         usage();
      }
   }
   argc -= optind;
   argv += optind;


   if (!no_signals) {
      init_signals(terminate_console);
   }

   if (argc) {
      usage();
   }

   if (configfile == NULL) {
      configfile = bstrdup(CONFIG_FILE);
   }

   parse_config(configfile);

   if (init_crypto() != 0) {
      Emsg0(M_ERROR_TERM, 0, _("Cryptography library initialization failed.\n"));
   }

   if (!check_resources()) {
      Emsg1(M_ERROR_TERM, 0, _("Please correct configuration file: %s\n"), configfile);
   }

   mainWin = new MainWin;
   mainWin->show();

   return app->exec();
}

void terminate_console(int sig)
{
   (void)sig;                         /* avoid compiler complaints */
   exit(0);
}

static void usage()
{
   fprintf(stderr, _(
PROG_COPYRIGHT
"\nVersion: %s (%s) %s %s %s\n\n"
"Usage: bat [-s] [-c config_file] [-d debug_level] [config_file]\n"
"       -c <file>   set configuration file to file\n"
"       -dnn        set debug level to nn\n"
"       -s          no signals\n"
"       -t          test - read configuration and exit\n"
"       -?          print this message.\n"
"\n"), 2007, VERSION, BDATE, HOST_OS, DISTNAME, DISTVER);

   exit(1);
}

#ifdef xxx
/*
 * Call-back for reading a passphrase for an encrypted PEM file
 * This function uses getpass(), which uses a static buffer and is NOT thread-safe.
 */
static int tls_pem_callback(char *buf, int size, const void *userdata)
{
#ifdef HAVE_TLS
   const char *prompt = (const char *) userdata;
   char *passwd;

   passwd = getpass(prompt);
   bstrncpy(buf, passwd, size);
   return (strlen(buf));
#else
   buf[0] = 0;
   return 0;
#endif
}
#endif


/*
 * Make a quick check to see that we have all the
 * resources needed.
 */
static int check_resources()
{
   bool ok = true;
   DIRRES *director;
   int numdir;

   LockRes();

   numdir = 0;
   foreach_res(director, R_DIRECTOR) {
      numdir++;
      /* tls_require implies tls_enable */
      if (director->tls_require) {
         if (have_tls) {
            director->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            ok = false;
            continue;
         }
      }

      if ((!director->tls_ca_certfile && !director->tls_ca_certdir) && director->tls_enable) {
         Emsg2(M_FATAL, 0, _("Neither \"TLS CA Certificate\""
                             " or \"TLS CA Certificate Dir\" are defined for Director \"%s\" in %s."
                             " At least one CA certificate store is required.\n"),
                             director->hdr.name, configfile);
         ok = false;
      }
   }
   
   if (numdir == 0) {
      Emsg1(M_FATAL, 0, _("No Director resource defined in %s\n"
                          "Without that I don't how to speak to the Director :-(\n"), configfile);
      ok = false;
   }

   CONRES *cons;
   /* Loop over Consoles */
   foreach_res(cons, R_CONSOLE) {
      /* tls_require implies tls_enable */
      if (cons->tls_require) {
         if (have_tls) {
            cons->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            ok = false;
            continue;
         }
      }

      if ((!cons->tls_ca_certfile && !cons->tls_ca_certdir) && cons->tls_enable) {
         Emsg2(M_FATAL, 0, _("Neither \"TLS CA Certificate\""
                             " or \"TLS CA Certificate Dir\" are defined for Console \"%s\" in %s.\n"),
                             cons->hdr.name, configfile);
         ok = false;
      }
   }

   UnlockRes();

   return ok;
}
