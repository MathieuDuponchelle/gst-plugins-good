plugin_LTLIBRARIES = libgstosxaudio.la

libgstosxaudio_la_SOURCES = gstosxaudioringbuffer.c   \
                            gstosxaudioelement.c \
                            gstosxaudiosink.c    \
                            gstosxaudiosrc.c     \
                            gstosxcoreaudiocommon.c     \
                            gstosxcoreaudio.c     \
                            gstosxaudio.c

libgstosxaudio_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
    -Wno-deprecated-declarations
libgstosxaudio_la_LIBADD = 			  \
    -lgstaudio-@GST_API_VERSION@       \
    $(GST_PLUGINS_BASE_LIBS)          \
    $(GST_BASE_LIBS)                  \
    $(GST_LIBS)

libgstosxaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework,CoreAudio  -Wl,-framework,AudioToolbox

if !HAVE_IOS
libgstosxaudio_la_LDFLAGS += -Wl,-framework,AudioUnit -Wl,-framework,CoreServices
endif

libgstosxaudio_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS) --tag=CC

noinst_HEADERS = gstosxaudiosink.h	  \
                 gstosxaudioelement.h \
                 gstosxaudioringbuffer.h	  \
                 gstosxaudiosrc.h     \
                 gstosxcoreaudiocommon.h     \
                 gstosxcoreaudio.h \
                 gstosxcoreaudiohal.c \
                 gstosxcoreaudioremoteio.c



