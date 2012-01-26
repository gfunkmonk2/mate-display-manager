/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */
#define PACKAGE "mdm"
#define VERSION "1.0.8"
#define GETTEXT_PACKAGE "mdm"
/* #undef HALT_COMMAND */
/* #undef REBOOT_COMMAND */
#define SOUND_PROGRAM ""
/* #undef SUSPEND_COMMAND */
#define ENABLE_IPV6 1
#define ENABLE_NLS 1
/* #undef ALWAYS_RESTART_SERVER */
/* #undef HAVE_ADT */
/* #undef HAVE_CATGETS */
/* #undef HAVE_CHKAUTHATTR */
/* #undef HAVE_CHPASS */
#define HAVE_CLEARENV 1
/* #undef HAVE_CRYPT */
#define HAVE_GETTEXT 1
#define HAVE_LC_MESSAGES 1
/* #undef HAVE_LIBSM */
#define HAVE_LIBXDMCP 1
/* #undef HAVE_LOGINCAP */
/* #undef HAVE_LOGINDEVPERM */
/* #undef HAVE_LOGINRESTRICTIONS */
/* #undef HAVE_PAM */
/* #undef HAVE_PASSWDEXPIRED */
#define HAVE_SCHED_YIELD 1
#define HAVE_SELINUX 1
#define HAVE_SETENV 1
#define HAVE_SETRESUID 1
/* #undef HAVE_SHADOW */
/* #undef HAVE_SOLARIS_XINERAMA */
/* #undef HAVE_STPCPY */
/* #undef HAVE_SYS_SOCKIO_H */
#define HAVE_TCPWRAPPERS 1
#define HAVE_UNSETENV 1
/* #undef HAVE_UT_SYSLEN */
#define HAVE_XINERAMA 1
#define HAVE_XFREE_XINERAMA 1
#define HAVE_XINPUT 1
#define X_SERVER "/usr/bin/Xorg"
#define X_SERVER_PATH "/usr/bin"
#define X_XNEST_CMD ""
#define X_XNEST_CONFIG_OPTIONS ""
#define X_XNEST_UNSCALED_FONTPATH "true"
#define X_CONFIG_OPTIONS "-audit 0"
/* #undef X_PATH */
#define XSESSION_SHELL "/bin/sh"
#define XEVIE_OPTION ""
#define HAVE_LOGIN 1
#define HAVE_LOGOUT 1
#define HAVE_LOGWTMP 1


/* Define this variable if the code to use the setpenv function can be
   compiled and used */
/* #undef CAN_USE_SETPENV */

/* Define if we have ipv6 */
#define ENABLE_IPV6 1

/* always defined to indicate that i18n is enabled */
#define ENABLE_NLS 1

/* enable profiling */
#define ENABLE_PROFILING 1

/* Set if we build with RBAC support */
/* #undef ENABLE_RBAC_SHUTDOWN */

/* gettext package */
#define GETTEXT_PACKAGE "mdm"

/* Define if have adt */
/* #undef HAVE_ADT */

/* Define to 1 if you have the `bind_textdomain_codeset' function. */
#define HAVE_BIND_TEXTDOMAIN_CODESET 1

/* Define to 1 if you have the `clearenv' function. */
#define HAVE_CLEARENV 1

/* Define to 1 if you have the <crt_externs.h> header file. */
/* #undef HAVE_CRT_EXTERNS_H */

/* Define to 1 if you have the `dcgettext' function. */
#define HAVE_DCGETTEXT 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <execinfo.h> header file. */
#define HAVE_EXECINFO_H 1

/* Define if the GNU gettext() function is already present or preinstalled. */
#define HAVE_GETTEXT 1

/* Define to 1 if you have the `getutxent' function. */
#define HAVE_GETUTXENT 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if your <locale.h> file defines LC_MESSAGES. */
#define HAVE_LC_MESSAGES 1

/* linux audit support */
/* #undef HAVE_LIBAUDIT */

/* Define if we have libgen.h */
#define HAVE_LIBGEN_H 1

/* Define to 1 if you have the <libutil.h> header file. */
/* #undef HAVE_LIBUTIL_H */

/* Define if have libxdmcp */
#define HAVE_LIBXDMCP 1

/* Define if we have libxklavier */
#define HAVE_LIBXKLAVIER /**/

/* Define to 1 if you have the <locale.h> header file. */
#define HAVE_LOCALE_H 1

/* Define if have login */
#define HAVE_LOGIN 1

/* Define if we have logincap */
/* #undef HAVE_LOGINCAP */

/* Define if have logout */
#define HAVE_LOGOUT 1

/* Define if have logwtmp */
#define HAVE_LOGWTMP 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Have GlibC function to make temp dirs */
#define HAVE_MKDTEMP 1

/* Have non-POSIX function getpwnam_r */
/* #undef HAVE_NONPOSIX_GETPWNAM_R */

/* Define to 1 if you have the pam_syslog function */
#define HAVE_PAM_SYSLOG /**/

/* Have POSIX function getpwnam_r */
#define HAVE_POSIX_GETPWNAM_R 1

/* Define if we have sched yield */
#define HAVE_SCHED_YIELD 1

/* Define to 1 if you have the <security/pam_ext.h> header file. */
#define HAVE_SECURITY_PAM_EXT_H 1

/* Define to 1 if you have the <security/pam_modutil.h> header file. */
#define HAVE_SECURITY_PAM_MODUTIL_H 1

/* Define if have selinux */
#define HAVE_SELINUX 1

/* Define to 1 if you have the `setenv' function. */
#define HAVE_SETENV 1

/* Define to 1 if you have the `setresuid' function. */
#define HAVE_SETRESUID 1

/* Define if have Solaris xinerama */
/* #undef HAVE_SOLARIS_XINERAMA */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <stropts.h> header file. */
#define HAVE_STROPTS_H 1

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/prctl.h> header file. */
#define HAVE_SYS_PRCTL_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define if we have sys/sockio.h */
/* #undef HAVE_SYS_SOCKIO_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define if have tcp wrappers */
#define HAVE_TCPWRAPPERS 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `unsetenv' function. */
#define HAVE_UNSETENV 1

/* Define to 1 if you have the `updwtmp' function. */
#define HAVE_UPDWTMP 1

/* Define to 1 if you have the `updwtmpx' function. */
#define HAVE_UPDWTMPX 1

/* Define if we have UPower */
#define HAVE_UPOWER /**/

/* Define to 1 if you have the <utmpx.h> header file. */
#define HAVE_UTMPX_H 1

/* Define to 1 if you have the <utmp.h> header file. */
#define HAVE_UTMP_H 1

/* Define if have ut_syslen record */
/* #undef HAVE_UT_SYSLEN */

/* Define if your utmp struct contains a ut_exit.e_termination field. */
#define HAVE_UT_UT_EXIT_E_TERMINATION 1

/* Define if your utmp struct contains a ut_host field. */
#define HAVE_UT_UT_HOST 1

/* Define if your utmp struct contains a ut_id field. */
#define HAVE_UT_UT_ID 1

/* Define if your utmp struct contains a ut_name field. */
#define HAVE_UT_UT_NAME 1

/* Define if your utmp struct contains a ut_pid field. */
#define HAVE_UT_UT_PID 1

/* Define if your utmp struct contains a ut_syslen field. */
/* #undef HAVE_UT_UT_SYSLEN */

/* Define if your utmp struct contains a ut_time field. */
#define HAVE_UT_UT_TIME 1

/* Define if your utmp struct contains a ut_tv field. */
#define HAVE_UT_UT_TV 1

/* Define if your utmp struct contains a ut_type field. */
#define HAVE_UT_UT_TYPE 1

/* Define if your utmp struct contains a ut_user field. */
#define HAVE_UT_UT_USER 1

/* Define if have xfree xinerama */
#define HAVE_XFREE_XINERAMA 1

/* Define if Xft functionality is available */
#define HAVE_XFT2 /**/

/* Define if have xinerama */
#define HAVE_XINERAMA 1

/* Define if have xinput */
#define HAVE_XINPUT 1

/* Define to 1 if you have the `_NSGetEnviron' function. */
/* #undef HAVE__NSGETENVIRON */

/* Define to 256 if neither have HOST_NAME_MAX nor _POSIX_HOST_NAME_MAX */
#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX

/* ISO codes prefix */
#define ISO_CODES_PREFIX "/usr"

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* MateConf Default Path */
#define MATECONF_DEFAULTPATH "/etc/mateconf/2/path"

/* Group to use */
#define MDM_GROUPNAME "mdm"

/* pid file */
#define MDM_PID_FILE "/var/run/mdm.pid"

/* User to use */
#define MDM_USERNAME "mdm"

/* Define to 1 if your C compiler doesn't accept -c and -o together. */
/* #undef NO_MINUS_C_MINUS_O */

/* Name of package */
#define PACKAGE "mdm"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "http://bugzilla.mate.org/enter_bug.cgi?product=mdm"

/* Define to the full name of this package. */
#define PACKAGE_NAME "mdm"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "mdm 1.0.8"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "mdm"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.0.8"

/* Define if your PAM support takes non-const arguments (Solaris) */
/* #undef PAM_MESSAGE_NONCONST */

/* Set if we build with RBAC support */
/* #undef RBAC_SHUTDOWN_KEY */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Define to the name of a structure which holds utmp data. */
#define UTMP struct utmpx

/* Version number of package */
#define VERSION "1.0.8"

/* Define to enable ConsoleKit support */
#define WITH_CONSOLE_KIT 1

/* Show incomplete locales in lang list */
/* #undef WITH_INCOMPLETE_LOCALES */

/* Define xevie option */
#define XEVIE_OPTION ""

/* xsession shell */
#define XSESSION_SHELL "/bin/sh"

/* Options used when launching xserver */
#define X_CONFIG_OPTIONS "-audit 0"

/* Define to 1 if the X Window System is missing or not being used. */
/* #undef X_DISPLAY_MISSING */

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */

/* Compatibility type */
/* #undef socklen_t */
