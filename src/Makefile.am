lib_LIBRARIES		= libmnutils.a
libmnutils_a_SOURCES	= mnannotate.c
libmnutils_a_CFLAGS	= -fPIC
otherincludedir		= $(includedir)/mnutils
otherinclude_HEADERS	= mnannotate.h

bin_PROGRAMS		= mngrab mndraw mnstitch

mngrab_SOURCES		= mngrab.c
mngrab_CFLAGS		= $(DEBUG) $(LIBAVCODEC_CFLAGS) $(LIBAVFORMAT_CFLAGS) $(LIBAVDEVICE_CFLAGS) \
			  $(LIBSWSCALE_CFLAGS) $(LIBAVUTIL_CFLAGS) $(OPENCV_CFLAGS)
mngrab_LDADD		= libmnutils.a $(LIBAVCODEC_LIBS) $(LIBAVFORMAT_LIBS) $(LIBAVDEVICE_LIBS) \
			  $(LIBSWSCALE_LIBS) $(LIBAVUTIL_LIBS) $(OPENCV_LIBS) \
			  $(OPENCV_LIBS) $(JSON_LIBS)

mndraw_SOURCES		= mndraw.c
mndraw_CFLAGS		= $(DEBUG) $(OPENCV_CFLAGS) $(JSON_CFLAGS)
mndraw_LDADD		= libmnutils.a $(OPENCV_LIBS) $(JSON_LIBS)

mnstitch_SOURCES	= mnstitch.cpp mnstitch.hpp mnstitch_util.cpp mnstitch_util.hpp mnstitch_main.cpp
mnstitch_CXXFLAGS	= $(DEBUG) $(OPENCV_CFLAGS) -std=c++11
mnstitch_LDADD		= $(OPENCV_LIBS)
