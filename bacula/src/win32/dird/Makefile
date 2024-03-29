#
# Makefile for win32 bacula executables
# Using MinGW cross-compiler on GNU/Linux
#
#
# Author: Robert Nelson
# License: BSD 2-Clause; see file LICENSE-FOSS
#
#  Written by Robert Nelson, June 2006
#

include ../Makefile.inc

INCLUDES = \
	-I. \
	$(INCLUDE_PTHREADS) \
	$(INCLUDE_BACULA) \
	$(INCLUDE_ZLIB) \
	$(INCLUDE_OPENSSL) \
	$(INCLUDE_ICONS)

DEFINES = \
	-DUSING_DLL \
	-DUSING_CATS \
	-DWINVER=0x500 \
	$(HAVES)

vpath %.c $(MAINDIR)/src/dird $(BUILDDIR)/libwin32
vpath %.cpp $(MAINDIR)/src/dird $(BUILDDIR)/libwin32

##########################################################################

DIRD_OBJS = \
	$(OBJDIR)/admin.o \
	$(OBJDIR)/authenticate.o \
	$(OBJDIR)/autoprune.o \
	$(OBJDIR)/backup.o \
	$(OBJDIR)/bsr.o \
	$(OBJDIR)/catreq.o \
	$(OBJDIR)/dir_plugins.o \
	$(OBJDIR)/dird.o \
	$(OBJDIR)/dird_conf.o \
	$(OBJDIR)/expand.o \
	$(OBJDIR)/fd_cmds.o \
	$(OBJDIR)/getmsg.o \
	$(OBJDIR)/inc_conf.o \
	$(OBJDIR)/job.o \
	$(OBJDIR)/jobq.o \
	$(OBJDIR)/mountreq.o \
	$(OBJDIR)/msgchan.o \
	$(OBJDIR)/newvol.o \
	$(OBJDIR)/next_vol.o \
	$(OBJDIR)/recycle.o \
	$(OBJDIR)/restore.o \
	$(OBJDIR)/run_conf.o \
	$(OBJDIR)/scheduler.o \
	$(OBJDIR)/ua_acl.o \
	$(OBJDIR)/ua_cmds.o \
	$(OBJDIR)/ua_dotcmds.o \
	$(OBJDIR)/ua_collect.o \
	$(OBJDIR)/ua_input.o \
	$(OBJDIR)/ua_label.o \
	$(OBJDIR)/ua_output.o \
	$(OBJDIR)/ua_prune.o \
	$(OBJDIR)/ua_purge.o \
	$(OBJDIR)/ua_query.o \
	$(OBJDIR)/ua_restore.o \
	$(OBJDIR)/ua_run.o \
	$(OBJDIR)/ua_select.o \
	$(OBJDIR)/ua_server.o \
	$(OBJDIR)/ua_status.o \
	$(OBJDIR)/ua_tree.o \
	$(OBJDIR)/ua_update.o \
	$(OBJDIR)/ua_dde.o \
	$(OBJDIR)/vbackup.o \
	$(OBJDIR)/verify.o \
	$(OBJDIR)/service.o \
	$(OBJDIR)/mac_sql.o \
	$(OBJDIR)/mac.o \
	$(OBJDIR)/main.o \
	$(OBJDIR)/bacula.res

ALL_OBJS = $(DIRD_OBJS)


DIRD_LIBS = \
	$(LIBS_PTHREADS) \
	$(LIBS_NETWORK) \
	-lole32 \
	-loleaut32 \
	-luuid \
	-lcomctl32

######################################################################

# Targets

.PHONY: all clean

all: $(BINDIR)/bacula-dir.exe

clean:
	@echo "Cleaning `pwd`"
	$(call clean_obj,$(ALL_OBJS))
	$(call clean_exe,$(BINDIR)/bacula-dir.exe)
	$(ECHO_CMD)rm -rf $(OBJDIRS)

#
# Rules
#

$(BINDIR)/bacula-dir.exe: $(DIRD_OBJS) $(LIBS_CATS) $(LIBS_BACULA)
	$(call link_winapp,$(DIRD_LIBS))

$(OBJDIR)/winres.res: winres.rc
	$(WINDRES) $(INCLUDE_ICONS) -O coff $< -o $@

include ../Makefile.rules
