#                                               -*- Autoconf -*-
################################################################################
# Copyright 2008 VMware, Inc.  All rights reserved.
#
# This file is part of VMware View Open Client.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation version 2.1 and no later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
# License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
################################################################################

# http://www.mail-archive.com/autoconf@gnu.org/msg16720.html
AC_INIT([VMware-view-open-client],
	m4_esyscmd([echo ${VDM_VERSION_NUM:-e.x.p}-${BUILD_NUMBER:-00000} | tr -d '\n']),
	[http://code.google.com/p/vmware-view-open-client/issues/entry],
	[VMware-view-open-client-source])
echo "Configuring $PACKAGE_STRING"
AC_CONFIG_SRCDIR([tunnel/tunnelMain.c])

# Keep the top-level directory tidy by putting auxiliary build tools and local
# macros in separate subdirectories.
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])
AC_PREFIX_DEFAULT([/usr])
AC_CANONICAL_HOST

# Quote the regular expressions
RPM_ARCH="$host_cpu"
DEB_ARCH="$host_cpu"
case "$host_cpu" in
   [i[3456]86])
      RPM_ARCH="i386"
      DEB_ARCH="i386"
      ;;
   x86_64)
      DEB_ARCH="amd64"
      ;;
esac

AC_ARG_ENABLE([eulas],
   [AS_HELP_STRING([--enable-eulas],
      [include EULAs with binaries and source])],
   [],[enable_eulas="no"])
AM_CONDITIONAL(VIEW_EULAS, [test $enable_eulas != "no"])
if test $enable_eulas != "no" ; then
   VIEW_RPM_LICENSE="Commercial"
else
   VIEW_RPM_LICENSE="LGPLv2.1 only"
fi
AC_SUBST(VIEW_RPM_LICENSE)

AC_ARG_ENABLE([cvp],
   [AS_HELP_STRING([--enable-cvp],
      [build the client for use with cvp])],
   [],[enable_cvp="no"])
if test "$enable_cvp" != "no" ; then
   AC_DEFINE(VIEW_CVP)
fi

AC_MSG_CHECKING([which interface to build])
use_gtk="no"
use_cocoa="no"
use_posix="yes"
mingw32="no"
case "$host_os" in
   [darwin*])
      AC_MSG_RESULT([Cocoa])
      AC_DEFINE(VIEW_COCOA)
      AC_DEFINE(VIEW_POSIX)
      use_cocoa="yes"
      ;;
   [mingw32*])
      AC_MSG_RESULT([Gtk+])
      AC_DEFINE(VIEW_GTK)
      AC_DEFINE(_WIN32)
      AC_DEFINE(__MINGW32__)
      AC_DEFINE(__MSVCRT__)
      AC_DEFINE(__MSVCRT_VERSION__,0x0601)
      AC_DEFINE(WINVER,0x0501)
      AC_DEFINE(USERLEVEL)
      AC_DEFINE(U_HAVE_INT8_T)
      AC_DEFINE(U_HAVE_UINT8_T)
      AC_DEFINE(U_HAVE_INT16_T)
      AC_DEFINE(U_HAVE_UINT16_T)
      AC_DEFINE(U_HAVE_INT32_T)
      AC_DEFINE(U_HAVE_UINT32_T)
      AC_DEFINE(U_HAVE_INT64_T)
      AC_DEFINE(U_HAVE_UINT64_T)
      mingw32="yes"
      use_gtk="yes"
      use_posix="no"
      ;;
   [*])
      AC_MSG_RESULT([Gtk+])
      AC_DEFINE(VIEW_GTK)
      AC_DEFINE(VIEW_POSIX)
      use_gtk="yes"
      ;;
esac
AM_CONDITIONAL([VIEW_GTK], [test "$use_gtk" = "yes"])
AM_CONDITIONAL([VIEW_COCOA], [test "$use_cocoa" = "yes"])
AM_CONDITIONAL([VIEW_CVP], [test "$enable_cvp" = "yes"])
AM_CONDITIONAL([VIEW_WIN32], [test "$mingw32" = "yes"])
AM_CONDITIONAL([VIEW_POSIX], [test "$use_posix" = "yes"])

AC_ARG_WITH([pkg-config],
   [AS_HELP_STRING([--without-pkg-config],
      [don't use pkg-config to find flags and libs])],
   [],[with_pkg_config="yes"])
if test "$with_pkg_config" = "yes" ; then
   if test -z "$GENCCODE" ; then
      GENCCODE="/usr/sbin/genccode"
   fi
   if test -z "$GENSPREP" ; then
      GENSPREP="/usr/sbin/gensprep"
   fi
   if test -z "$GZIP" ; then
      GZIP=gzip
   fi
   if test -z "$ICUPKG" ; then
      ICUPKG="/usr/sbin/icupkg"
   fi
   if test -z "$MKSQUASHFS" ; then
      MKSQUASHFS=mksquashfs
   fi
   if test -z "$RPMBUILD" ; then
      RPMBUILD=rpmbuild
   fi

   # Need this manually since the first call is conditional.
   PKG_PROG_PKG_CONFIG
   PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.6.0 gmodule-2.0 >= 2.6.0])
   if test "$use_gtk" = "yes" ; then
      PKG_CHECK_MODULES(GTK, [gtk+-2.0 >= 2.4.0])
   fi
   PKG_CHECK_MODULES(XML, [libxml-2.0 >= 2.6.0])
   PKG_CHECK_MODULES(CURL, [libcurl >= 7.16.0])
   case "$host_os" in
      [darwin*])
         # the -arch flags confuse automake at some point
         CURL_LIBS=`$PKG_CONFIG --libs-only-l libcurl`
         # Leopard still ships with 0.9.7.
         OPENSSL_MIN_VER="0.9.7"
         ;;
      [*])
         OPENSSL_MIN_VER="0.9.8"
         ;;
   esac
   PKG_CHECK_MODULES(SSL, [openssl >= $OPENSSL_MIN_VER])
fi

# Checks for programs.
# This needs to be after TCROOT is set to pick up the correct paths
# for mawk et. al.
AM_INIT_AUTOMAKE([foreign tar-pax])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_OBJC
AM_PROG_CC_C_O
AM_PROG_AS
AC_PROG_LN_S
AC_PROG_RANLIB
AC_PROG_SED
AC_PATH_PROG([ADD_LICENSE],[AddLicense])
AC_PATH_PROG([AR],[ar])
AC_PATH_PROG([DATE],[date])
AC_PATH_PROG([DPKG_DEB],[dpkg-deb])
AC_PATH_PROG([FAKEROOT],[fakeroot])
AC_PATH_PROG([GDK_PIXBUF_CSOURCE],[gdk-pixbuf-csource])
AC_PATH_PROG([GENCCODE],[genccode])
AC_PATH_PROG([GENSPREP],[gensprep])
AC_PATH_PROG([GZIP],[gzip])
AC_PATH_PROG([HDIUTIL],[hdiutil])
AC_PATH_PROG([ICUPKG],[icupkg])
AC_PATH_PROG([INSTALL_NAME_TOOL],[install_name_tool])
AC_PATH_PROG([MKSQUASHFS],[mksquashfs])
AC_PATH_PROG([OTOOL],[otool])
AC_PATH_PROG([RPMBUILD],[rpmbuild])
AC_PATH_PROG([STRIP],[strip])
AC_PATH_PROG([UNZIP],[unzip])
AC_PROG_INTLTOOL([0.21])

GETTEXT_PACKAGE="vmware-view"
AC_SUBST(GETTEXT_PACKAGE)
# Change this when [language].po files are added to cdk/po/.
ALL_LINGUAS="de fr ja zh_CN"
AC_SUBST(ALL_LINGUAS)
AM_GLIB_GNU_GETTEXT

# This needs to be after AC_PROG_CC and friends
if test "$with_pkg_config" = "yes" ; then
   AX_BOOST_BASE([1.34.1])
   AX_BOOST_SIGNALS
fi

AC_ARG_WITH([icons-dir],
   [AS_HELP_STRING([--with-icons-dir=DIR],
      [Use icon files found in DIR])],
   [],[with_icons_dir="$srcdir/icons/open"])
AC_MSG_CHECKING([for required icon files])
for i in view_16x.png view_32x.png view_48x.png view_client_banner.png ; do
   echo $ECHO_N " $i"
   if ! test -f $with_icons_dir/$i ; then
      echo " missing!"
      AC_MSG_ERROR([$i was not found in $with_icons_dir])
   fi
done
AC_MSG_RESULT([ all present and accounted for])
ICONS_DIR="$with_icons_dir"
have_pcoip_banner="no"
if test -f $with_icons_dir/view_client_banner_pcoip.png ; then
   AC_DEFINE(HAVE_PCOIP_BANNER)
   have_pcoip_banner="yes"
fi
AM_CONDITIONAL([VIEW_PCOIP_BANNER], [test "$have_pcoip_banner" = "yes"])

AC_ARG_WITH([packaging-name],
   [AS_HELP_STRING([--with-packages-name=NAME],
      [use NAME for the base name of output packages])],
   [PACKAGING_NAME=$with_packaging_name],[PACKAGING_NAME=$PACKAGE_NAME])
changequote(,)dnl
DEB_PACKAGING_NAME=`echo $PACKAGING_NAME | tr [:upper:] [:lower:]`
changequote([,])dnl
if test "$PACKAGING_NAME" != "VMware-view-client" ; then
   DEB_REPLACES="$DEB_REPLACES, vmware-view-client"
   DEB_CONFLICTS="$DEB_CONFLICTS, vmware-view-client"
   RPM_CONFLICTS="$RPM_CONFLICTS VMware-view-client"
   RPM_CONFLICTS_LITE="$RPM_CONFLICTS_LITE VMware-view-client-lite"
fi
if test "$PACKAGING_NAME" != "VMware-view-open-client" ; then
   DEB_REPLACES="$DEB_REPLACES, vmware-view-open-client"
   DEB_CONFLICTS="$DEB_CONFLICTS, vmware-view-open-client"
   RPM_CONFLICTS="$RPM_CONFLICTS VMware-view-open-client"
   RPM_CONFLICTS_LITE="$RPM_CONFLICTS_LITE VMware-view-open-client-lite"
fi

AC_ARG_WITH([product-name],
   [AS_HELP_STRING([--with-product-name=NAME],
      [use NAME as the user-visable product name])],
   [PRODUCT_VIEW_CLIENT_NAME=$with_product_name],
   [PRODUCT_VIEW_CLIENT_NAME="VMware View Open Client"])
AC_DEFINE_UNQUOTED([PRODUCT_VIEW_CLIENT_NAME],["$PRODUCT_VIEW_CLIENT_NAME"])

BUILD_THINCLIENT_RPM=0
case "$host_os" in
   [linux*])
      if test "$RPMBUILD" ; then
         if test "$MKSQUASHFS" ; then
            BUILD_THINCLIENT_RPM=1
         else
            AC_MSG_WARN([mksquashfs was not found; disabling thinclient RPM])
         fi
      else
         AC_MSG_WARN([rpmbuild was not found; disabling RPMs])
      fi
      if test "$DPKG_DEB" ; then
         if test "$FAKEROOT" ; then
	    # This is only needed by the Debian change log, and OS X's
	    # date doesn't support -R, so only do this here.
	    DATE_R=`$DATE -R`
	 else
            AC_MSG_WARN([fakeroot was not found; disabling deb])
            DPKG_DEB=
         fi
      else
         AC_MSG_WARN([dpkg-deb was not found; disabling deb])
      fi
      ;;
   [*])
      RPMBUILD=
      DPKG_DEB=
      ;;
esac

VIEW_VERSION=`echo $PACKAGE_VERSION | $SED -e 's/-.*//'`
changequote(,)dnl
BUILD_NUMBER=`echo $PACKAGE_VERSION | $SED -e 's/[^-].*-//'`
changequote([,])dnl
BUILD_MAJOR=m4_esyscmd([echo ${BUILD_MAJOR:-3} | tr -d '\n'])
BUILD_MINOR=m4_esyscmd([echo ${BUILD_MINOR:-1} | tr -d '\n'])
BUILD_REV=m4_esyscmd([echo ${BUILD_REV:-0} | tr -d '\n'])
VIEW_VERSION_NUM="${BUILD_MAJOR}.${BUILD_MINOR}.${BUILD_REV}"
PRODUCT_BUILD_NUMBER=m4_esyscmd([echo ${PRODUCT_BUILD_NUMBER:-0} | tr -d '\n'])

AC_DEFINE_UNQUOTED([VIEW_CLIENT_VERSION_NUMBER],["$VIEW_VERSION"])

AC_ARG_WITH([buildtype],
   [AS_HELP_STRING([--with-buildtype=obj|beta|release],
      [configures debugging and optimization flags])],
   [],[with_buildtype="obj"])

AC_DEFINE(VMX86_TOOLS)
if test "$with_buildtype" = "obj" -o "$with_buildtype" = "beta" ; then
   AC_DEFINE(VMX86_DEBUG)
fi
if test "$with_buildtype" = "obj" -o "$with_buildtype" = "opt" ; then
   AC_DEFINE(VMX86_DEVEL)
   AC_DEFINE(VMX86_STATS)
   AC_DEFINE(DEBUG_STUB_ACCESS_IO_PORTS)
fi
if test "$with_buildtype" = "obj" ; then
   AC_DEFINE(VMX86_LOG)
fi
if test "$with_buildtype" = "release" ; then
   AC_DEFINE(VMX86_RELEASE)
fi
if test "$with_buildtype" = "beta" ; then
   AC_DEFINE(VMX86_BETA)
fi
CPPFLAGS="$CPPFLAGS -DBUILT_BY_$USER"
AC_DEFINE(DEBUG_STUB)
case "$host_os" in
   [linux*])
      AC_DEFINE(_XOPEN_SOURCE,[500])
      AC_DEFINE(_BSD_SOURCE)
      AC_DEFINE(_SVID_SOURCE)
      AC_DEFINE(_LARGEFILE64_SOURCE)
      AC_DEFINE(_FILE_OFFSET_BITS,[64])
      GENCCODE_ASM_TARGET="gcc"
      ;;
   [mingw32*])
      GENCCODE_ASM_TARGET="gcc"
      LIBS="$LIBS -lgdi32 -lws2_32 -lintl"
      ;;
   [darwin*])
      for fw in AppKit Cocoa CoreData CoreServices Foundation \
                Security SecurityInterface SystemConfiguration ; do
	 VIEW_FRAMEWORKS="$VIEW_FRAMEWORKS -Wl,-framework -Wl,$fw"
      done
      for fw in CoreServices Security SystemConfiguration ; do
         TUNNEL_FRAMEWORKS="$TUNNEL_FRAMEWORKS -Wl,-framework -Wl,$fw"
      done
      GENCCODE_ASM_TARGET="gcc-darwin"
      ;;
   [solaris*])
      LIB_USER_CPPFLAGS="$LIB_USER_CPPFLAGS -D_POSIX_PTHREAD_SEMANTICS"
      ;;
esac

AC_ARG_ENABLE([static-icu],
   [AS_HELP_STRING([--enable-static-icu],
      [statically link against libicu])],
   [],[enable_static_icu="no"])
if test "$enable_static_icu" = "no" ; then
   ICU_LIBS="$ICU_LIBS -licuuc -licudata"
else
   ICU_LIBS="$ICU_LIBS -lsicuuc"
   AC_MSG_CHECKING([for U_ICUDATA_ENTRY_POINT])
   # Sadly AC_EGREP_CPP doesn't let us grab the result of the grep, so
   # it's somewhat duplicated here.
   AC_LANG_CONFTEST([AC_LANG_SOURCE(
      [#include <unicode/udata.h>
vm_grep_value=U_ICUDATA_ENTRY_POINT])])
   CPPFLAGS_save="$CPPFLAGS"
   CPPFLAGS="$CPPFLAGS $ICU_CPPFLAGS"
   vm_grep_value=
   AS_IF([(eval "$ac_cpp conftest.$ac_ext") 2>&AS_MESSAGE_LOG_FD |
      $EGREP "^vm_grep_value=" > conftest.grep 2>/dev/null],
      [eval `cat conftest.grep`])
   CPPFLAGS="$CPPFLAGS_save"
   rm -f conftest*
   if test "$vm_grep_value" ; then
      ICUDATA_ENTRY_POINT=`echo $vm_grep_value | sed -e s/_dat//`
      AC_MSG_RESULT([$ICUDATA_ENTRY_POINT])
   else
      AC_MSG_RESULT([unknown])
      AC_MSG_FAILURE([Could not determine the value of U_ICUDATA_ENTRY_POINT; check config.log for details.])
   fi
fi
AM_CONDITIONAL([STATIC_ICU],[test "$enable_static_icu" != "no"])

AC_CHECK_FUNCS([lseek],[LIB_FILE_CPPFLAGS="$LIB_FILE_CPPFLAGS -DGLIBC_VERSION_21"])
AC_CHECK_HEADER([wchar.h],[LIB_STRING_CPPFLAGS="$LIB_STRING_CPPFLAGS -DGLIBC_VERSION_22"])
# Check to see if SYS_setuid32 is defined. If SYS_setuid32 is unknown, we will
# certainly error out.
AC_MSG_CHECKING([for SYS_setuid32])
AC_TRY_COMPILE(
   [#include <sys/syscall.h>
    #include <unistd.h>],
   [#ifndef __LP64__
    syscall(SYS_setuid32, 1);
    #endif],
   [HAVE_SYS_SETUID="yes"],
   [HAVE_SYS_SETUID="no"])
AC_MSG_RESULT($HAVE_SYS_SETUID)
if test "$HAVE_SYS_SETUID" = "yes"; then
   LIB_MISC_CPPFLAGS="$LIB_MISC_CPPFLAGS -DGLIBC_VERSION_21"
fi

# Newer iconv (from glibc 2.2 and later on Linux) takes a char ** as the second
# argument. Here, we test for the older form and expect a compiler warning/error
# if the newer form is defined (because, in that case, we would be attempting to
# remove const'ness without an explicit cast).
AC_MSG_CHECKING([for the new format of iconv])
ORIGINAL_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Werror"
AC_TRY_COMPILE(
   [#include <iconv.h>],
   [
      iconv_t cd;
      char const *inbuf;
      size_t inbytesleft;
      char *outbuf;
      size_t outbytesleft;

      iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
   ],
   [HAVE_NEW_ICONV="no"],
   [HAVE_NEW_ICONV="yes"])
CFLAGS="$ORIGINAL_CFLAGS"
AC_MSG_RESULT($HAVE_NEW_ICONV)
if test "$HAVE_NEW_ICONV" = "yes" ; then
   LIB_MISC_CPPFLAGS="$LIB_MISC_CPPFLAGS -DGLIBC_VERSION_22"
fi

AC_MSG_CHECKING([whether sys/ucontext.h defines REG_EAX])
AC_COMPILE_IFELSE(
   [AC_LANG_PROGRAM(
      [[#define _GNU_SOURCE
        #include <signal.h>
        #include <sys/ucontext.h>]],
      [[int foo = REG_EAX; return foo;]])],
   [
      LIB_SIG_CPPFLAGS="$LIB_SIG_CPPFLAGS -DGLIBC_VERSION_22"
      AC_MSG_RESULT([yes])
   ],
   [
      AC_MSG_RESULT([no])
   ])

CPPFLAGS_save="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $ICU_CPPFLAGS"
AC_CHECK_HEADER([unicode/uidna.h],,
   [AC_MSG_ERROR([libicu is required, but not found.  Check config.log for details.])])

# Due to icu symbol version renaming, we can't do a simple
# AC_CHECK_LIB here...
AC_MSG_CHECKING([for uidna_IDNToASCII in -licuuc])
LIBS_save="$LIBS"
LIBS="$LIBS $ICU_LIBS"
if test "$enable_static_icu" != "no" -a "$mingw32" != "yes" ; then
   LIBS="$LIBS -lsicudata"
fi
AC_LANG_PUSH([C++])
AC_LINK_IFELSE([AC_LANG_CALL([#include <unicode/urename.h>],[uidna_IDNToASCII])],
   [AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])
    AC_MSG_ERROR([libicu is required, but not found.  Check config.log for details.])])
AC_LANG_POP([C++])
LIBS="$LIBS_save"
if test "$enable_static_icu" != "no" ; then
   if test -z "$GENCCODE" ; then
      AC_MSG_ERROR([genccode is required when linking ICU statically.])
   fi
   if test -z "$GENSPREP" ; then
      AC_MSG_ERROR([gensprep is required when linking ICU statically.])
   fi
   if test -z "$ICUPKG" ; then
      AC_MSG_ERROR([icupkg is required when linking ICU statically.])
   fi
fi
AC_DEFINE(HAVE_UIDNA_IDNTOASCII)
CPPFLAGS="$CPPFLAGS_save"

AC_CHECK_HEADERS([dlfcn.h arpa/inet.h])
AC_CHECK_FUNCS([localtime_r])

RPM_STAGE_ROOT="`pwd`/rpm-stage"
RPM_OUTPUT_DIR="`pwd`/rpm-output"

# docdir defaults to the tarball name; we don't want to include
# "-source"
docdir='${datarootdir}/doc/${PACKAGING_NAME}'

# workarounds for automake/gnumake's keyword collisions
ELSE=else
ENDIF=endif

AC_SUBST(LIB_FILE_CPPFLAGS)
AC_SUBST(LIB_MISC_CPPFLAGS)
AC_SUBST(LIB_SIG_CPPFLAGS)
AC_SUBST(LIB_STRING_CPPFLAGS)
AC_SUBST(LIB_USER_CPPFLAGS)

AC_SUBST(VIEW_VERSION)
AC_SUBST(VIEW_VERSION_NUM)
AC_SUBST(BUILD_NUMBER)
AC_SUBST(PRODUCT_BUILD_NUMBER)
AC_SUBST(PACKAGING_NAME)
AC_SUBST(PRODUCT_VIEW_CLIENT_NAME)
AC_SUBST(ICONS_DIR)

AC_SUBST(DEB_PACKAGING_NAME)
AC_SUBST(DEB_REPLACES)
AC_SUBST(DEB_CONFLICTS)
AC_SUBST(RPM_CONFLICTS)
AC_SUBST(RPM_CONFLICTS_LITE)

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

AC_SUBST(ICU_CPPFLAGS)
AC_SUBST(ICU_LIBS)
AC_SUBST(ICUDATA_ENTRY_POINT)
AC_SUBST(GENCCODE_ASM_TARGET)

AC_SUBST(XML_CFLAGS)
AC_SUBST(XML_LIBS)

AC_SUBST(CURL_CFLAGS)
AC_SUBST(CURL_LIBS)

AC_SUBST(SSL_CFLAGS)
AC_SUBST(SSL_LIBS)

AC_SUBST(TUNNEL_FRAMEWORKS)
AC_SUBST(VIEW_FRAMEWORKS)

AC_SUBST(BUILD_THINCLIENT_RPM)
AC_SUBST(DATE_R)
AC_SUBST(RPM_STAGE_ROOT)
AC_SUBST(RPM_OUTPUT_DIR)
AC_SUBST(RPM_ARCH)
AC_SUBST(DEB_ARCH)

AC_SUBST(ELSE)
AC_SUBST(ENDIF)

AC_CONFIG_FILES([
Info.plist
Makefile
VMware-view-open-client.control
VMware-view-open-client.spec
buildNumber.h
doc/changelog.Debian
doc/vmware-view.1
doc/vmware-view-tunnel.1
fix-toolchain-paths.sh
intltool-update
intltool-merge
intltool-extract
po/Makefile.in
vmware-view.desktop.in
])

AC_OUTPUT

dnl  LocalWords:  boostversion
