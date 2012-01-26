/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>

#include <float.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gio/gio.h>

#include "mdm-user-manager.h"
#include "mdm-user-private.h"

#define MDM_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), MDM_TYPE_USER, MdmUserClass))
#define MDM_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MDM_TYPE_USER))
#define MDM_USER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), MDM_TYPE_USER, MdmUserClass))

#define GLOBAL_FACEDIR    DATADIR "/faces"
#define MAX_ICON_SIZE     128
#define MAX_FILE_SIZE     65536
#define MINIMAL_UID       100

enum {
        PROP_0,
        PROP_MANAGER,
        PROP_REAL_NAME,
        PROP_DISPLAY_NAME,
        PROP_USER_NAME,
        PROP_UID,
        PROP_HOME_DIR,
        PROP_SHELL,
        PROP_ICON_URL,        
        PROP_LOGIN_FREQUENCY,
};

enum {
        ICON_CHANGED,
        SESSIONS_CHANGED,
        LAST_SIGNAL
};

struct _MdmUser {
        GObject         parent;

        MdmUserManager *manager;

        uid_t           uid;
        char           *user_name;
        char           *real_name;
        char           *display_name;
        char           *home_dir;
        char           *shell;
        char           *icon_url;
        GList          *sessions;
        gulong          login_frequency;

        GFileMonitor   *icon_monitor;
};

typedef struct _MdmUserClass
{
        GObjectClass parent_class;

        void (* icon_changed)     (MdmUser *user);
        void (* sessions_changed) (MdmUser *user);
} MdmUserClass;

static void mdm_user_finalize     (GObject      *object);

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (MdmUser, mdm_user, G_TYPE_OBJECT)

static int
session_compare (const char *a,
                 const char *b)
{
        if (a == NULL) {
                return 1;
        } else if (b == NULL) {
                return -1;
        }

        return strcmp (a, b);
}

void
_mdm_user_add_session (MdmUser    *user,
                       const char *ssid)
{
        GList *li;

        g_return_if_fail (MDM_IS_USER (user));
        g_return_if_fail (ssid != NULL);

        li = g_list_find_custom (user->sessions, ssid, (GCompareFunc)session_compare);
        if (li == NULL) {
                g_debug ("MdmUser: adding session %s", ssid);
                user->sessions = g_list_prepend (user->sessions, g_strdup (ssid));
                g_signal_emit (user, signals[SESSIONS_CHANGED], 0);
        } else {
                g_debug ("MdmUser: session already present: %s", ssid);
        }
}

void
_mdm_user_remove_session (MdmUser    *user,
                          const char *ssid)
{
        GList *li;

        g_return_if_fail (MDM_IS_USER (user));
        g_return_if_fail (ssid != NULL);

        li = g_list_find_custom (user->sessions, ssid, (GCompareFunc)session_compare);
        if (li != NULL) {
                g_debug ("MdmUser: removing session %s", ssid);
                g_free (li->data);
                user->sessions = g_list_delete_link (user->sessions, li);
                g_signal_emit (user, signals[SESSIONS_CHANGED], 0);
        } else {
                g_debug ("MdmUser: session not found: %s", ssid);
        }
}

guint
mdm_user_get_num_sessions (MdmUser    *user)
{
        return g_list_length (user->sessions);
}

GList *
mdm_user_get_sessions (MdmUser *user)
{
        return user->sessions;
}

static void
_mdm_user_set_login_frequency (MdmUser *user,
                               gulong   login_frequency)
{
        user->login_frequency = login_frequency;
        g_object_notify (G_OBJECT (user), "login-frequency");
}

static void
mdm_user_set_property (GObject      *object,
                       guint         param_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
        MdmUser *user;

        user = MDM_USER (object);

        switch (param_id) {
        case PROP_MANAGER:
                user->manager = g_value_get_object (value);
                g_assert (user->manager);
                break;
        case PROP_LOGIN_FREQUENCY:
                _mdm_user_set_login_frequency (user, g_value_get_ulong (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

static void
mdm_user_get_property (GObject    *object,
                       guint       param_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
        MdmUser *user;

        user = MDM_USER (object);

        switch (param_id) {
        case PROP_MANAGER:
                g_value_set_object (value, user->manager);
                break;
        case PROP_USER_NAME:
                g_value_set_string (value, user->user_name);
                break;
        case PROP_REAL_NAME:
                g_value_set_string (value, user->real_name);
                break;
        case PROP_DISPLAY_NAME:
                g_value_set_string (value, user->display_name);
                break;
        case PROP_HOME_DIR:
                g_value_set_string (value, user->home_dir);
                break;
        case PROP_UID:
                g_value_set_ulong (value, user->uid);
                break;
        case PROP_SHELL:
                g_value_set_string (value, user->shell);
                break;
        case PROP_ICON_URL:
                g_value_set_string (value, user->icon_url);
                break;            
        case PROP_LOGIN_FREQUENCY:
                g_value_set_ulong (value, user->login_frequency);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
                break;
        }
}

static void
mdm_user_class_init (MdmUserClass *class)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (class);

        gobject_class->set_property = mdm_user_set_property;
        gobject_class->get_property = mdm_user_get_property;
        gobject_class->finalize = mdm_user_finalize;

        g_object_class_install_property (gobject_class,
                                         PROP_MANAGER,
                                         g_param_spec_object ("manager",
                                                              _("Manager"),
                                                              _("The user manager object this user is controlled by."),
                                                              MDM_TYPE_USER_MANAGER,
                                                              (G_PARAM_READWRITE |
                                                               G_PARAM_CONSTRUCT_ONLY)));

        g_object_class_install_property (gobject_class,
                                         PROP_REAL_NAME,
                                         g_param_spec_string ("real-name",
                                                              "Real Name",
                                                              "The real name to display for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));

        g_object_class_install_property (gobject_class,
                                         PROP_DISPLAY_NAME,
                                         g_param_spec_string ("display-name",
                                                              "Display Name",
                                                              "The unique name to display for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));

        g_object_class_install_property (gobject_class,
                                         PROP_UID,
                                         g_param_spec_ulong ("uid",
                                                             "User ID",
                                                             "The UID for this user.",
                                                             0, G_MAXULONG, 0,
                                                             G_PARAM_READABLE));
        g_object_class_install_property (gobject_class,
                                         PROP_USER_NAME,
                                         g_param_spec_string ("user-name",
                                                              "User Name",
                                                              "The login name for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (gobject_class,
                                         PROP_HOME_DIR,
                                         g_param_spec_string ("home-directory",
                                                              "Home Directory",
                                                              "The home directory for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (gobject_class,
                                         PROP_SHELL,
                                         g_param_spec_string ("shell",
                                                              "Shell",
                                                              "The shell for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (gobject_class,
                                         PROP_SHELL,
                                         g_param_spec_string ("icon-url",
                                                              "Icon URL",
                                                              "The icon for this user.",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (gobject_class,
                                         PROP_LOGIN_FREQUENCY,
                                         g_param_spec_ulong ("login-frequency",
                                                             "login frequency",
                                                             "login frequency",
                                                             0,
                                                             G_MAXULONG,
                                                             0,
                                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        signals [ICON_CHANGED] =
                g_signal_new ("icon-changed",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MdmUserClass, icon_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals [SESSIONS_CHANGED] =
                g_signal_new ("sessions-changed",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MdmUserClass, sessions_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}


static void
on_icon_monitor_changed (GFileMonitor     *monitor,
                         GFile            *file,
                         GFile            *other_file,
                         GFileMonitorEvent event_type,
                         MdmUser          *user)
{
        g_debug ("Icon changed: %d", event_type);

        if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
            event_type != G_FILE_MONITOR_EVENT_CREATED) {
                return;
        }

        _mdm_user_icon_changed (user);
}

static void
update_icon_monitor (MdmUser *user)
{
        GFile  *file;
        GError *error;
        char   *path;

        if (user->home_dir == NULL) {
                return;
        }

        if (user->icon_monitor != NULL) {
                g_file_monitor_cancel (user->icon_monitor);
                user->icon_monitor = NULL;
        }

        path = g_build_filename (user->home_dir, ".face", NULL);
        g_debug ("adding monitor for '%s'", path);
        file = g_file_new_for_path (path);
        error = NULL;
        user->icon_monitor = g_file_monitor_file (file,
                                                  G_FILE_MONITOR_NONE,
                                                  NULL,
                                                  &error);
        if (user->icon_monitor != NULL) {
                g_signal_connect (user->icon_monitor,
                                  "changed",
                                  G_CALLBACK (on_icon_monitor_changed),
                                  user);
        } else {
                g_warning ("Unable to monitor %s: %s", path, error->message);
                g_error_free (error);
        }
        g_object_unref (file);

        g_free (user->icon_url);
        user->icon_url = g_strjoin(NULL, "file://", path, NULL);
        g_object_notify (G_OBJECT (user), "icon-url");

        g_free (path);
}

static void
mdm_user_init (MdmUser *user)
{
        user->manager = NULL;
        user->user_name = NULL;
        user->real_name = NULL;
        user->display_name = NULL;
        user->sessions = NULL;
        user->icon_url = NULL;
}

static void
mdm_user_finalize (GObject *object)
{
        MdmUser *user;

        user = MDM_USER (object);

        g_file_monitor_cancel (user->icon_monitor);

        g_free (user->user_name);
        g_free (user->real_name);
        g_free (user->display_name);
        g_free (user->icon_url);

        if (G_OBJECT_CLASS (mdm_user_parent_class)->finalize)
                (*G_OBJECT_CLASS (mdm_user_parent_class)->finalize) (object);
}

/**
 * _mdm_user_update:
 * @user: the user object to update.
 * @pwent: the user data to use.
 *
 * Updates the properties of @user using the data in @pwent.
 *
 * Since: 1.0
 **/
void
_mdm_user_update (MdmUser             *user,
                  const struct passwd *pwent)
{
        gchar *real_name = NULL;

        g_return_if_fail (MDM_IS_USER (user));
        g_return_if_fail (pwent != NULL);

        g_object_freeze_notify (G_OBJECT (user));

        /* Display Name */
        if (pwent->pw_gecos && pwent->pw_gecos[0] != '\0') {
                gchar *first_comma = NULL;
                gchar *valid_utf8_name = NULL;

                if (g_utf8_validate (pwent->pw_gecos, -1, NULL)) {
                        valid_utf8_name = pwent->pw_gecos;
                        first_comma = g_utf8_strchr (valid_utf8_name, -1, ',');
                } else {
                        g_warning ("User %s has invalid UTF-8 in GECOS field. "
                                   "It would be a good thing to check /etc/passwd.",
                                   pwent->pw_name ? pwent->pw_name : "");
                }

                if (first_comma) {
                        real_name = g_strndup (valid_utf8_name,
                                                  (first_comma - valid_utf8_name));
                } else if (valid_utf8_name) {
                        real_name = g_strdup (valid_utf8_name);
                } else {
                        real_name = NULL;
                }

                if (real_name && real_name[0] == '\0') {
                        g_free (real_name);
                        real_name = NULL;
                }
        } else {
                real_name = NULL;
        }

        if ((real_name && !user->real_name) ||
            (!real_name && user->real_name) ||
            (real_name &&
             user->real_name &&
             strcmp (real_name, user->real_name) != 0)) {
                g_free (user->real_name);
                user->real_name = real_name;
                g_object_notify (G_OBJECT (user), "real-name");
        } else {
                g_free (real_name);
        }

        /* Unique Display Name */
        if ((!user->real_name && user->display_name) ||
            (user->real_name &&
             user->display_name &&
             strncmp (user->real_name, user->display_name, strlen (user->real_name)) != 0)) {
                g_free (user->display_name);
                user->display_name = NULL;
                g_object_notify (G_OBJECT (user), "display-name");
        }

        /* UID */
        if (pwent->pw_uid != user->uid) {
                user->uid = pwent->pw_uid;
                g_object_notify (G_OBJECT (user), "uid");
        }

        /* Username */
        if ((pwent->pw_name && !user->user_name) ||
            (!pwent->pw_name && user->user_name) ||
            (pwent->pw_name &&
             user->user_name &&
             strcmp (user->user_name, pwent->pw_name) != 0)) {
                g_free (user->user_name);
                user->user_name = g_strdup (pwent->pw_name);
                g_object_notify (G_OBJECT (user), "user-name");
        }

        /* Home Directory */
        if ((pwent->pw_dir && !user->home_dir) ||
            (!pwent->pw_dir && user->home_dir) ||
            strcmp (user->home_dir, pwent->pw_dir) != 0) {
                g_free (user->home_dir);
                user->home_dir = g_strdup (pwent->pw_dir);
                g_object_notify (G_OBJECT (user), "home-directory");
                g_signal_emit (user, signals[ICON_CHANGED], 0);
        }

        /* Shell */
        if ((pwent->pw_shell && !user->shell) ||
            (!pwent->pw_shell && user->shell) ||
            (pwent->pw_shell &&
             user->shell &&
             strcmp (user->shell, pwent->pw_shell) != 0)) {
                g_free (user->shell);
                user->shell = g_strdup (pwent->pw_shell);
                g_object_notify (G_OBJECT (user), "shell");
        }

        update_icon_monitor (user);

        g_object_thaw_notify (G_OBJECT (user));
}

/**
 * _mdm_user_icon_changed:
 * @user: the user to emit the signal for.
 *
 * Emits the "icon-changed" signal for @user.
 *
 * Since: 1.0
 **/
void
_mdm_user_icon_changed (MdmUser *user)
{
        g_return_if_fail (MDM_IS_USER (user));

        g_signal_emit (user, signals[ICON_CHANGED], 0);
}

/**
 * mdm_user_get_uid:
 * @user: the user object to examine.
 *
 * Retrieves the ID of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

uid_t
mdm_user_get_uid (MdmUser *user)
{
        g_return_val_if_fail (MDM_IS_USER (user), -1);

        return user->uid;
}

/**
 * mdm_user_get_real_name:
 * @user: the user object to examine.
 *
 * Retrieves the display name of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/
const gchar *
mdm_user_get_real_name (MdmUser *user)
{
        g_return_val_if_fail (MDM_IS_USER (user), NULL);

        return (user->real_name ? user->real_name : user->user_name);
}

/**
 * mdm_user_get_display_name:
 * @user: the user object to examine.
 *
 * Retrieves the unique display name of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/
const gchar *
mdm_user_get_display_name (MdmUser *user)
{
        g_return_val_if_fail (MDM_IS_USER (user), NULL);

        return (user->display_name ? user->display_name
                : mdm_user_get_real_name (user));
}

/**
 * mdm_user_get_user_name:
 * @user: the user object to examine.
 *
 * Retrieves the login name of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

const gchar *
mdm_user_get_user_name (MdmUser *user)
{
        g_return_val_if_fail (MDM_IS_USER (user), NULL);

        return user->user_name;
}

/**
 * mdm_user_get_home_directory:
 * @user: the user object to examine.
 *
 * Retrieves the home directory of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

const gchar *
mdm_user_get_home_directory (MdmUser *user)
{
        g_return_val_if_fail (MDM_IS_USER (user), NULL);

        return user->home_dir;
}

/**
 * mdm_user_get_shell:
 * @user: the user object to examine.
 *
 * Retrieves the login shell of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

const gchar *
mdm_user_get_shell (MdmUser *user)
{
        g_return_val_if_fail (MDM_IS_USER (user), NULL);

        return user->shell;
}

gulong
mdm_user_get_login_frequency (MdmUser *user)
{
        g_return_val_if_fail (MDM_IS_USER (user), 0);

        return user->login_frequency;
}

int
mdm_user_collate (MdmUser *user1,
                  MdmUser *user2)
{
        const char *str1;
        const char *str2;
        gulong      num1;
        gulong      num2;

        g_return_val_if_fail (MDM_IS_USER (user1), 0);
        g_return_val_if_fail (MDM_IS_USER (user2), 0);

        if (user1->real_name != NULL) {
                str1 = user1->real_name;
        } else {
                str1 = user1->user_name;
        }

        if (user2->real_name != NULL) {
                str2 = user2->real_name;
        } else {
                str2 = user2->user_name;
        }

        num1 = user1->login_frequency;
        num2 = user2->login_frequency;
        g_debug ("Login freq 1=%u 2=%u", (guint)num1, (guint)num2);
        if (num1 > num2) {
                return -1;
        }

        if (num1 < num2) {
                return 1;
        }

        /* if login frequency is equal try names */
        if (str1 == NULL && str2 != NULL) {
                return -1;
        }

        if (str1 != NULL && str2 == NULL) {
                return 1;
        }

        if (str1 == NULL && str2 == NULL) {
                return 0;
        }

        return g_utf8_collate (str1, str2);
}

const char *
mdm_user_get_icon_url (MdmUser *user)
{
        g_return_val_if_fail (MDM_IS_USER (user), NULL);

        /* FIXME: Icon can be one of:
         * ~/.face
         * ~/.face.icon
         * ~/.mate/mdm:[face]picture
         * ${GlobalFaceDir}/${username}
         * ${GlobalFaceDir}/${username}.png
         * but we only monitor the first.
         */   
        return user->icon_url;
}
