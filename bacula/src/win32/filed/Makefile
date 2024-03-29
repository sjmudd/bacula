#
# Makefile for win32 bacula executables
# Using MinGW cross-compiler on GNU/Linux
#
# Author: Robert Nelson
# License: BSD 2-Clause; see file LICENSE-FOSS
#
#  Written by Robert Nelson, June 2006
#

#
# Change the following depending on where this directory is located
#  in the Bacula tree.	It should point to the src/win32 directory.
BUILDDIR = ..


include $(BUILDDIR)/Makefile.inc

INCLUDES = \
	-I. \
	$(INCLUDE_PTHREADS) \
	$(INCLUDE_BACULA) \
	$(INCLUDE_ZLIB) \
	$(INCLUDE_VSS) \
	$(INCLUDE_OPENSSL) \
	$(INCLUDE_ICONS)

DEFINES = \
	-DUSING_DLL \
	-DWIN32_VSS \
	-DWINVER=0x500 \
	$(HAVES)

#	-DHAVE_TRAY_MONITOR \


vpath %.c $(MAINDIR)/src/filed $(BUILDDIR)/libwin32
vpath %.cpp $(MAINDIR)/src/filed $(BUILDDIR)/libwin32

##########################################################################

FILED_OBJS = \
	$(OBJDIR)/accurate.o \
	$(OBJDIR)/authenticate.o \
	$(OBJDIR)/backup.o \
	$(OBJDIR)/crypto.o \
	$(OBJDIR)/fdcollect.o \
	$(OBJDIR)/win_efs.o \
	$(OBJDIR)/estimate.o \
	$(OBJDIR)/fd_plugins.o \
	$(OBJDIR)/fd_snapshot.o \
	$(OBJDIR)/filed.o \
	$(OBJDIR)/filed_conf.o \
	$(OBJDIR)/heartbeat.o \
	$(OBJDIR)/hello.o \
	$(OBJDIR)/job.o \
	$(OBJDIR)/restore.o \
	$(OBJDIR)/status.o \
	$(OBJDIR)/verify.o \
	$(OBJDIR)/verify_vol.o \
	$(OBJDIR)/vss.o \
	$(OBJDIR)/vss_XP.o \
	$(OBJDIR)/vss_W2K3.o \
	$(OBJDIR)/vss_Vista.o \
	$(OBJDIR)/service.o \
	$(OBJDIR)/main.o \
	$(OBJDIR)/bacl.o \
	$(OBJDIR)/bacl_linux.o \
	$(OBJDIR)/bacl_solaris.o \
	$(OBJDIR)/bxattr_freebsd.o \
	$(OBJDIR)/bxattr_osx.o \
	$(OBJDIR)/bacl_freebsd.o \
	$(OBJDIR)/bacl_osx.o \
	$(OBJDIR)/bxattr.o \
	$(OBJDIR)/bxattr_linux.o \
	$(OBJDIR)/bxattr_solaris.o \
	$(OBJDIR)/suspend.o \
	$(OBJDIR)/bacula.res

#	$(OBJDIR)/trayMonitor.o \
#	$(OBJDIR)/aboutDialog.o \
#	$(OBJDIR)/statusDialog.o \


ALL_OBJS = $(FILED_OBJS)

FILED_LIBS = \
	$(LIBS_PTHREADS) \
	$(LIBS_ZLIB) \
	$(LIBS_LZO) \
	$(LIBS_NETWORK) \
	-lole32 \
	-loleaut32 \
	-luuid \
	-lcomctl32

######################################################################

# Targets

.PHONY: all clean distclean

all: $(BINDIR)/bacula-fd.exe

distclean: clean

clean:
	@echo "Cleaning `pwd`"
	$(call clean_obj,$(ALL_OBJS))
	$(call clean_exe,$(BINDIR)/bacula-fd.exe)
	$(ECHO_CMD)rm -rf $(OBJDIRS)

#
# Rules
#

$(BINDIR)/bacula-fd.exe: $(FILED_OBJS) $(LIBS_BACULA)
	$(call link_winapp,$(FILED_LIBS))

$(OBJDIR)/winres.res: $(BUILDDIR)/libwin32/winres.rc
	@echo "Compiling $@"
	$(call checkdir,$@)
	$(ECHO_CMD)$(WINDRES) $(INCLUDE_ICONS) -DMINGW64 -O coff $< -o $@

include $(BUILDDIR)/Makefile.rules
