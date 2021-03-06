dnl **********************************************************************
dnl * configure.ac
dnl *
dnl *  Fineltra build configuration.
dnl *
dnl ***********************************************************************/

AC_INIT()
AC_CONFIG_HEADERS([fineltra_config.h])

dnl
dnl Compilers
dnl
AC_PROG_CC

dnl
dnl Define executable suffix to use for utility programs
dnl
EXESUFFIX="$ac_cv_exeext"
AC_SUBST([EXESUFFIX])

dnl
dnl Search for flex/bison to build the parser
dnl
dnl AC_PROG_LEX
dnl AC_PROG_YACC
dnl AC_SUBST([LEX])
dnl AC_SUBST([YACC])

dnl ===========================================================================
dnl Version Information imported from Version.config
dnl ===========================================================================

FINELTRA_VERSION=`cat ${srcdir}/Version.config`
AC_SUBST([FINELTRA_VERSION])
AC_DEFINE_UNQUOTED([FINELTRA_VERSION], ["$FINELTRA_VERSION"], [Fineltra version])

AC_SUBST([CUNIT_LDFLAGS])
AC_SUBST([CUNIT_CPPFLAGS])


dnl ===========================================================================
dnl Detect the version of PostgreSQL installed on the system
dnl ===========================================================================

AC_ARG_WITH([pgconfig], 
	[AS_HELP_STRING([--with-pgconfig=FILE], [specify an alternative pg_config file])], 
	[PG_CONFIG="$withval"], [PG_CONFIG=""])

if test "x$PG_CONFIG" = "x"; then
	dnl PG_CONFIG was not specified, so search within the current path
	AC_PATH_PROG([PG_CONFIG], [pg_config])

	dnl If we couldn't find pg_config, display an error
	if test "x$PG_CONFIG" = "x"; then
		AC_MSG_ERROR([could not find pg_config within the current path. You may need to try re-running configure with a --with-pgconfig parameter.])
	fi
else
	dnl PG_CONFIG was specified; display a message to the user
	if test "x$PG_CONFIG" = "xyes"; then
		AC_MSG_ERROR([you must specify a parameter to --with-pgconfig, e.g. --with-pgconfig=/path/to/pg_config])
	else
		if test -f $PG_CONFIG; then
			AC_MSG_RESULT([Using user-specified pg_config file: $PG_CONFIG])
		else
			AC_MSG_ERROR([the user-specified pg_config file $PG_CONFIG does not exist])
		fi
	fi
fi


dnl ===========================================================================
dnl Ensure that $PG_CONFIG --pgxs points to a valid file. This is because some 
dnl distributions such as Debian also include pg_config as part of libpq-dev 
dnl packages, but don't install the Makefile it points to unless
dnl the postgresql-server-dev packages are installed :)
dnl ===========================================================================

PGXS=`$PG_CONFIG --pgxs`
if test ! -f $PGXS; then
	AC_MSG_ERROR([the PGXS Makefile $PGXS cannot be found. Please install the PostgreSQL server development packages and re-run configure.])
fi

AC_SUBST([PG_CONFIG])
AC_SUBST([PGXS])

dnl Extract the version information from pg_config
dnl Note: we extract the major & minor separately, ensure they are numeric, and then combine to give
dnl the final version. This is to guard against user error... 
PGSQL_MAJOR_VERSION=`$PG_CONFIG --version | sed 's/[[A-Za-z ]]*//' | cut -d. -f1 | sed 's/[[^0-9]]//g'`	
PGSQL_MINOR_VERSION=`$PG_CONFIG --version | sed 's/[[A-Za-z ]]*//' | cut -d. -f2 | sed 's/[[^0-9]]//g'`	
PGSQL_FULL_VERSION=`$PG_CONFIG --version`
PGSQL_VERSION="$PGSQL_MAJOR_VERSION$PGSQL_MINOR_VERSION"

PGSQL_PKGLIBDIR=`$PG_CONFIG --pkglibdir`
PGSQL_LIBDIR=`$PG_CONFIG --libdir`
PGSQL_SHAREDIR=`$PG_CONFIG --sharedir`

AC_MSG_RESULT([checking PostgreSQL version... $PGSQL_FULL_VERSION])
dnl Ensure that we are using PostgreSQL >= 9.0
if test ! "$PGSQL_MAJOR_VERSION" -ge 9; then
	AC_MSG_ERROR([Fineltra requires PostgreSQL >= 9.0])
fi	

AC_DEFINE_UNQUOTED([PGSQL_VERSION], [$PGSQL_VERSION], [PostgreSQL server version])	
AC_SUBST([PGSQL_VERSION])

dnl ===========================================================================
dnl Search for liblwgeom static library (2.0+ needed)
dnl ===========================================================================

AC_ARG_WITH([liblwgeom],
	[AS_HELP_STRING([--with-liblwgeom=FILE], [specify path to liblwgeom.a])],
	[LIBLWGEOM="$withval"], [])

if test x"$LIBLWGEOM" != "x"; then
  if test -e "$LIBLWGEOM"; then
    LWGEOM_LDFLAGS=$LIBLWGEOM
  else
	  AC_MSG_ERROR([$LIBLWGEOM: no such file or directory])
  fi
else
  for dir in /usr/lib /usr/local/lib; do
    if test -e "$dir"/liblwgeom.a; then
      LWGEOM_LDFLAGS=${dir}/liblwgeom.a
      break
    fi
  done
  if test x"$LWGEOM_LDFLAGS" = "x"; then
	  AC_MSG_ERROR([Cannot find liblwgeom.a, try --with-liblwgeom])
  fi
fi

AC_SUBST(LWGEOM_LDFLAGS)
AC_SUBST(LWGEOM_CFLAGS)

dnl ===========================================================================
dnl Output the relevant files
dnl ===========================================================================

AC_OUTPUT([ 
   Makefile
])
    
dnl ===========================================================================
dnl Display the configuration status information
dnl ===========================================================================

AC_MSG_RESULT()
AC_MSG_RESULT([  Fineltra is now configured for ${host}])
AC_MSG_RESULT()
AC_MSG_RESULT([ -------------- Compiler Info ------------- ])
AC_MSG_RESULT([  C compiler:           ${CC} ${CFLAGS}])
AC_MSG_RESULT()
AC_MSG_RESULT([ -------------- Dependencies -------------- ])
AC_MSG_RESULT([  PostgreSQL config:    ${PG_CONFIG}])
AC_MSG_RESULT([  PostgreSQL version:   ${PGSQL_FULL_VERSION}])
AC_MSG_RESULT([  liblwgeom ldflags:    ${LWGEOM_LDFLAGS}])
AC_MSG_RESULT([  liblwgeom cflags:     ${LWGEOM_CFLAGS}])
AC_MSG_RESULT()
