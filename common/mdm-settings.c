/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <polkit/polkit.h>

#include "mdm-settings.h"
#include "mdm-settings-glue.h"

#include "mdm-settings-desktop-backend.h"

#include "mdm-marshal.h"

#define MDM_DBUS_PATH         "/org/mate/DisplayManager"
#define MDM_SETTINGS_DBUS_PATH MDM_DBUS_PATH "/Settings"
#define MDM_SETTINGS_DBUS_NAME "org.mate.DisplayManager.Settings"

#define MATECONF_SOUND_EVENT_KEY "/desktop/mate/sound/event_sounds"
#define MATECONF_FACE_BROWSER_DISABLE_KEY "/apps/mdm/simple-greeter/disable_user_list"

#define MDM_SETTINGS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MDM_TYPE_SETTINGS, MdmSettingsPrivate))

struct MdmSettingsPrivate
{
        DBusGConnection    *connection;
        MdmSettingsBackend *backend;
};

enum {
        VALUE_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     mdm_settings_class_init (MdmSettingsClass *klass);
static void     mdm_settings_init       (MdmSettings      *settings);
static void     mdm_settings_finalize   (GObject          *object);

static gpointer settings_object = NULL;

G_DEFINE_TYPE (MdmSettings, mdm_settings, G_TYPE_OBJECT)

GQuark
mdm_settings_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("mdm_settings_error");
        }

        return ret;
}

static void
set_mdm_uid_child_setup ()
{
        struct passwd *pwent;
        uid_t  mdm_uid;
        uid_t  mdm_gid;

        pwent = getpwnam (MDM_USERNAME);
        // --shutdown take into account the resuid/resgid and HOME whereas
        // --get --set take the euid/egid
        setenv("HOME", pwent->pw_dir, 1);
        mdm_uid = pwent->pw_uid;
        mdm_gid = pwent->pw_gid;
        setresgid (mdm_gid, mdm_gid, mdm_gid);
        setresuid (mdm_uid, mdm_uid, mdm_uid);

}

static gboolean
mdm_settings_get_mateconf_value (gchar *mateconf_key, gchar **value) {

        GError  *error = NULL;
        char    *shutdown_command[] = { "mateconftool-2", "--shutdown", NULL };
        char    *get_command[]  =  { "mateconftool-2", "--direct", "-g", mateconf_key, "--config-source", NULL, NULL };
        gboolean res;
        struct passwd *pwent;
        gboolean success = FALSE;

        pwent = getpwnam (MDM_USERNAME);
        if G_UNLIKELY (pwent == NULL)
                g_warning ("Can't access to 'mdm' user name in passwd");
        else {
                get_command[5] = g_strdup_printf("xml:readwrite:%s/.mateconf", pwent->pw_dir);
                res = g_spawn_sync (NULL,
                                    shutdown_command,
                                    NULL,
                                    G_SPAWN_SEARCH_PATH,
                                    (GSpawnChildSetupFunc)set_mdm_uid_child_setup,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    &error);
                if (!res) {
                        if (error != NULL) {
                                g_warning ("Unable to shutdown mateconf: %s", error->message);
                                g_error_free (error);
                        }
                        else
                                g_warning ("Unable to shutdown mateconf: unknown error");
                }
                else {
                        res = g_spawn_sync (NULL,
                                            get_command,
                                            NULL,
                                            G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                                            (GSpawnChildSetupFunc)set_mdm_uid_child_setup,
                                            NULL,
                                            value,
                                            NULL,
                                            NULL,
                                            &error);
                        if (!res) {
                                 if (error != NULL) {
                                         g_warning ("Unable to get event key to mateconf: %s", error->message);
                                         g_error_free (error);
                                 }
                                else
                                         g_warning ("Unable to get event key to mateconf: unknown error");
                         }
                        else {
                                 if (error != NULL) {
                                         g_warning ("Unable to get event key to mateconf: %s", error->message);
                                         g_error_free (error);
                                 }
                                 else {
                                         g_debug ("mateconftool call returning: %s", *value);
                                         success = TRUE;
                                 }
                        }
                }
        }

        return success;
}



static gboolean
mdm_settings_get_bool_mateconf_value (gchar *mateconf_key, gboolean *enabled) {

        gchar *value = NULL;
        gboolean result = FALSE;

        if (mdm_settings_get_mateconf_value(mateconf_key, &value)) {
                result = TRUE;
                if (strstr(value, "false") != NULL)
                        *enabled = FALSE;
                else if (strstr(value, "true") != NULL)
                        *enabled = TRUE;
                else
                    result = FALSE;
        }

        if (value)
                g_free (value);
        return result;

}

static gboolean
mdm_settings_set_mateconf_value (gchar *mateconf_key, gchar *type, gchar *value) {

        GError  *error = NULL;
        char    *shutdown_command[] = { "mateconftool-2", "--shutdown", NULL };
        char    *set_command[]  =  { "mateconftool-2", "--direct", "-s", mateconf_key, "--config-source", NULL, "-t", type, value, NULL };
        gboolean res;
        struct passwd *pwent;
        gboolean success = FALSE;

        pwent = getpwnam (MDM_USERNAME);
        if G_UNLIKELY (pwent == NULL)
                g_warning ("Can't access to 'mdm' user name in passwd");
        else {
                set_command[5] = g_strdup_printf("xml:readwrite:%s/.mateconf", pwent->pw_dir);
                res = g_spawn_sync (NULL,
                                    shutdown_command,
                                    NULL,
                                    G_SPAWN_SEARCH_PATH,
                                    (GSpawnChildSetupFunc)set_mdm_uid_child_setup,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    &error);
                if (!res) {
                        if (error != NULL) {
                                g_warning ("Unable to shutdown mateconf: %s", error->message);
                                g_error_free (error);
                        }
                        else
                                g_warning ("Unable to shutdown mateconf: unknown error");
                }
                else {
                        res = g_spawn_async (NULL,
                                             set_command,
                                             NULL,
                                             G_SPAWN_SEARCH_PATH
                                             | G_SPAWN_STDOUT_TO_DEV_NULL
                                             | G_SPAWN_STDERR_TO_DEV_NULL,
                                             (GSpawnChildSetupFunc)set_mdm_uid_child_setup,
                                             NULL,
                                             NULL,
                                             &error);
                        if (!res) {
                                if (error != NULL) {
                                        g_warning ("Unable to set event key to mateconf: %s", error->message);
                                        g_error_free (error);
                                 }
                                 else
                                        g_warning ("Unable to set event key to mateconf: unknown error");
                        }
                        else
                                 success = TRUE;
                }
        }

        return success;
}

static gboolean
mdm_settings_set_bool_mateconf_value (gchar *mateconf_key, gboolean enabled) {

        gchar *value = g_strdup_printf ("%i", enabled);
        gboolean result;

        result = mdm_settings_set_mateconf_value (mateconf_key, "bool", value);

        if (value)
                g_free (value);
        return result;
}


/*
dbus-send --system --print-reply --dest=org.mate.DisplayManager /org/mate/DisplayManager/Settings org.mate.DisplayManager.Settings.GetValue string:"xdmcp/Enable"
*/

gboolean
mdm_settings_get_value (MdmSettings *settings,
                        const char  *key,
                        char       **value,
                        GError     **error)
{
        GError  *local_error;
        gboolean res;

        g_return_val_if_fail (MDM_IS_SETTINGS (settings), FALSE);
        g_return_val_if_fail (key != NULL, FALSE);

        local_error = NULL;
        res = mdm_settings_backend_get_value (settings->priv->backend,
                                              key,
                                              value,
                                              &local_error);
        if (! res) {
                g_propagate_error (error, local_error);
        }

        return res;
}


/*
dbus-send --system --print-reply --dest=org.mate.DisplayManager /org/mate/DisplayManager/Settings org.mate.DisplayManager.Settings.GetSoundEnabled
*/

gboolean
mdm_settings_get_sound_enabled (MdmSettings *settings,
                                gboolean    *enabled,
                                GError     **error)
{
        gboolean res;
        g_debug ("Trying to get sound");

        g_return_val_if_fail (MDM_IS_SETTINGS (settings), FALSE);

        *enabled = FALSE;
        res = mdm_settings_get_bool_mateconf_value (MATECONF_SOUND_EVENT_KEY, enabled);
        if (res)
                  g_debug ("get sound returned: %i", *enabled);

        return TRUE;
}


/*
dbus-send --system --print-reply --dest=org.mate.DisplayManager /org/mate/DisplayManager/Settings org.mate.DisplayManager.Settings.GetFaceBrowserEnabled
*/

gboolean
mdm_settings_get_face_browser_enabled (MdmSettings *settings,
                                       gboolean    *enabled,
                                       GError     **error)
{
        gboolean res;

        g_return_val_if_fail (MDM_IS_SETTINGS (settings), FALSE);

        *enabled = TRUE;  
        res = mdm_settings_get_bool_mateconf_value (MATECONF_FACE_BROWSER_DISABLE_KEY, enabled);
        if (res)
            *enabled = !*enabled;

        return TRUE;
}

static void
unlock_auth_cb (PolkitAuthority *authority,
                GAsyncResult *result,
                DBusGMethodInvocation *context)
{
        PolkitAuthorizationResult *auth_result;
        GError  *error = NULL;

        auth_result = polkit_authority_check_authorization_finish (authority, result, &error);

        if (!auth_result)
                dbus_g_method_return_error (context, error);
        else {
                dbus_g_method_return (context,
                                      polkit_authorization_result_get_is_authorized (auth_result));
        }
    
        if (auth_result)
                g_object_unref (auth_result);
        if (error)
                g_error_free (error);
}

gboolean
mdm_settings_unlock (MdmSettings *settings,
                     DBusGMethodInvocation *context)
{
        polkit_authority_check_authorization (polkit_authority_get (),
                                              polkit_system_bus_name_new (dbus_g_method_get_sender (context)),
                                              "org.mate.displaymanager.settings.write",
                                              NULL,
                                              POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                              NULL,
                                              (GAsyncReadyCallback) unlock_auth_cb,
                                              context);
}

typedef struct
{
        MdmSettings *settings;
        DBusGMethodInvocation *context;
        gchar *key, *value;
} SetValueData;

typedef struct
{
        DBusGMethodInvocation *context;
        gboolean enabled;
} SetMateConfBooleanData;

static void
set_value_auth_cb (PolkitAuthority *authority,
                   GAsyncResult *result,
                   SetValueData *data)
{
        PolkitAuthorizationResult *auth_result;
        GError  *error = NULL;

        auth_result = polkit_authority_check_authorization_finish (authority, result, &error);

        if (!auth_result)
                dbus_g_method_return_error (data->context, error);
        else {
                if (polkit_authorization_result_get_is_authorized (auth_result)) {
                        gboolean result;
                    
                        result = mdm_settings_backend_set_value (data->settings->priv->backend,
                                                                 data->key,
                                                                 data->value,
                                                                 &error);
                        if (result)
                                dbus_g_method_return (data->context);
                        else
                                dbus_g_method_return_error (data->context, error);
                }
                else {
                        error = g_error_new (DBUS_GERROR_REMOTE_EXCEPTION, 0, "Not authorized");
                        dbus_g_method_return_error (data->context, error);
                }
        }
    
        if (auth_result)
                g_object_unref (auth_result);
        if (error)
                g_error_free (error);
        g_free (data->key);
        g_free (data->value);
        g_free (data);
}

/*
dbus-send --system --print-reply --dest=org.mate.DisplayManager /org/mate/DisplayManager/Settings org.mate.DisplayManager.Settings.SetValue string:"xdmcp/Enable" string:"false"
*/

gboolean
mdm_settings_set_value (MdmSettings *settings,
                        const char  *key,
                        const char  *value,
                        DBusGMethodInvocation *context)
{
        SetValueData *data;
    
        g_return_val_if_fail (MDM_IS_SETTINGS (settings), FALSE);
        g_return_val_if_fail (key != NULL, FALSE);

        g_debug ("Setting value %s", key);
    
        /* Authorize with PolicyKit */
        data = g_malloc (sizeof(SetValueData));
        data->settings = settings;
        data->context = context;
        data->key = g_strdup(key);
        data->value = g_strdup(value);    
        polkit_authority_check_authorization (polkit_authority_get (),
                                              polkit_system_bus_name_new (dbus_g_method_get_sender (context)),
                                              "org.mate.displaymanager.settings.write",
                                              NULL,
                                              POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                              NULL,
                                              (GAsyncReadyCallback) set_value_auth_cb,
                                              data);
        return TRUE;
}

static void
set_sound_enabled_auth_cb (PolkitAuthority *authority,
                           GAsyncResult *result,
                           SetMateConfBooleanData *data)
{
        PolkitAuthorizationResult *auth_result;
        GError  *error = NULL;
        
        auth_result = polkit_authority_check_authorization_finish (authority, result, &error);

        if (!auth_result)
                dbus_g_method_return_error (data->context, error);
        else {
                if (polkit_authorization_result_get_is_authorized (auth_result)) {
                        if (!mdm_settings_set_bool_mateconf_value (MATECONF_SOUND_EVENT_KEY, data->enabled))
                                g_warning ("set new value for sound failed");
                        dbus_g_method_return (data->context);
                }
                else {
                        error = g_error_new (DBUS_GERROR_REMOTE_EXCEPTION, 0, "Not authorized");
                        dbus_g_method_return_error (data->context, error);
                }
        }
    
        if (auth_result)
                g_object_unref (auth_result);
        if (error)
                g_error_free (error);

        g_free (data);
}

/*
dbus-send --system --print-reply --dest=org.mate.DisplayManager /org/mate/DisplayManager/Settings org.mate.DisplayManager.Settings.SetSoundEnabled boolean:false
*/

gboolean
mdm_settings_set_sound_enabled (MdmSettings *settings,
                                gboolean     enabled,
                                DBusGMethodInvocation *context)
{
        SetMateConfBooleanData *data;
    
        g_return_val_if_fail (MDM_IS_SETTINGS (settings), FALSE);


        g_debug ("Setting sound enabled to %s", enabled ? "true" : "false");
    
        /* Authorize with PolicyKit */
        data = g_malloc (sizeof(SetMateConfBooleanData));
        data->context = context;
        data->enabled = enabled;
        polkit_authority_check_authorization (polkit_authority_get (),
                                              polkit_system_bus_name_new (dbus_g_method_get_sender (context)),
                                              "org.mate.displaymanager.settings.write",
                                              NULL,
                                              POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                              NULL,
                                              (GAsyncReadyCallback) set_sound_enabled_auth_cb,
                                              data);
        return TRUE;
}

static void
set_face_browser_enabled_auth_cb (PolkitAuthority *authority,
                                  GAsyncResult *result,
                                  SetMateConfBooleanData *data)
{
        PolkitAuthorizationResult *auth_result;
        GError  *error = NULL;
        
        auth_result = polkit_authority_check_authorization_finish (authority, result, &error);

        if (!auth_result)
                dbus_g_method_return_error (data->context, error);
        else {
                if (polkit_authorization_result_get_is_authorized (auth_result)) {
                        if (!mdm_settings_set_bool_mateconf_value (MATECONF_FACE_BROWSER_DISABLE_KEY, !data->enabled))
                                g_warning ("set new value for face browser failed");
                        dbus_g_method_return (data->context);
                }
                else {
                        error = g_error_new (DBUS_GERROR_REMOTE_EXCEPTION, 0, "Not authorized");
                        dbus_g_method_return_error (data->context, error);
                }
        }
    
        if (auth_result)
                g_object_unref (auth_result);
        if (error)
                g_error_free (error);

        g_free (data);
}

/*
dbus-send --system --print-reply --dest=org.mate.DisplayManager /org/mate/DisplayManager/Settings org.mate.DisplayManager.Settings.SetFaceBrowserEnabled boolean:true
*/

gboolean
mdm_settings_set_face_browser_enabled (MdmSettings *settings,
                                       gboolean     enabled,
                                       DBusGMethodInvocation *context)
{
        SetMateConfBooleanData *data;
    
        g_return_val_if_fail (MDM_IS_SETTINGS (settings), FALSE);
    
        /* Authorize with PolicyKit */
        data = g_malloc (sizeof(SetMateConfBooleanData));
        data->context = context;
        data->enabled = enabled;
        polkit_authority_check_authorization (polkit_authority_get (),
                                              polkit_system_bus_name_new (dbus_g_method_get_sender (context)),
                                              "org.mate.displaymanager.settings.write",
                                              NULL,
                                              POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                              NULL,
                                              (GAsyncReadyCallback) set_face_browser_enabled_auth_cb,
                                              data);
        return TRUE;
}

static gboolean
register_settings (MdmSettings *settings)
{
        GError *error = NULL;

        error = NULL;
        settings->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (settings->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        dbus_g_connection_register_g_object (settings->priv->connection, MDM_SETTINGS_DBUS_PATH, G_OBJECT (settings));

        return TRUE;
}

/*
dbus-send --system --print-reply --dest=org.mate.DisplayManager /org/mate/DisplayManager/Settings org.freedesktop.DBus.Introspectable.Introspect
*/

static void
mdm_settings_class_init (MdmSettingsClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = mdm_settings_finalize;

        signals [VALUE_CHANGED] =
                g_signal_new ("value-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (MdmSettingsClass, value_changed),
                              NULL,
                              NULL,
                              mdm_marshal_VOID__STRING_STRING_STRING,
                              G_TYPE_NONE,
                              3,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_STRING);

        g_type_class_add_private (klass, sizeof (MdmSettingsPrivate));

        dbus_g_object_type_install_info (MDM_TYPE_SETTINGS, &dbus_glib_mdm_settings_object_info);
}

static void
backend_value_changed (MdmSettingsBackend *backend,
                       const char         *key,
                       const char         *old_value,
                       const char         *new_value,
                       MdmSettings        *settings)
{
        g_debug ("Emitting value-changed %s %s %s", key, old_value, new_value);
        /* just proxy it */
        g_signal_emit (settings,
                       signals [VALUE_CHANGED],
                       0,
                       key,
                       old_value,
                       new_value);
}

static void
mdm_settings_init (MdmSettings *settings)
{
        settings->priv = MDM_SETTINGS_GET_PRIVATE (settings);

        settings->priv->backend = mdm_settings_desktop_backend_new ();
        g_signal_connect (settings->priv->backend,
                          "value-changed",
                          G_CALLBACK (backend_value_changed),
                          settings);
}

static void
mdm_settings_finalize (GObject *object)
{
        MdmSettings *settings;

        g_return_if_fail (object != NULL);
        g_return_if_fail (MDM_IS_SETTINGS (object));

        settings = MDM_SETTINGS (object);

        g_return_if_fail (settings->priv != NULL);

        if (settings->priv->backend != NULL) {
                g_object_unref (settings->priv->backend);
        }

        G_OBJECT_CLASS (mdm_settings_parent_class)->finalize (object);
}

MdmSettings *
mdm_settings_new (void)
{
        if (settings_object != NULL) {
                g_object_ref (settings_object);
        } else {
                gboolean res;

                settings_object = g_object_new (MDM_TYPE_SETTINGS, NULL);
                g_object_add_weak_pointer (settings_object,
                                           (gpointer *) &settings_object);
                res = register_settings (settings_object);
                if (! res) {
                        g_warning ("Unable to register settings");
                        g_object_unref (settings_object);
                        return NULL;
                }
        }

        return MDM_SETTINGS (settings_object);
}
