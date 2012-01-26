/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif /* HAVE_PATHS_H */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "mdm-user-manager.h"
#include "mdm-user-manager-glue.h"
#include "mdm-user-private.h"
#include "mdm-settings-keys.h"

#define MDM_USER_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MDM_TYPE_USER_MANAGER, MdmUserManagerPrivate))

#define MDM_DBUS_PATH         "/org/mate/DisplayManager"
#define MDM_USER_MANAGER_DBUS_PATH MDM_DBUS_PATH "/UserManager"
#define MDM_USER_MANAGER_DBUS_NAME "org.mate.DisplayManager.UserManager"

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

/* Prefs Defaults */
#define DEFAULT_ALLOW_ROOT      TRUE
#define DEFAULT_MAX_ICON_SIZE   128
#define DEFAULT_USER_MAX_FILE   65536

#ifdef __sun
#define DEFAULT_MINIMAL_UID     100
#else
#define DEFAULT_MINIMAL_UID     1000
#endif

#ifndef _PATH_SHELLS
#define _PATH_SHELLS    "/etc/shells"
#endif
#define PATH_PASSWD     "/etc/passwd"

#define DEFAULT_GLOBAL_FACE_DIR DATADIR "/faces"
#define DEFAULT_USER_ICON       "stock_person"

#define LOGIN_CACHE_FILE CACHEDIR "/login_frequency.cache"

struct MdmUserManagerPrivate
{
        GHashTable            *users;
        GHashTable            *sessions;
        GHashTable            *shells;
        DBusGConnection       *connection;
        DBusGProxy            *seat_proxy;
        char                  *seat_id;

        GFileMonitor          *passwd_monitor;
        GFileMonitor          *shells_monitor;

        GSList                *exclude;
        GSList                *include;
        gboolean               include_all;

        guint                  reload_id;
        guint                  ck_history_id;
        guint                  minimal_uid;

        guint8                 loaded_passwd : 1;
        guint8                 users_dirty : 1;
        guint8                 loaded_cache : 1;
        guint8                 loading_users : 1;    
};

enum {
        LOADING_USERS,
        USERS_LOADED,
        USER_ADDED,
        USER_REMOVED,
        USER_UPDATED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     mdm_user_manager_class_init (MdmUserManagerClass *klass);
static void     mdm_user_manager_init       (MdmUserManager      *user_manager);
static void     mdm_user_manager_finalize   (GObject             *object);

static gpointer user_manager_object = NULL;

G_DEFINE_TYPE (MdmUserManager, mdm_user_manager, G_TYPE_OBJECT)

GQuark
mdm_user_manager_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("mdm_user_manager_error");
        }

        return ret;
}

static void
on_user_sessions_changed (MdmUser        *user,
                          MdmUserManager *manager)
{
        guint nsessions;

        nsessions = mdm_user_get_num_sessions (user);

        g_debug ("MdmUserManager: sessions changed user=%s num=%d",
                 mdm_user_get_user_name (user),
                 nsessions);

        /* only signal on zero and one */
        if (nsessions > 1) {
                return;
        }

        g_signal_emit (manager, signals [USER_UPDATED], 0, mdm_user_get_uid (user));
}

static void
on_user_icon_changed (MdmUser        *user,
                      MdmUserManager *manager)
{
        g_debug ("MdmUserManager: user icon changed");
}

static char *
get_seat_id_for_session (DBusGConnection *connection,
                         const char      *session_id)
{
        DBusGProxy      *proxy;
        GError          *error;
        char            *seat_id;
        gboolean         res;

        proxy = NULL;
        seat_id = NULL;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           session_id,
                                           CK_SESSION_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit session object");
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetSeatId",
                                 &error,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH, &seat_id,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_debug ("Failed to identify the current seat: %s", error->message);
                        g_error_free (error);
                } else {
                        g_debug ("Failed to identify the current seat");
                }
        }
 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }

        return seat_id;
}

static char *
get_x11_display_for_session (DBusGConnection *connection,
                             const char      *session_id)
{
        DBusGProxy      *proxy;
        GError          *error;
        char            *x11_display;
        gboolean         res;

        proxy = NULL;
        x11_display = NULL;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           session_id,
                                           CK_SESSION_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit session object");
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetX11Display",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &x11_display,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_debug ("Failed to identify the x11 display: %s", error->message);
                        g_error_free (error);
                } else {
                        g_debug ("Failed to identify the x11 display");
                }
        }
 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }

        return x11_display;
}

static gint
match_name_cmpfunc (gconstpointer a,
                    gconstpointer b)
{
        if (a == NULL || b == NULL) {
                return -1;
        }

        return g_strcmp0 ((char *) a,
                          (char *) b);
}

static gboolean
user_in_exclude_list (MdmUserManager *manager,
                      const char *user)
{
        GSList   *found;
        gboolean  ret = FALSE;

        g_debug ("checking exclude list");

        /* always exclude the "mdm" user. */
        if (user == NULL || (strcmp (user, MDM_USERNAME) == 0)) {
                return TRUE;
        }

        if (manager->priv->exclude != NULL) {
                found = g_slist_find_custom (manager->priv->exclude,
                                             user,
                                             match_name_cmpfunc);
                if (found != NULL) {
                        ret = TRUE;
                }
        }
        return ret;
}

static gboolean
maybe_add_session_for_user (MdmUserManager *manager,
                            MdmUser        *user,
                            const char     *ssid)
{
        char    *sid;
        char    *x11_display;
        gboolean ret;

        ret = FALSE;
        sid = NULL;
        x11_display = NULL;

        /* skip if on another seat */
        sid = get_seat_id_for_session (manager->priv->connection, ssid);
        if (sid == NULL
            || manager->priv->seat_id == NULL
            || strcmp (sid, manager->priv->seat_id) != 0) {
                g_debug ("MdmUserManager: not adding session on other seat: %s", ssid);
                goto out;
        }

        /* skip if doesn't have an x11 display */
        x11_display = get_x11_display_for_session (manager->priv->connection, ssid);
        if (x11_display == NULL || x11_display[0] == '\0') {
                g_debug ("MdmUserManager: not adding session without a x11 display: %s", ssid);
                goto out;
        }

        if (user_in_exclude_list (manager, mdm_user_get_user_name (user))) {
                g_debug ("MdmUserManager: excluding user '%s'", mdm_user_get_user_name (user));
                goto out;
        }

        g_hash_table_insert (manager->priv->sessions,
                             g_strdup (ssid),
                             g_strdup (mdm_user_get_user_name (user)));

        _mdm_user_add_session (user, ssid);
        g_debug ("MdmUserManager: added session for user: %s", mdm_user_get_user_name (user));

        ret = TRUE;

 out:
        g_free (sid);
        g_free (x11_display);

        return ret;
}

static void
add_sessions_for_user (MdmUserManager *manager,
                       MdmUser        *user)
{
        DBusGProxy      *proxy;
        GError          *error;
        gboolean         res;
        guint32          uid;
        GPtrArray       *sessions;
        int              i;

        proxy = dbus_g_proxy_new_for_name (manager->priv->connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit manager object");
                goto out;
        }

        uid = mdm_user_get_uid (user);

        g_debug ("Getting list of sessions for user %u", uid);

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetSessionsForUnixUser",
                                 &error,
                                 G_TYPE_UINT, uid,
                                 G_TYPE_INVALID,
                                 dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
                                 &sessions,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_debug ("Failed to find sessions for user: %s", error->message);
                        g_error_free (error);
                } else {
                        g_debug ("Failed to find sessions for user");
                }
                goto out;
        }

        g_debug ("Found %d sessions for user %s", sessions->len, mdm_user_get_user_name (user));

        for (i = 0; i < sessions->len; i++) {
                char *ssid;

                ssid = g_ptr_array_index (sessions, i);
                maybe_add_session_for_user (manager, user, ssid);
        }

        g_ptr_array_foreach (sessions, (GFunc)g_free, NULL);
        g_ptr_array_free (sessions, TRUE);

 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }
}

static MdmUser *
create_user (MdmUserManager *manager)
{
        MdmUser *user;

        user = g_object_new (MDM_TYPE_USER, "manager", manager, NULL);
        g_signal_connect (user,
                          "sessions-changed",
                          G_CALLBACK (on_user_sessions_changed),
                          manager);
        g_signal_connect (user,
                          "icon-changed",
                          G_CALLBACK (on_user_icon_changed),
                          manager);
        return user;
}

static gint
match_real_name_cmpfunc (gconstpointer a,
                         gconstpointer b)
{
        if (a == b)
                return -1;

        return g_strcmp0 (mdm_user_get_real_name ((MdmUser *) a),
                          mdm_user_get_real_name ((MdmUser *) b));
}

static gboolean
match_real_name_hrfunc (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
        return (g_strcmp0 (user_data, mdm_user_get_real_name (value)) == 0);
}

static void
add_user (MdmUserManager *manager,
          MdmUser        *user)
{
        MdmUser *dup;

        add_sessions_for_user (manager, user);
        dup = g_hash_table_find (manager->priv->users,
                                 match_real_name_hrfunc,
                                 (char *) mdm_user_get_real_name (user));
        if (dup != NULL) {
                //_mdm_user_show_full_display_name (user);
                //_mdm_user_show_full_display_name (dup);
        }
        g_hash_table_insert (manager->priv->users,
                             g_strdup (mdm_user_get_user_name (user)),
                             g_object_ref (user));

        g_signal_emit (manager, signals[USER_ADDED], 0, mdm_user_get_uid (user));
}

static MdmUser *
add_new_user_for_pwent (MdmUserManager *manager,
                        struct passwd  *pwent)
{
        MdmUser *user;

        g_debug ("Creating new user");

        user = create_user (manager);
        _mdm_user_update (user, pwent);

        add_user (manager, user);

        return user;
}

static char *
get_current_seat_id (DBusGConnection *connection)
{
        DBusGProxy      *proxy;
        GError          *error;
        char            *session_id;
        char            *seat_id;
        gboolean         res;

        proxy = NULL;
        session_id = NULL;
        seat_id = NULL;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit manager object");
                goto out;
        }

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetCurrentSession",
                                 &error,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 &session_id,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_debug ("Failed to identify the current session: %s", error->message);
                        g_error_free (error);
                } else {
                        g_debug ("Failed to identify the current session");
                }
                goto out;
        }

        seat_id = get_seat_id_for_session (connection, session_id);

 out:
        if (proxy != NULL) {
                g_object_unref (proxy);
        }
        g_free (session_id);

        return seat_id;
}

static gboolean
get_uid_from_session_id (MdmUserManager *manager,
                         const char     *session_id,
                         uid_t          *uidp)
{
        DBusGProxy      *proxy;
        GError          *error;
        guint            uid;
        gboolean         res;

        proxy = dbus_g_proxy_new_for_name (manager->priv->connection,
                                           CK_NAME,
                                           session_id,
                                           CK_SESSION_INTERFACE);
        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit session object");
                return FALSE;
        }

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetUnixUser",
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, &uid,
                                 G_TYPE_INVALID);
        g_object_unref (proxy);

        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to query the session: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to query the session");
                }
                return FALSE;
        }

        if (uidp != NULL) {
                *uidp = (uid_t) uid;
        }

        return TRUE;
}

static void
seat_session_added (DBusGProxy     *seat_proxy,
                    const char     *session_id,
                    MdmUserManager *manager)
{
        uid_t          uid;
        gboolean       res;
        struct passwd *pwent;
        MdmUser       *user;
        gboolean       is_new;

        g_debug ("Session added: %s", session_id);

        res = get_uid_from_session_id (manager, session_id, &uid);
        if (! res) {
                g_warning ("Unable to lookup user for session");
                return;
        }

        errno = 0;
        pwent = getpwuid (uid);
        if (pwent == NULL) {
                g_warning ("Unable to lookup user ID %d: %s", (int)uid, g_strerror (errno));
                return;
        }

        if (pwent->pw_uid < manager->priv->minimal_uid) {
                return;
        }

        /* check exclusions up front */
        if (user_in_exclude_list (manager, pwent->pw_name)) {
                g_debug ("MdmUserManager: excluding user '%s'", pwent->pw_name);
                return;
        }

        user = g_hash_table_lookup (manager->priv->users, pwent->pw_name);
        if (user == NULL) {
                g_debug ("Creating new user");

                user = create_user (manager);
                _mdm_user_update (user, pwent);
                is_new = TRUE;
        } else {
                is_new = FALSE;
        }

        res = maybe_add_session_for_user (manager, user, session_id);

        /* only add the user if we added a session */
        if (is_new) {
                if (res) {
                        add_user (manager, user);
                } else {
                        g_object_unref (user);
                }
        }
}

static void
seat_session_removed (DBusGProxy     *seat_proxy,
                      const char     *session_id,
                      MdmUserManager *manager)
{
        MdmUser *user;
        char    *username;

        g_debug ("Session removed: %s", session_id);

        /* since the session object may already be gone
         * we can't query CK directly */

        username = g_hash_table_lookup (manager->priv->sessions, session_id);
        if (username == NULL) {
                return;
        }

        user = g_hash_table_lookup (manager->priv->users, username);
        if (user == NULL) {
                /* nothing to do */
                return;
        }

        g_debug ("MdmUserManager: Session removed for %s", username);
        _mdm_user_remove_session (user, session_id);
}

static void
on_proxy_destroy (DBusGProxy     *proxy,
                  MdmUserManager *manager)
{
        g_debug ("MdmUserManager: seat proxy destroyed");

        manager->priv->seat_proxy = NULL;
}

static void
get_seat_proxy (MdmUserManager *manager)
{
        DBusGProxy      *proxy;
        GError          *error;

        g_assert (manager->priv->seat_proxy == NULL);

        error = NULL;
        manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_warning ("Failed to connect to the D-Bus daemon: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to connect to the D-Bus daemon");
                }
                return;
        }

        manager->priv->seat_id = get_current_seat_id (manager->priv->connection);
        if (manager->priv->seat_id == NULL) {
                return;
        }

        g_debug ("MdmUserManager: Found current seat: %s", manager->priv->seat_id);

        error = NULL;
        proxy = dbus_g_proxy_new_for_name_owner (manager->priv->connection,
                                                 CK_NAME,
                                                 manager->priv->seat_id,
                                                 CK_SEAT_INTERFACE,
                                                 &error);

        if (proxy == NULL) {
                if (error != NULL) {
                        g_warning ("Failed to connect to the ConsoleKit seat object: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to connect to the ConsoleKit seat object");
                }
                return;
        }

        g_signal_connect (proxy, "destroy", G_CALLBACK (on_proxy_destroy), manager);

        dbus_g_proxy_add_signal (proxy,
                                 "SessionAdded",
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (proxy,
                                 "SessionRemoved",
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (proxy,
                                     "SessionAdded",
                                     G_CALLBACK (seat_session_added),
                                     manager,
                                     NULL);
        dbus_g_proxy_connect_signal (proxy,
                                     "SessionRemoved",
                                     G_CALLBACK (seat_session_removed),
                                     manager,
                                     NULL);
        manager->priv->seat_proxy = proxy;

}

/**
 * mdm_manager_get_user:
 * @manager: the manager to query.
 * @username: the login name of the user to get.
 *
 * Retrieves a pointer to the #MdmUser object for the login named @username
 * from @manager. This pointer is not a reference, and should not be released.
 *
 * Returns: a pointer to a #MdmUser object.
 **/
MdmUser *
mdm_user_manager_get_user (MdmUserManager *manager,
                           const char     *username)
{
        MdmUser *user;

        g_return_val_if_fail (MDM_IS_USER_MANAGER (manager), NULL);
        g_return_val_if_fail (username != NULL && username[0] != '\0', NULL);

        user = g_hash_table_lookup (manager->priv->users, username);

        if (user == NULL) {
                struct passwd *pwent;

                pwent = getpwnam (username);

                if (pwent != NULL) {
                        user = add_new_user_for_pwent (manager, pwent);
                }
        }

        return user;
}

MdmUser *
mdm_user_manager_get_user_by_uid (MdmUserManager *manager,
                                  uid_t           uid)
{
        MdmUser       *user;
        struct passwd *pwent;

        g_return_val_if_fail (MDM_IS_USER_MANAGER (manager), NULL);

        pwent = getpwuid (uid);
        if (pwent == NULL) {
                g_warning ("MdmUserManager: unable to lookup uid %d", (int)uid);
                return NULL;
        }

        user = g_hash_table_lookup (manager->priv->users, pwent->pw_name);

        if (user == NULL) {
                user = add_new_user_for_pwent (manager, pwent);
        }

        return user;
}

static void
listify_hash_values_hfunc (gpointer key,
                           gpointer value,
                           gpointer user_data)
{
        GSList **list = user_data;

        *list = g_slist_prepend (*list, value);
}

GSList *
mdm_user_manager_list_users (MdmUserManager *manager)
{
        GSList *retval;

        g_return_val_if_fail (MDM_IS_USER_MANAGER (manager), NULL);

        retval = NULL;
        g_hash_table_foreach (manager->priv->users, listify_hash_values_hfunc, &retval);

        return g_slist_sort (retval, (GCompareFunc) mdm_user_collate);
}

static gboolean
parse_value_as_ulong (const char *value,
                      gulong     *ulongval)
{
        char  *end_of_valid_long;
        glong  long_value;
        gulong ulong_value;

        errno = 0;
        long_value = strtol (value, &end_of_valid_long, 10);

        if (*value == '\0' || *end_of_valid_long != '\0') {
                return FALSE;
        }

        ulong_value = long_value;
        if (ulong_value != long_value || errno == ERANGE) {
                return FALSE;
        }

        *ulongval = ulong_value;

        return TRUE;
}

static gboolean
parse_ck_history_line (const char *line,
                       char      **user_namep,
                       gulong     *frequencyp)
{
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        gboolean    ret;
        GError     *error;

        ret = FALSE;
        re = NULL;
        match_info = NULL;

        error = NULL;
        re = g_regex_new ("(?P<username>[0-9a-zA-Z]+)[ ]+(?P<frequency>[0-9]+)", 0, 0, &error);
        if (re == NULL) {
                if (error != NULL) {
                        g_critical ("%s", error->message);
                } else {
                        g_critical ("Error in regex call");
                }
                goto out;
        }

        g_regex_match (re, line, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse history: %s", line);
                goto out;
        }

        if (user_namep != NULL) {
                *user_namep = g_match_info_fetch_named (match_info, "username");
        }

        if (frequencyp != NULL) {
                char *freq;
                freq = g_match_info_fetch_named (match_info, "frequency");
                res = parse_value_as_ulong (freq, frequencyp);
                g_free (freq);
                if (! res) {
                        goto out;
                }
        }

        ret = TRUE;

 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }
        return ret;
}

static void
process_ck_history_line (MdmUserManager *manager,
                         const char     *line)
{
        gboolean res;
        char    *username;
        gulong   frequency;
        struct passwd *pwent;
        MdmUser *user;

        frequency = 0;
        username = NULL;
        res = parse_ck_history_line (line, &username, &frequency);
        if (! res) {
                return;
        }

        if (user_in_exclude_list (manager, username)) {
                g_debug ("MdmUserManager: excluding user '%s'", username);
                g_free (username);
                return;
        }

        /* do not show system users; we cannot use mdm_user_manager_get_user()
         * here since this creates/signals users as a side effect */
        pwent = getpwnam (username);
        if (pwent == NULL) {
                g_warning ("Unable to lookup user name %s: %s", username, g_strerror (errno));
                return;
        }
        if (pwent->pw_uid < manager->priv->minimal_uid) {
                g_debug ("MdmUserManager: excluding user '%s'", username);
                return;
        }

        user = mdm_user_manager_get_user (manager, username);
        if (user == NULL) {
                g_debug ("MdmUserManager: unable to lookup user '%s'", username);
                g_free (username);
                return;
        }

        g_object_set (user, "login-frequency", frequency, NULL);
        g_signal_emit (manager, signals [USER_UPDATED], 0, mdm_user_get_uid (user));
        g_free (username);
}

static gboolean
ck_history_watch (GIOChannel     *source,
                  GIOCondition    condition,
                  MdmUserManager *manager)
{
        GIOStatus status;
        gboolean  done  = FALSE;

        g_return_val_if_fail (manager != NULL, FALSE);

        if (condition & G_IO_IN) {
                char   *str;
                GError *error;

                error = NULL;
                status = g_io_channel_read_line (source, &str, NULL, NULL, &error);
                if (error != NULL) {
                        g_warning ("MdmUserManager: unable to read line: %s", error->message);
                        g_error_free (error);
                }

                if (status == G_IO_STATUS_NORMAL) {
                        g_debug ("MdmUserManager: history output: %s", str);
                        process_ck_history_line (manager, str);
                } else if (status == G_IO_STATUS_EOF) {
                        done = TRUE;
                }

                g_free (str);
        } else if (condition & G_IO_HUP) {
                done = TRUE;
        }

        if (done) {
                FILE *fp;

                /* Cache login counts */
                fp = fopen (LOGIN_CACHE_FILE, "w");
                if (fp != NULL) {
                        GHashTableIter iter;
                        gpointer value;

                        g_hash_table_iter_init (&iter, manager->priv->users);
                        while (g_hash_table_iter_next (&iter, NULL, &value)) {
                                MdmUser *user = (MdmUser *) value;
                                fprintf (fp, "%s %lu\n",
                                         mdm_user_get_user_name (user),
                                         mdm_user_get_login_frequency (user));
                        }
                        fclose (fp);
                }
                else
                        g_warning ("Unable to write to login cache file: %s", LOGIN_CACHE_FILE);

                if (manager->priv->loading_users) {
                        g_signal_emit (G_OBJECT (manager), signals[USERS_LOADED], 0);
                        manager->priv->loading_users = FALSE;
                }

                manager->priv->ck_history_id = 0;
                return FALSE;
        }

        return TRUE;
}

static void
reload_ck_history (MdmUserManager *manager)
{
        char       *command;
        const char *seat_id;
        GError     *error;
        gboolean    res;
        char      **argv;
        int         standard_out;
        GIOChannel *channel;

        seat_id = NULL;
        if (manager->priv->seat_id != NULL
            && g_str_has_prefix (manager->priv->seat_id, "/org/freedesktop/ConsoleKit/")) {

                seat_id = manager->priv->seat_id + strlen ("/org/freedesktop/ConsoleKit/");
        }

        if (seat_id == NULL) {
                g_warning ("Unable to find users: no seat-id found");
                return;
        }

        command = g_strdup_printf ("ck-history --frequent --seat='%s' --session-type=''",
                                   seat_id);
        g_debug ("MdmUserManager: running '%s'", command);
        error = NULL;
        if (! g_shell_parse_argv (command, NULL, &argv, &error)) {
                if (error != NULL) {
                        g_warning ("Could not parse command: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Could not parse command");
                }
                goto out;
        }

        error = NULL;
        res = g_spawn_async_with_pipes (NULL,
                                        argv,
                                        NULL,
                                        G_SPAWN_SEARCH_PATH,
                                        NULL,
                                        NULL,
                                        NULL, /* pid */
                                        NULL,
                                        &standard_out,
                                        NULL,
                                        &error);
        g_strfreev (argv);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Unable to run ck-history: %s", error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Unable to run ck-history");
                }
                goto out;
        }

        channel = g_io_channel_unix_new (standard_out);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        manager->priv->ck_history_id = g_io_add_watch (channel,
                                                       G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                                       (GIOFunc)ck_history_watch,
                                                       manager);
        g_io_channel_unref (channel);

 out:
        g_free (command);
}

static void
add_included_user (char *username, MdmUserManager *manager)
{
        MdmUser     *user;

        g_debug ("Adding included user %s", username);
        /*
         * The call to mdm_user_manager_get_user will add the user if it is
         * valid and not already in the hash.
         */
        user = mdm_user_manager_get_user (manager, username);
        if (user == NULL) {
                g_debug ("MdmUserManager: unable to lookup user '%s'", username);
                g_free (username);
                return;
        }
}

static void
add_included_users (MdmUserManager *manager)
{
        /* Add users who are specifically included */
        if (manager->priv->include != NULL) {
                g_slist_foreach (manager->priv->include,
                                 (GFunc)add_included_user,
                                 (gpointer)manager);
        }
}


static void
reload_passwd (MdmUserManager *manager)
{
        struct passwd *pwent;
        GSList        *old_users;
        GSList        *new_users;
        GSList        *list;
        GSList        *dup;
        FILE          *fp;

        old_users = NULL;
        new_users = NULL;

        errno = 0;
        fp = fopen (PATH_PASSWD, "r");
        if (fp == NULL) {
                g_warning ("Unable to open %s: %s", PATH_PASSWD, g_strerror (errno));
                goto out;
        }

        g_hash_table_foreach (manager->priv->users, listify_hash_values_hfunc, &old_users);
        g_slist_foreach (old_users, (GFunc) g_object_ref, NULL);

        /* Make sure we keep users who are logged in no matter what. */
        for (list = old_users; list; list = list->next) {
                if (mdm_user_get_num_sessions (list->data) > 0) {
                        g_object_freeze_notify (G_OBJECT (list->data));
                        //_mdm_user_show_short_display_name (list->data);
                        new_users = g_slist_prepend (new_users, g_object_ref (list->data));
                }
        }

        if (manager->priv->include_all != TRUE) {
                g_debug ("MdmUserManager: include_all is FALSE");
        } else {
                g_debug ("MdmUserManager: include_all is TRUE");

                for (pwent = fgetpwent (fp);
                     pwent != NULL;
                     pwent = fgetpwent (fp)) {
                        MdmUser *user;

                        user = NULL;

                        /* Skip users below MinimalUID... */
                        if (pwent->pw_uid < manager->priv->minimal_uid) {
                                continue;
                        }

                        /* ...And users w/ invalid shells... */
                        if (pwent->pw_shell == NULL ||
                            !g_hash_table_lookup (manager->priv->shells,
                                                  pwent->pw_shell)) {
                                g_debug ("MdmUserManager: skipping user with bad shell: %s", pwent->pw_name);
                                continue;
                        }

                        /* ...And explicitly excluded users */
                        if (user_in_exclude_list (manager, pwent->pw_name)) {
                                g_debug ("MdmUserManager: explicitly skipping user: %s", pwent->pw_name);
                                continue;
                        }

                        user = g_hash_table_lookup (manager->priv->users,
                                                    pwent->pw_name);

                        /* Update users already in the *new* list */
                        if (g_slist_find (new_users, user)) {
                                _mdm_user_update (user, pwent);
                                continue;
                        }

                        if (user == NULL) {
                                user = create_user (manager);
                        } else {
                                g_object_ref (user);
                        }

                        /* Freeze & update users not already in the new list */
                        g_object_freeze_notify (G_OBJECT (user));
                        _mdm_user_update (user, pwent);

                        new_users = g_slist_prepend (new_users, user);
                }
        }

        /* Go through and handle removed users */
        for (list = old_users; list; list = list->next) {
                if (! g_slist_find (new_users, list->data)) {
                        g_signal_emit (manager, signals[USER_REMOVED], 0, mdm_user_get_uid (MDM_USER (list->data)));
                        g_hash_table_remove (manager->priv->users,
                                             mdm_user_get_user_name (list->data));
                }
        }

        /* Go through and handle added users or update display names */
        for (list = new_users; list; list = list->next) {
                if (g_slist_find (old_users, list->data)) {
                        dup = g_slist_find_custom (new_users,
                                                   list->data,
                                                   match_real_name_cmpfunc);
                        if (dup != NULL) {
                                //_mdm_user_show_full_display_name (list->data);
                                //_mdm_user_show_full_display_name (dup->data);
                        }
                } else {
                        add_user (manager, list->data);
                }
        }

        add_included_users (manager);

        if (!manager->priv->loaded_passwd) {
                g_signal_emit (manager, signals[USERS_LOADED], 0);
                manager->priv->loaded_passwd = TRUE;
        }

 out:
        /* Cleanup */

        fclose (fp);

        g_slist_foreach (new_users, (GFunc) g_object_thaw_notify, NULL);
        g_slist_foreach (new_users, (GFunc) g_object_unref, NULL);
        g_slist_free (new_users);

        g_slist_foreach (old_users, (GFunc) g_object_unref, NULL);
        g_slist_free (old_users);
}

static void
load_login_frequency_cache (MdmUserManager *manager)
{
        GIOChannel *channel;
        gchar *line;

        channel = g_io_channel_new_file (LOGIN_CACHE_FILE, "r", NULL);
        if (channel == NULL)
                return;

        while (g_io_channel_read_line (channel, &line, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
                process_ck_history_line (manager, line);
                g_free (line);
        }
    
        g_io_channel_close (channel);
    
        if (manager->priv->loading_users) {
                g_signal_emit (G_OBJECT (manager), signals[USERS_LOADED], 0);
                manager->priv->loading_users = FALSE;
        }    
}

static void
reload_users (MdmUserManager *manager)
{
        if (!manager->priv->loaded_cache) {
                load_login_frequency_cache (manager);
                manager->priv->loaded_cache = TRUE;
        }
        reload_ck_history (manager);
        reload_passwd (manager);
}

static gboolean
reload_users_timeout (MdmUserManager *manager)
{
        reload_users (manager);
        manager->priv->reload_id = 0;

        return FALSE;
}

static void
queue_reload_users (MdmUserManager *manager)
{
        if (manager->priv->reload_id > 0) {
                return;
        }

        g_signal_emit (G_OBJECT (manager), signals[LOADING_USERS], 0);
        manager->priv->loading_users = TRUE;
        manager->priv->reload_id = g_idle_add ((GSourceFunc)reload_users_timeout, manager);
}

static void
reload_shells (MdmUserManager *manager)
{
        char *shell;

        setusershell ();

        g_hash_table_remove_all (manager->priv->shells);
        for (shell = getusershell (); shell != NULL; shell = getusershell ()) {
                /* skip well known not-real shells */
                if (shell == NULL
                    || strcmp (shell, "/sbin/nologin") == 0
                    || strcmp (shell, "/bin/false") == 0) {
                        g_debug ("MdmUserManager: skipping shell %s", shell);
                        continue;
                }
                g_hash_table_insert (manager->priv->shells,
                                     g_strdup (shell),
                                     GUINT_TO_POINTER (TRUE));
        }

        endusershell ();
}

static void
on_shells_monitor_changed (GFileMonitor     *monitor,
                           GFile            *file,
                           GFile            *other_file,
                           GFileMonitorEvent event_type,
                           MdmUserManager   *manager)
{
        if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
            event_type != G_FILE_MONITOR_EVENT_CREATED) {
                return;
        }

        reload_shells (manager);
        reload_passwd (manager);
}

static void
on_passwd_monitor_changed (GFileMonitor     *monitor,
                           GFile            *file,
                           GFile            *other_file,
                           GFileMonitorEvent event_type,
                           MdmUserManager   *manager)
{
        if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
            event_type != G_FILE_MONITOR_EVENT_CREATED) {
                return;
        }

        reload_passwd (manager);
}

static void
mdm_user_manager_class_init (MdmUserManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = mdm_user_manager_finalize;

        signals [LOADING_USERS] =
                g_signal_new ("loading-users",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MdmUserManagerClass, loading_users),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals [USERS_LOADED] =
                g_signal_new ("users-loaded",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MdmUserManagerClass, users_loaded),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals [USER_ADDED] =
                g_signal_new ("user-added",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MdmUserManagerClass, user_added),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, G_TYPE_INT64);
        signals [USER_REMOVED] =
                g_signal_new ("user-removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MdmUserManagerClass, user_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, G_TYPE_INT64);
        signals [USER_UPDATED] =
                g_signal_new ("user-updated",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MdmUserManagerClass, user_updated),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1, G_TYPE_INT64);

        g_type_class_add_private (klass, sizeof (MdmUserManagerPrivate));

        dbus_g_object_type_install_info (MDM_TYPE_USER_MANAGER, &dbus_glib_mdm_user_manager_object_info);
}

static void
mdm_set_string_list (char *value, GSList **retval)
{
        char **temp_array;
        int    i;

        *retval = NULL;

        if (value == NULL || *value == '\0') {
                g_debug ("Not adding NULL user");
                *retval = NULL;
                return;
        }

        temp_array = g_strsplit (value, ",", 0);
        for (i = 0; temp_array[i] != NULL; i++) {
                g_debug ("Adding value %s", temp_array[i]);
                g_strstrip (temp_array[i]);
                *retval = g_slist_prepend (*retval, g_strdup (temp_array[i]));
        }

        g_strfreev (temp_array);
}


static guint
system_minimal_uid (void)
{
        guint  uid = DEFAULT_MINIMAL_UID;
#ifndef __sun
        char *defspath = "/etc/login.defs";
        FILE *fp;
        char line[128];

        errno = 0;
        fp = fopen (defspath, "r");
        if (fp == NULL) {
                g_warning ("Unable to open %s: %s", defspath, g_strerror (errno));
                goto out;
        }
        while (fgets (line, sizeof(line), fp)) {
            if (strncmp (line, "UID_MIN", 7) == 0) {
                char *ptr = line + 7;
                int  value;
                while (*ptr && isblank (*ptr)) { ptr++; }
                value = atoi (ptr);
                if (value) uid = value;
                break;
            }
        }
        fclose (fp);
#endif
out:
        return uid;
}


static void
mdm_user_manager_init (MdmUserManager *manager)
{
        int            i;
        GFile         *file;
        GError        *error;
        char          *temp;
        gboolean       res;

        manager->priv = MDM_USER_MANAGER_GET_PRIVATE (manager);

        /* exclude/include */
        g_debug ("Setting users to include:");
        res = mdm_settings_direct_get_string  (MDM_KEY_INCLUDE,
                                               &temp);
        mdm_set_string_list (temp, &manager->priv->include);

        g_debug ("Setting users to exclude:");
        res = mdm_settings_direct_get_string  (MDM_KEY_EXCLUDE,
                                               &temp);
        mdm_set_string_list (temp, &manager->priv->exclude);

        res = mdm_settings_direct_get_boolean (MDM_KEY_INCLUDE_ALL,
                                               &manager->priv->include_all);

        manager->priv->minimal_uid = system_minimal_uid ();

        /* sessions */
        manager->priv->sessions = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         g_free,
                                                         g_free);

        /* users */
        manager->priv->users = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      (GDestroyNotify) g_object_run_dispose);

        if (manager->priv->include_all == TRUE) {
                /* /etc/shells */
                manager->priv->shells = g_hash_table_new_full (g_str_hash,
                                                               g_str_equal,
                                                               g_free,
                                                               NULL);
                reload_shells (manager);
                file = g_file_new_for_path (_PATH_SHELLS);
                error = NULL;
                manager->priv->shells_monitor = g_file_monitor_file (file,
                                                                     G_FILE_MONITOR_NONE,
                                                                     NULL,
                                                                     &error);
                if (manager->priv->shells_monitor != NULL) {
                        g_signal_connect (manager->priv->shells_monitor,
                                          "changed",
                                          G_CALLBACK (on_shells_monitor_changed),
                                          manager);
                } else {
                        g_warning ("Unable to monitor %s: %s", _PATH_SHELLS, error->message);
                        g_error_free (error);
                }
                g_object_unref (file);

                /* /etc/passwd */
                file = g_file_new_for_path (PATH_PASSWD);
                manager->priv->passwd_monitor = g_file_monitor_file (file,
                                                                     G_FILE_MONITOR_NONE,
                                                                     NULL,
                                                                     &error);
                if (manager->priv->passwd_monitor != NULL) {
                        g_signal_connect (manager->priv->passwd_monitor,
                                          "changed",
                                          G_CALLBACK (on_passwd_monitor_changed),
                                          manager);
                } else {
                        g_warning ("Unable to monitor %s: %s", PATH_PASSWD, error->message);
                        g_error_free (error);
                }
                g_object_unref (file);
        }

        get_seat_proxy (manager);

        queue_reload_users (manager);

        manager->priv->users_dirty = FALSE;

        dbus_g_connection_register_g_object (manager->priv->connection, MDM_USER_MANAGER_DBUS_PATH, G_OBJECT (manager));
}

static void
mdm_user_manager_finalize (GObject *object)
{
        MdmUserManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (MDM_IS_USER_MANAGER (object));

        manager = MDM_USER_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        if (manager->priv->exclude != NULL) {
                g_slist_free (manager->priv->exclude);
        }

        if (manager->priv->include != NULL) {
                g_slist_free (manager->priv->include);
        }

        if (manager->priv->seat_proxy != NULL) {
                g_object_unref (manager->priv->seat_proxy);
        }

        if (manager->priv->ck_history_id != 0) {
                g_source_remove (manager->priv->ck_history_id);
                manager->priv->ck_history_id = 0;
        }

        if (manager->priv->reload_id > 0) {
                g_source_remove (manager->priv->reload_id);
                manager->priv->reload_id = 0;
        }

        g_hash_table_destroy (manager->priv->sessions);

        g_file_monitor_cancel (manager->priv->passwd_monitor);
        g_hash_table_destroy (manager->priv->users);

        g_file_monitor_cancel (manager->priv->shells_monitor);
        g_hash_table_destroy (manager->priv->shells);

        g_free (manager->priv->seat_id);

        G_OBJECT_CLASS (mdm_user_manager_parent_class)->finalize (object);
}

MdmUserManager *
mdm_user_manager_ref_default (void)
{
        if (user_manager_object != NULL) {
                g_object_ref (user_manager_object);
        } else {
                user_manager_object = g_object_new (MDM_TYPE_USER_MANAGER, NULL);
                g_object_add_weak_pointer (user_manager_object,
                                           (gpointer *) &user_manager_object);
        }

        return MDM_USER_MANAGER (user_manager_object);
}

/*
  Example:
  dbus-send --system --print-reply --dest=org.mate.DisplayManager \
  /org/mate/DisplayManager/UserManager org.mate.DisplayManager.UserManager.CountUsers
*/
gboolean
mdm_user_manager_count_users (MdmUserManager *user_manager,
                              gint           *user_count,
                              GError        **error)
{
        *user_count = g_hash_table_size (user_manager->priv->users);
    
        return TRUE;
}

/*
  Example:
  dbus-send --system --print-reply --dest=org.mate.DisplayManager \
  /org/mate/DisplayManager/UserManager org.mate.DisplayManager.UserManager.GetUserList
*/
gboolean
mdm_user_manager_get_user_list (MdmUserManager *user_manager,
                                GArray        **user_list,
                                GError        **error)
{
        GHashTableIter iter;
        MdmUser *user;

        *user_list = g_array_new (FALSE, FALSE, sizeof (gint64));
        g_hash_table_iter_init (&iter, user_manager->priv->users);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&user)) {
                gint64 uid = mdm_user_get_uid (user);
                g_array_append_val (*user_list, uid);
        }

        return TRUE;
}


/*
  Example:
  dbus-send --system --print-reply --dest=org.mate.DisplayManager \
  /org/mate/DisplayManager/UserManager org.mate.DisplayManager.UserManager.GetUserInfo int64:1000
*/
gboolean
mdm_user_manager_get_user_info (MdmUserManager *user_manager,
                                gint64          uid,
                                gchar         **user_name,
                                gchar         **real_name,
                                gchar         **shell,
                                gint           *login_count,
                                gchar         **icon_url,
                                GError        **error)
{
        MdmUser *user;

        user = mdm_user_manager_get_user_by_uid (user_manager, uid);
        if (user == NULL)
                return FALSE;

        *user_name = g_strdup (mdm_user_get_user_name (user));
        *real_name = g_strdup (mdm_user_get_real_name (user));
        *login_count = mdm_user_get_login_frequency (user);
        *shell = g_strdup (mdm_user_get_shell (user));
        *icon_url = g_strdup (mdm_user_get_icon_url (user));

        return TRUE;
}

/*
  Example:
  dbus-send --system --print-reply --dest=org.mate.DisplayManager \
  /org/mate/DisplayManager/UserManager org.mate.DisplayManager.UserManager.GetUsersInfo array:int64:1000,1001
*/
gboolean
mdm_user_manager_get_users_info (MdmUserManager *user_manager,
                                 GArray         *uids,
                                 GPtrArray     **users_info,
                                 GError        **error)
{
        int i;
    
        *users_info = g_ptr_array_new ();

        for (i = 0; i < uids->len; i++) {
                gint64 uid;
                MdmUser *user;
                GValueArray *user_info;
                GValue arg = {0};

                uid = g_array_index (uids, gint64, i);
                user = mdm_user_manager_get_user_by_uid (user_manager, uid);
                if (user == NULL)
                        continue;

                user_info = g_value_array_new (5);

                g_value_init (&arg, G_TYPE_INT64);
                g_value_set_int64 (&arg, uid);
                g_value_array_append (user_info, &arg);
                g_value_unset (&arg);

                g_value_init (&arg, G_TYPE_STRING);
                g_value_set_string (&arg, mdm_user_get_user_name (user));
                g_value_array_append (user_info, &arg);
                g_value_unset (&arg);

                g_value_init (&arg, G_TYPE_STRING);
                g_value_set_string (&arg, mdm_user_get_real_name (user));
                g_value_array_append (user_info, &arg);
                g_value_unset (&arg);

                g_value_init (&arg, G_TYPE_STRING);
                g_value_set_string (&arg, mdm_user_get_shell (user));
                g_value_array_append (user_info, &arg);
                g_value_unset (&arg);

                g_value_init (&arg, G_TYPE_INT);
                g_value_set_int (&arg, mdm_user_get_login_frequency (user));
                g_value_array_append (user_info, &arg);
                g_value_unset (&arg);

                g_value_init (&arg, G_TYPE_STRING);
                g_value_set_string (&arg, mdm_user_get_icon_url (user));
                g_value_array_append (user_info, &arg);
                g_value_unset (&arg);

                g_ptr_array_add (*users_info, user_info);
        }
    
        return TRUE;
}

/*
  Example:
  dbus-send --system --print-reply --dest=org.mate.DisplayManager \
  /org/mate/DisplayManager/UserManager org.mate.DisplayManager.UserManager.GetUsersLoaded
*/
gboolean
mdm_user_manager_get_users_loaded (MdmUserManager *user_manager,
                                   gboolean       *is_loaded,
                                   GError        **error)
{
        *is_loaded = user_manager->priv->loaded_passwd;
        return TRUE;
}
