#
# Makefile for win32 bacula executables
# Using MinGW cross-compiler on GNU/Linux
#
#  Written by James Harper, October 2008
#     Patterned after a Makefile by Robert Nelson, June 2006
#

#
# Change the following depending on where this directory is located
#  in the Bacula tree.	It should point to the src/win32 directory.
BUILDDIR = ../..

include $(BUILDDIR)/Makefile.inc

INCLUDES = \
	$(INCLUDE_PTHREADS) \
	$(INCLUDE_BACULA) \
	$(INCLUDE_ZLIB) \
	$(INCLUDE_OPENSSL) \
	$(INCLUDE_VSS) \
	-I$(MAINDIR)/src \
	-I$(MAINDIR)/src/filed \
	-I$(MAINDIR)/src/win32/filed

DEFINES = \
	$(HAVES)

#vpath %.c $(BUILDDIR)/compat $(MAINDIR)/src/findlib $(MAINDIR)/src/lib
#vpath %.cpp $(BUILDDIR)/compat $(MAINDIR)/src/findlib $(MAINDIR)/src/lib

# Look files in src/lib and plugins/fd mainly for SAP and Oracle
vpath %.c $(MAINDIR)/src/lib $(MAINDIR)/src/plugins/fd
vpath %.h $(MAINDIR)/src/lib $(MAINDIR)/src/plugins/fd

######################################################################

# Files files in src/win32/filed/plugins

EXCHANGE_OBJS = \
	$(OBJDIR)/exchange-fd.o \
	$(OBJDIR)/exch_api.o \
	$(OBJDIR)/exch_node.o \
	$(OBJDIR)/exch_root_node.o \
	$(OBJDIR)/exch_service_node.o \
	$(OBJDIR)/exch_storage_group_node.o \
	$(OBJDIR)/exch_store_node.o \
	$(OBJDIR)/exch_dbi_node.o \
	$(OBJDIR)/exch_file_node.o

BPIPE_OBJS = \
	$(OBJDIR)/bpipe-fd.o

LIBS_DLL = \
	$(LIBS_SSL) \
	$(LIBS_CRYPTO) \
	$(LIBS_PTHREADS) \
	$(LIBS_ZLIB) \
	$(LIBS_NETWORK) \
	-lole32 \
	-loleaut32 \
	-luuid

######################################################################

# Targets

.PHONY: all clean distclean

all: $(BINDIR)/exchange-fd.dll $(BINDIR)/bpipe-fd.dll $(BINDIR)/alldrives-fd.dll

clean:
	@echo "Cleaning `pwd`"
	$(call clean_obj,$(EXCHANGE_OBJS))
	$(call clean_exe,$(BINDIR)/exchange-fd.dll)
	$(ECHO_CMD)rm -f $(OBJDIR)/exchange-fd.a $(LIBDIR)/libexchange-fd.a
	$(ECHO_CMD)rm -rf $(OBJDIRS)

distclean: clean

#
# Rules for generating from ../lib
#

#$(LIBDIR)/libexchange-fd.a: DLL_DEFINE=USING_DLL

#$(LIBDIR)/libexchange-fd.a: $(BINDIR)/exchange-fd.dll $(STATIC_OBJS)
#	@echo "Updating archive $@"
#	$(call checkdir,$@)
#	$(ECHO_CMD)cp $(OBJDIR)/exchange-fd.a $@
#	$(ECHO_CMD)$(AR) rsv $@ $(filter %.o,$^)

$(BINDIR)/exchange-fd.dll: DLL_DEFINE=BUILDING_DLL

$(BINDIR)/exchange-fd.dll: $(EXCHANGE_OBJS) exchange-fd.def
	@echo "Linking $@"
	$(call checkdir,$@)
	$(ECHO_CMD)$(CXX) $(LDFLAGS) -mdll -mwindows -Wl,--out-implib,$(OBJDIR)/exchange-fd.a $^ $(LIBS_DLL) -o $@

$(BINDIR)/bpipe-fd.dll: DLL_DEFINE=BUILDING_DLL

$(BINDIR)/bpipe-fd.dll: $(BPIPE_OBJS) bpipe-fd.def $(LIBS_BACULA)
	@echo "Linking $@"
	$(call checkdir,$@)
	$(ECHO_CMD)$(CXX) $(CFLAGS) $(LDFLAGS) -mdll -mwindows -Wl,--out-implib,$(OBJDIR)/bpipe-fd.a $^ $(LIBS_DLL) -o $@

$(BINDIR)/alldrives-fd.dll: DLL_DEFINE=BUILDING_DLL

$(BINDIR)/alldrives-fd.dll: $(OBJDIR)/alldrives-fd.o $(LIBS_BACULA)
	@echo "Linking $@"
	$(call checkdir,$@)
	$(ECHO_CMD)$(CXX) $(CFLAGS) $(LDFLAGS) -mdll -mwindows -Wl,--out-implib,$(OBJDIR)/alldrives-fd.a $^ $(LIBS_DLL) -o $@

include $(BUILDDIR)/Makefile.rules

$(OBJDIR)/%.o: %.c
	@echo "Compiling $<"
	$(call checkdir,$@)
	$(ECHO_CMD)$(CXX) -D$(DLL_DEFINE) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.cpp
	@echo "Compiling $<"
	$(call checkdir,$@)
	$(ECHO_CMD)$(CXX) -D$(DLL_DEFINE) $(CFLAGS) -c $< -o $@
