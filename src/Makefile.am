AM_CFLAGS = 

INCLUDES = 	\
	-DDATADIR=\""$(datadir)"\" 	\
	-DG_LOG_DOMAIN=\"SeqDraw\"\
	-DG_DISABLE_DEPRECATED 		\
	-DGDK_DISABLE_DEPRECATED 	\
	-DGDK_PIXBUF_DISABLE_DEPRECATED	\
	-DGTK_DISABLE_DEPRECATED	\
	-DGNOME_DISABLE_DEPRECATED	\
	-I$(top_srcdir) -I$(srcdir)

# Add some special flags to get specific behaviors on windows.
if WIN32
AM_CFLAGS += \
        -mms-bitfields  \
        -mwindows
endif

# testing executables
bin_PROGRAMS = seqdraw 

seqdraw_SOURCES = seqdraw.c sqd-layout.c sqd-util.c

seqdraw_CFLAGS = $(REQMOD_CFLAGS) 
seqdraw_LDADD = $(REQMOD_LIBS) 


