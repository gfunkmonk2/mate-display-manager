#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib-bindings.h>

#define MDM_SETTINGS_DBUS_NAME "org.mate.DisplayManager"
#define MDM_SETTINGS_DBUS_PATH "/org/mate/DisplayManager/Settings"
#define MDM_SETTINGS_DBUS_INTERFACE "org.mate.DisplayManager.Settings"

#define SESSION_KEY_GROUP "daemon"
#define SESSION_KEY_NAME  "DefaultSession"

typedef enum {
    CONNEXIONSUCCEED,
    CONNEXIONFAILED,
    ALREADYHASVALUE,
    HASNOVALUE,
    VALUEFOUND
} DBusState;

static gboolean debug = FALSE;
static gboolean keepold = FALSE;
static gboolean remove = FALSE;

static GOptionEntry entries[] =
{
  { "debug", 'd', 0, G_OPTION_ARG_NONE, &debug, "Enable debugging", NULL },
  { "keep-old", 'k', 0, G_OPTION_ARG_NONE, &keepold, "Only update if no default already set", NULL },
  { "remove", 'r', 0, G_OPTION_ARG_NONE, &remove, "Remove default session if it's this one", NULL },
  { NULL }
};

void
show_nothing(const gchar   *log_domain,
             GLogLevelFlags log_level,
             const gchar   *message,
             gpointer       unused_data) {};

int
init_dbus_connection(DBusGProxy **proxy) {
    DBusGConnection *connection;
    GError          *error = NULL;

    connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
    if (connection == NULL) {
         g_debug ("Can't connect to system bus: %s", error->message);
         g_error_free (error);
         return(CONNEXIONFAILED);
    }

    *proxy = dbus_g_proxy_new_for_name_owner (connection,
                                              MDM_SETTINGS_DBUS_NAME,
                                              MDM_SETTINGS_DBUS_PATH,
                                              MDM_SETTINGS_DBUS_INTERFACE,
                                              &error);
    if(!*proxy) {
         g_debug ("No object on the bus: %s", error->message);
         g_error_free (error);
         return(CONNEXIONFAILED);
    }

    return(CONNEXIONSUCCEED);
}

int
get_default_session_name_with_dbus(DBusGProxy *proxy, gchar **name)
{
    GError *error = NULL;

    if (!dbus_g_proxy_call (proxy, "GetValue", &error,
                            G_TYPE_STRING, SESSION_KEY_GROUP "/" SESSION_KEY_NAME, G_TYPE_INVALID,
                            G_TYPE_STRING, name, G_TYPE_INVALID)) {
        // This probably (_owner used previously) means that the value doesn't exist in config file
        if(error->domain == DBUS_GERROR && error->code == DBUS_GERROR_REMOTE_EXCEPTION) {
            g_debug ("Probably no value registered: %s. %s", dbus_g_error_get_name (error), error->message);
            g_error_free (error);
            return(HASNOVALUE);
        }
        // possible if MDM_SETTINGS_DBUS_PATH or MDM_SETTINGS_DBUS_INTERFACE aren't exposed by the
        // existing MDM_SETTINGS_DBUS_NAME (shouldn't happen)
        else {
            g_debug ("No MDM_SETTINGS_DBUS_PATH or MDM_SETTINGS_DBUS_INTERFACE on the bus: %s", error->message);
            g_error_free (error);
            return(CONNEXIONFAILED);
        }
    }
    return(VALUEFOUND);

}

int
set_default_session_name_with_dbus(DBusGProxy *proxy, gchar *sessionname)
{
    GError *error = NULL;
    
    dbus_g_proxy_call (proxy, "SetValue", &error,
                       G_TYPE_STRING, SESSION_KEY_GROUP "/" SESSION_KEY_NAME,
                       G_TYPE_STRING, sessionname, G_TYPE_INVALID, G_TYPE_INVALID);
    if (error) {
        g_debug ("Error changing default session value to '%s': %s\nNo update will be done", sessionname, error->message);
        g_error_free (error);
        return FALSE;
    }
    
    return TRUE;
}

int
update_session_if_needed(gchar *default_session, gchar *proposed_session, gboolean dbusupdate, gpointer *parameter)
{
    DBusGProxy      *proxy = NULL;
    GKeyFile        *keyfile = NULL;
    gboolean         success = FALSE;

    if (dbusupdate)
        proxy = (DBusGProxy *) parameter;
    else {
        keyfile = (GKeyFile *) parameter;
        success = TRUE; // by default, the function succeed (return void)
    }
        
    if (!(default_session)) {
        g_debug("No value previously set. Update to %s", proposed_session);
        if (dbusupdate)
            success = set_default_session_name_with_dbus(proxy, proposed_session);
        else
            g_key_file_set_string (keyfile, SESSION_KEY_GROUP, SESSION_KEY_NAME, proposed_session);
    }
    else {
        if (remove) {
            if (g_strcmp0(proposed_session, default_session) == 0) {
                g_debug("Remove %s as default session", proposed_session);
                if (dbusupdate)
                    success = set_default_session_name_with_dbus(proxy, "");
                else
                    g_key_file_set_string (keyfile, SESSION_KEY_GROUP, SESSION_KEY_NAME, "");
                if (!success)
                    return(2);
                return(0);
            }
            g_debug("Don't remove: %s not default session", proposed_session);
            return(4);
        }
        if (strlen(default_session) < 1) {
            g_debug("Empty value set as mdm default session. Set to %s", proposed_session);
            if (dbusupdate)
                success = set_default_session_name_with_dbus(proxy, proposed_session);
            else
                g_key_file_set_string (keyfile, SESSION_KEY_GROUP, SESSION_KEY_NAME, proposed_session);
        }
        else {
            g_debug("Found existing default session: %s", default_session);
            if(keepold)
                g_debug("keep-old mode: keep previous default session");
            else {
                g_debug("Update to %s", proposed_session);
                if (dbusupdate)
                    success = set_default_session_name_with_dbus(proxy, proposed_session);
                else
                    g_key_file_set_string (keyfile, SESSION_KEY_GROUP, SESSION_KEY_NAME, proposed_session);
            }
        }
    }
    if (!success)
        return(2);
    return(0);
}

int 
main (int argc, char *argv[])
{
    GOptionContext *context = NULL;
    GError         *error = NULL;

    DBusGProxy     *proxy = NULL;
    DBusState       return_dbus_code = CONNEXIONFAILED;
    gboolean        dbus_connexion_ok = FALSE;

    GKeyFile       *keyfile;
    GKeyFileFlags   flags;
    gchar          *s_data;
    gsize           size;
    const gchar    *mdm_conf_file = MDMCONFDIR "/custom.conf";

    gchar          *default_session = NULL;
    gchar          *proposed_session = NULL;
    gint            return_code;

    g_type_init ();

    context = g_option_context_new (_("- set mdm default session"));
    g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_printerr (_("option parsing failed: %s\n"), error->message);
        g_option_context_free(context);
        g_error_free (error);
        exit (1);
    }
    if (argc!=2) {
        g_printerr(_("Wrong usage of the command\n%s"), g_option_context_get_help (context, FALSE, NULL));
        g_option_context_free(context); 
        exit(1);
    }
    if (context)
        g_option_context_free(context); 
    if (!debug)
        g_log_set_handler (NULL, G_LOG_LEVEL_DEBUG, show_nothing, NULL);
    proposed_session = argv[1];


    if (init_dbus_connection(&proxy) == CONNEXIONSUCCEED) {
        return_dbus_code = get_default_session_name_with_dbus(proxy, &default_session);
        if (return_dbus_code == CONNEXIONFAILED)
            dbus_connexion_ok = FALSE; // dbus and service connexion ok, but can't access proxy
        else {
            dbus_connexion_ok = TRUE;
            if (return_dbus_code == HASNOVALUE)
                default_session = NULL;
            return_code = update_session_if_needed (default_session, proposed_session, TRUE, (gpointer *) proxy);
        }
    }
    if (proxy)
       g_object_unref (proxy);

    if (!dbus_connexion_ok) {
        g_debug ("Can't change value by dbus, failback in %s direct modification", mdm_conf_file);
        if (geteuid() != 0) {
            g_printerr ("Updating directly %s requires root permission\n", mdm_conf_file);
            exit(1);
        }
        keyfile = g_key_file_new ();
        flags = G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;
        if (!(g_key_file_load_from_file (keyfile, mdm_conf_file, flags, &error))) {
            g_debug ("File doesn't seem to exist or can't be read: create one (%s)", error->message);
            g_error_free (error);
            error = NULL;
        }
        // try to get the right key
        default_session = g_key_file_get_string (keyfile, SESSION_KEY_GROUP, SESSION_KEY_NAME, NULL);
        return_code = update_session_if_needed (default_session, proposed_session, FALSE, (gpointer *) keyfile);
        if(return_code == 0) {
            s_data = g_key_file_to_data (keyfile, &size, &error);
            if (!s_data) {
                g_debug ("Can't convert data to string: %s", error->message);
                g_error_free (error);
                return_code = 1;
            }
            else {
                if(!g_file_set_contents (mdm_conf_file, s_data, size, &error)) {
                    g_printerr ("Can't update: %s\n", error->message);
                    g_error_free (error);
                    return_code = 1;
                }
                g_free(s_data);
             }
        }
        g_key_file_free(keyfile);
    }

    if(default_session)
        g_free(default_session);

    exit(return_code);

}
