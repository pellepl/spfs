# spfs makefile
#
# MONOLITH=y will build a monolith
# GCOV=y to enable coverage analysis
# GCOV=f to enable coverage analysis and get coverage output
# ADDR-SANI=y to enable address sanitizer analysis
# DBG = y to enable loads of debug output
#

.DEFAULT_GOAL := all

srcdir = src
utildir = util
testdir = test
builddir = build
gcov-report = $(builddir)/gcov-report
binary = spfs
MKDIR = mkdir -p
FLAGS ?=

GCOV_ANNOTATION := //@REQUIRED_GCOV:

MONOLITH ?=
GCOV ?=
V ?= @
ADDR-SANI ?=
DBG ?=

TARGET-CALCULATOR = .calculator
TARGET-MKIMAGE = .mkimage
TARGET-UNPDUMP = .unpdump
CFILES_BASE := $(wildcard $(srcdir)/*.c)
CFILES_CALC = $(srcdir)/$(utildir)/calc.c
CFILES_MKIMAGE = $(srcdir)/$(utildir)/mkimg.c
CFILES_UNPDUMP = $(srcdir)/$(utildir)/unpdump.c
CFILES_TEST = $(wildcard $(srcdir)/$(testdir)/*.c)
CFILES_TEST_BASE = $(wildcard $(srcdir)/$(testdir)/framework/*.c)
CFILES_MONOLITH = $(srcdir)/spfs_monolith.c

CFLAGS += \
	-I$(srcdir) \
	-I$(srcdir)/$(testdir) \
	-I$(srcdir)/$(testdir)/framework \
	-I$(srcdir)/$(utildir)

ifeq ($(MONOLITH),y)
CFILES_FS = $(CFILES_MONOLITH)
binary := $(binary)-mono
FLAGS += -DSPFS_MONOLITH=1
else
CFILES_FS = $(filter-out $(CFILES_MONOLITH),$(CFILES_BASE))
endif

ifeq ($(MAKECMDGOALS), $(TARGET-CALCULATOR))
# calculator
CFILES := $(CFILES_FS) $(CFILES_CALC)
binary := $(binary)-calc
else
  ifeq ($(MAKECMDGOALS), $(TARGET-MKIMAGE))
# mkimg
CFILES := $(CFILES_FS) $(CFILES_TEST_BASE) $(CFILES_MKIMAGE)
FLAGS += -DSPFS_TEST=1
binary := $(binary)-mkimg
  else
    ifeq ($(MAKECMDGOALS), $(TARGET-UNPDUMP))
# unpdump
CFILES := $(CFILES_FS) $(CFILES_TEST_BASE) $(CFILES_UNPDUMP)
FLAGS += -DSPFS_TEST=1
binary := $(binary)-unpdump
    else
# default to the test binary
CFILES := $(CFILES_FS) $(CFILES_TEST_BASE) $(CFILES_TEST)
FLAGS += -DSPFS_TEST=1
binary := $(binary)-test
    endif
  endif
endif

OBJFILES = $(CFILES:%.c=$(builddir)/%.o)
OBJFSFILES = $(CFILES_FS:%.c=$(builddir)/%.o)
DEPFILES = $(CFILES:%.c=$(builddir)/%.d)

CFLAGS += \
-Wall -Wno-format-y2k -W -Wstrict-prototypes -Wmissing-prototypes \
-Wpointer-arith -Wreturn-type -Wcast-qual -Wwrite-strings -Wswitch \
-Wshadow -Wcast-align -Wchar-subscripts -Winline -Wnested-externs \
-Wredundant-decls

# todo remove when done
CFLAGS += -Wno-unused

#CFLAGS += -Os

# enable address sanitization
ifeq ($(ADDR-SANI),y)
CFLAGS += -fsanitize=address
LDFLAGS += -fsanitize=address -fno-omit-frame-pointer -O
LIBS += -lasan
endif

# enable coverage test
GCOV_FLAGS = -fprofile-arcs -ftest-coverage
ifeq ($(GCOV),f)
CFLAGS_GCOV = $(GCOV_FLAGS)
LDFLAGS_GCOV = $(GCOV_FLAGS)
GCOV_SWITCHES =
RUN_GCOV = y
endif
ifeq ($(GCOV),y)
CFLAGS_GCOV = $(GCOV_FLAGS)
LDFLAGS_GCOV = $(GCOV_FLAGS)
GCOV_SWITCHES = -n
RUN_GCOV = y
endif

# enable debug
ifeq ($(DBG),y)
CFLAGS += -D_DBG_FROM_MAKE=1
endif

LDFLAGS += $(LDFLAGS_GCOV)
CFLAGS += $(FLAGS)

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),info)
-include $(DEPFILES)
endif
endif

# coverage for fs files only
$(OBJFSFILES): CFLAGS += $(CFLAGS_GCOV)

$(builddir)/$(binary): $(OBJFILES)
	$(V)@echo "... linking $@"
	$(V)$(CC) $(LDFLAGS) $(LIBS) -o $@ $(OBJFILES)

$(OBJFILES) : $(builddir)/%.o:%.c
	$(V)echo "... compile $@"
	$(V)$(MKDIR) $(@D);
	$(V)$(CC) $(CFLAGS) -g -c -o $@ $<

$(DEPFILES) : $(builddir)/%.d:%.c
	$(V)echo "... depend $@"; \
	rm -f $@; \
	$(MKDIR) $(@D); \
	$(CC) -M $< > $@.$$$$ 2> /dev/null; \
	sed 's,\($*\)\.o[ :]*, $(builddir)/\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

.PHONY: all test clean

.mkdirs:
	-$(V)$(MKDIR) $(builddir)

# make all binaries
all:	$(builddir)/$(binary) calculator mkimg unpdump

# build and run test suites
test: GCOV_FILES := $(shell grep -EIr '$(GCOV_ANNOTATION)[0-9]{2}' $(srcdir)/)
test: $(builddir)/$(binary)
	$(V)./$(builddir)/$(binary)
ifeq ($(RUN_GCOV),y)
	$(V)rm -f $(gcov-report)
	$(V)for cfile in $(CFILES_FS); do \
		gcov $(GCOV_SWITCHES) -o $(builddir)/$(srcdir) $(builddir)/$$cfile >> $(gcov-report); \
	done
	$(V)awk -v GCOV_FILES="$(GCOV_FILES)" \
	     -v GCOV_ANNOTATION="$(GCOV_ANNOTATION)" \
       -f $(srcdir)/$(testdir)/gcov.awk $(gcov-report)
endif

test-buildonly: $(builddir)/$(binary)

clean:
	$(V)echo "... clean"
	$(V)rm -rf $(builddir)
	$(V)rm -f *.gcov

info:
	$(V)echo CFILES
	$(V)echo $(CFILES)
	$(V)echo CFLAGS
	$(V)echo $(CFLAGS)
	$(V)echo LDFLAGS
	$(V)echo $(LDFLAGS)
	$(V)echo DEPFILES
	$(V)echo $(DEPFILES)

calculator:
	$(V)$(MAKE) $(TARGET-CALCULATOR) FLAGS="\
	-DSPFS_CFG_DYNAMIC=1 \
	-DSPFS_TEST=0 \
	-DSPFS_ERRSTR=1 \
	-DSPFS_DUMP=0 \
	-DSPFS_ASSERT=0 \
	-DSPFS_DBG_ERROR=0 \
	-DSPFS_DBG_LOWLEVEL=0 \
	-DSPFS_DBG_CACHE=0 \
	-DSPFS_DBG_FILE=0 \
	-DSPFS_DBG_GC=0 \
	-DSPFS_DBG_JOURNAL=0 \
	-DSPFS_DBG_FS=0 \
	"
	
$(TARGET-CALCULATOR): $(builddir)/$(binary)

mkimg:
	$(V)$(MAKE) $(TARGET-MKIMAGE) FLAGS="\
	-DSPFS_MKIMG=1 \
	"

$(TARGET-MKIMAGE): $(builddir)/$(binary)

unpdump:
	$(V)$(MAKE) $(TARGET-UNPDUMP) FLAGS="\
	-DSPFS_MKIMG=1 \
	"

$(TARGET-UNPDUMP): $(builddir)/$(binary)
