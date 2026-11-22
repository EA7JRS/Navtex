AC_INIT([navtex], [1.7.0])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CXX
AC_PROG_CC
AC_CHECK_LIB(cjson,cJSON_CreateArray)
AC_CHECK_LIB(dl,dlopen)
AC_CHECK_LIB(sqlite3,sqlite3_open)
AC_CHECK_LIB(microhttpd,MHD_start_daemon)
AC_CHECK_LIB(m,atan2)
# The SDRplay API installs under /usr/local
CPPFLAGS="$CPPFLAGS -I/usr/local/include"
LDFLAGS="$LDFLAGS -L/usr/local/lib"
# Receivers: each one is optional, but at least one is needed
AC_CHECK_LIB(sdrplay_api,sdrplay_api_Open,[have_sdrplay=yes],[have_sdrplay=no])
AC_CHECK_LIB(rtlsdr,rtlsdr_open,[have_rtlsdr=yes],[have_rtlsdr=no])
RECEIVER_LIBS=""
if test "$have_sdrplay" = yes; then
  AC_DEFINE([HAVE_LIBSDRPLAY_API],[1],[SDRplay receivers])
  RECEIVER_LIBS="$RECEIVER_LIBS -lsdrplay_api"
fi
if test "$have_rtlsdr" = yes; then
  AC_DEFINE([HAVE_LIBRTLSDR],[1],[RTL-SDR receivers])
  RECEIVER_LIBS="$RECEIVER_LIBS -lrtlsdr"
  AC_CHECK_LIB(rtlsdr,rtlsdr_set_bias_tee,[AC_DEFINE([HAVE_RTLSDR_BIAS_TEE],[1],[bias T control])])
fi
AC_SUBST([RECEIVER_LIBS])
if test "$have_sdrplay" = no && test "$have_rtlsdr" = no; then
  AC_MSG_WARN([neither the SDRplay API nor librtlsdr were found: navtexreceiver will only decode recordings])
fi
AC_CONFIG_FILES([Makefile receiver/Makefile web_server/Makefile])
AC_OUTPUT
AC_MSG_NOTICE([receivers: SDRplay $have_sdrplay, RTL-SDR $have_rtlsdr])

