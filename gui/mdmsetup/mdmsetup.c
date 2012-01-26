#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <dbus/dbus-glib.h>
#include <glib/gi18n.h>

#include "mdm-user-manager.h"
#include "mdm-sessions.h"

#define MAX_USERS_IN_COMBO_BOX 20

/* User interface */
static GtkBuilder *ui;
static GtkWidget *dialog, *unlock_button, *option_vbox;
static GtkWidget *user_combo, *user_entry, *delay_spin;
static GtkWidget *session_combo;
static GtkWidget *auto_login_radio, *login_delay_box, *login_delay_check, *sound_enable_check, *face_browser_enable_check;

/* Timer to delay application of configuration */
static guint apply_timeout = 0;

/* Flag to show when information is user_login_loaded */
static gboolean user_login_loaded = FALSE;
static gboolean session_loaded = FALSE;

/* Flag to show when information is loaded */
static gboolean locked = TRUE;

/* True if the user selection method is a combo box, False if an entry */
static gboolean user_list_is_combo = TRUE;

/* User information */
static MdmUserManager *user_manager;

/* Proxy to MDM settings */
static DBusGProxy *proxy = NULL;


static gboolean
get_sound_enabled ()
{
    gboolean value;
    GError *error = NULL;
    
    if (!dbus_g_proxy_call (proxy, "GetSoundEnabled", &error,
                            G_TYPE_INVALID,
                            G_TYPE_BOOLEAN, &value, G_TYPE_INVALID)) {
        g_warning ("Error calling GetSoundEnabled(): %s", error->message);
        return FALSE;
    }
  
    return value;
}


static gboolean
set_sound_enabled (gboolean enabled)
{
    GError *error = NULL;
    
    dbus_g_proxy_call (proxy, "SetSoundEnabled", &error,
                       G_TYPE_BOOLEAN, enabled, G_TYPE_INVALID,
                       G_TYPE_INVALID);
    if (error) {
        g_warning ("Error calling SetSoundEnabled(%s): %s", enabled ? "TRUE" : "FALSE", error->message);
        return FALSE;
    }
    
    return TRUE;
}


static gboolean
get_face_browser_enabled ()
{
    gboolean value;
    GError *error = NULL;
    
    if (!dbus_g_proxy_call (proxy, "GetFaceBrowserEnabled", &error,
                            G_TYPE_INVALID,
                            G_TYPE_BOOLEAN, &value, G_TYPE_INVALID)) {
        g_warning ("Error calling GetFaceBrowserEnabled(): %s", error->message);
        return FALSE;
    }
  
    return value;
}


static gboolean
set_face_browser_enabled (gboolean enabled)
{
    GError *error = NULL;
    
    dbus_g_proxy_call (proxy, "SetFaceBrowserEnabled", &error,
                       G_TYPE_BOOLEAN, enabled, G_TYPE_INVALID,
                       G_TYPE_INVALID);
    if (error) {
        g_warning ("Error calling SetFaceBrowserEnabled(%s): %s", enabled ? "TRUE" : "FALSE", error->message);
        return FALSE;
    }
    
    return TRUE;
}


static gchar *
get_value (const gchar *key, gchar *def)
{
    gchar *value;
    GError *error = NULL;
    
    if (!dbus_g_proxy_call (proxy, "GetValue", &error,
                            G_TYPE_STRING, key, G_TYPE_INVALID,
                            G_TYPE_STRING, &value, G_TYPE_INVALID)) {
        g_warning ("Error calling GetValue('%s'): %s", key, error->message);
        return def;
    }
    
    return value;
}


static gboolean
set_value (const gchar *key, const gchar *value)
{
    GError *error = NULL;
    
    dbus_g_proxy_call (proxy, "SetValue", &error,
                       G_TYPE_STRING, key,
                       G_TYPE_STRING, value, G_TYPE_INVALID, G_TYPE_INVALID);
    if (error) {
        g_warning ("Error calling SetValue('%s', '%s'): %s", key, value, error->message);
        return FALSE;
    }
    
    return TRUE;
}


static gboolean
get_boolean_value (const gchar *key, gboolean def)
{
    gchar *value;
    gboolean result;
    
    value = get_value (key, NULL);
    if (!value)
        return def;
    result = strcmp (value, "true") == 0;
    g_free (value);
    return result;
}


static gint
get_integer_value (const gchar *key, gint def)
{
    gchar *value;
    gint result;
    char *end;
    
    value = get_value (key, NULL);
    if (!value || value[0] == '\0')
        result = def;
    else {
        result = strtol (value, &end, 10);
        if (*end != '\0')
            result = def;
    }

    if (value)
        g_free (value);
    return result;
}


static gboolean
set_boolean_value (const gchar *key, gboolean value)
{
    return set_value (key, value ? "true" : "false");
}


static gboolean
set_integer_value (const gchar *key, gint value)
{
    char value_string[1024];
    snprintf(value_string, 1024, "%d", value);
    return set_value (key, value_string);
}


static void
update_config ()
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *user = NULL;
    gint delay = 0;
    gboolean auto_login = FALSE, timed_login = FALSE, error = FALSE;
    gchar *default_session = NULL;
    
    if (apply_timeout != 0) {
        g_source_remove (apply_timeout);
        apply_timeout = 0;
    }

    if (user_list_is_combo) {
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (user_combo));
        if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (user_combo), &iter))
            gtk_tree_model_get (model, &iter, 1, &user, -1);
    }
    else
        user = g_strdup (gtk_entry_get_text (GTK_ENTRY (user_entry)));

    model = gtk_combo_box_get_model (GTK_COMBO_BOX (session_combo));
    if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (session_combo), &iter))
        gtk_tree_model_get (model, &iter, 1, &default_session, -1);

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (auto_login_radio))) {
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (login_delay_check)))
            timed_login = TRUE;
        else
            auto_login = TRUE;
    }
    
    delay = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (delay_spin));

    g_debug ("set user='%s', auto=%s, timed=%s, delay=%d, default_session=%s",
             user,
             auto_login ? "True" : "False",
             timed_login ? "True" : "False", delay,
             default_session);
    
    if (!set_boolean_value ("daemon/TimedLoginEnable", timed_login) ||
        !set_boolean_value ("daemon/AutomaticLoginEnable", auto_login) ||
        !set_value ("daemon/TimedLogin", user) ||
        !set_value ("daemon/AutomaticLogin", user) ||    
        !set_integer_value ("daemon/TimedLoginDelay", delay) ||
        !set_value ("daemon/DefaultSession", default_session))
        error = TRUE;

    if (user)
        g_free (user);
}


G_MODULE_EXPORT
void
mdm_capplet_response_cb (GtkWidget *widget, gint response_id)
{
    if (response_id != 1)
        gtk_main_quit ();
}


static void
unlock_response_cb (DBusGProxy     *proxy,
                    DBusGProxyCall *call_id,
                    void           *user_data)
{
    gboolean is_unlocked;
    GError *error = NULL;
    
    dbus_g_proxy_end_call (proxy, call_id, &error, G_TYPE_BOOLEAN, &is_unlocked, G_TYPE_INVALID);
    if (error) {
        g_warning ("Failed to unlock: %s", error->message);
        g_error_free (error);
        return;
    }
    
    if (!is_unlocked)
        return;
    
    locked = FALSE;
    gtk_widget_set_sensitive (unlock_button, FALSE);
    gtk_widget_set_sensitive (option_vbox, user_login_loaded && !locked);
}


G_MODULE_EXPORT
void
unlock_button_clicked_cb (GtkWidget *widget)
{
    dbus_g_proxy_begin_call (proxy, "Unlock", unlock_response_cb, NULL, NULL, G_TYPE_INVALID);
}


G_MODULE_EXPORT
void
login_delay_check_toggled_cb (GtkWidget *widget)
{
    gtk_widget_set_sensitive (delay_spin,
                              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (login_delay_check)));
    
    if (session_loaded && user_login_loaded)
        update_config ();
}


G_MODULE_EXPORT
void
sound_enable_check_toggled_cb (GtkWidget *widget)
{
   set_sound_enabled (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));
}


G_MODULE_EXPORT
void
face_browser_enable_check_toggled_cb (GtkWidget *widget)
{
   set_face_browser_enabled (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)));
}


static gboolean
delayed_apply_cb ()
{
    update_config ();
    return FALSE;
}


G_MODULE_EXPORT
void
apply_config_cb (GtkWidget *widget)
{
    if (session_loaded && user_login_loaded) {
        if (apply_timeout != 0)
            g_source_remove (apply_timeout);
        apply_timeout = g_timeout_add (200, (GSourceFunc)delayed_apply_cb, NULL);
    }
}


static void
init_login_delay ()
{
    gint delay;
    
    if (get_boolean_value ("daemon/TimedLoginEnable", FALSE))
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (login_delay_check), TRUE);
    
    delay = get_integer_value ("daemon/TimedLoginDelay", 30);

    g_debug ("init delay=%d", delay);

    gtk_spin_button_set_value (GTK_SPIN_BUTTON (delay_spin), delay);
}


G_MODULE_EXPORT
void
automatic_login_toggle_cb (GtkWidget *automatic_login_toggle)    
{
    gboolean automatic_login;

    automatic_login = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (automatic_login_toggle));
    gtk_widget_set_sensitive (login_delay_box, automatic_login);
    gtk_widget_set_sensitive (user_combo, automatic_login);
    gtk_widget_set_sensitive (user_entry, automatic_login);
    gtk_widget_set_sensitive (face_browser_enable_check, !automatic_login);

    if (session_loaded && user_login_loaded)
        update_config ();
}


G_MODULE_EXPORT
void
default_user_combo_box_changed_cb (void)
{
    if (session_loaded && user_login_loaded) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (auto_login_radio), TRUE);
        update_config ();
    }
}

G_MODULE_EXPORT
void
default_session_combo_box_changed_cb (void)
{
    if (session_loaded && user_login_loaded)
        update_config ();
}


static void
init_default_user (void)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean auto_login, timed_login, active;
    gchar *user = NULL;
    
    auto_login = get_boolean_value ("daemon/AutomaticLoginEnable", FALSE);
    timed_login = get_boolean_value ("daemon/TimedLoginEnable", FALSE);
    
    if (auto_login)
        user = get_value ("daemon/AutomaticLogin", NULL);
    if (user == NULL)
        user = get_value ("daemon/TimedLogin", NULL);

    g_debug ("init user='%s' auto=%s", user, auto_login || timed_login ? "True" : "False");

    if (auto_login || timed_login)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (auto_login_radio), TRUE);
    
    if (!user_list_is_combo) {
        if (user != NULL)
            gtk_entry_set_text (GTK_ENTRY (user_entry), user);
    }
    else {
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (user_combo));
        active = gtk_tree_model_get_iter_first (model, &iter);
        
        /* If no user then use first available */
        if (user == NULL) {
            if (active)
                gtk_combo_box_set_active_iter (GTK_COMBO_BOX (user_combo), &iter);
        }
        else {
            while (user != NULL && active) {
                gchar *u;
                gboolean matched;
            
                gtk_tree_model_get (model, &iter, 1, &u, -1);
                matched = strcmp (user, u) == 0;
                g_free (u);
                if (matched) {
                    gtk_combo_box_set_active_iter (GTK_COMBO_BOX (user_combo), &iter);
                    break;
                }
            
                active = gtk_tree_model_iter_next (model, &iter);
            }
        }
    }
    
    g_free (user);
}


static void
init_default_session (void)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean active;
    gchar *default_session = NULL;

    default_session = get_value ("daemon/DefaultSession", NULL);
    g_debug ("Init default session found:'%s'", default_session);

    model = gtk_combo_box_get_model (GTK_COMBO_BOX (session_combo));
    active = gtk_tree_model_get_iter_first (model, &iter);

    /* If no default session then use mate one */
    if (default_session == NULL || strlen(default_session) == 0)
        default_session = g_strdup("mate");

    while (active) {
        gchar *u;
        gboolean matched;

        gtk_tree_model_get (model, &iter, 1, &u, -1);
        matched = strcmp (default_session, u) == 0;
        g_free (u);
        if (matched) {
            gtk_combo_box_set_active_iter (GTK_COMBO_BOX (session_combo), &iter);
            break;
        }

        active = gtk_tree_model_iter_next (model, &iter);
    }
    session_loaded = TRUE;

    g_free (default_session);
}


static void add_user (MdmUser *user)
{
    GtkListStore *model;
    GtkTreeIter iter;
    GString *label;
    const gchar *home_user_dir;
    gchar *encryption_path;
    gchar *encryption_content;
    GFile *encryption_file;
    GString *encryption_mount;
    gboolean using_encryption = FALSE;

    /* don't enable autologin for user having encrypted home dir
       there are some corner case if mdmsetup isn't run as root and another user
       than the current one is using encrypted dir: you can't check for them if
       they access to it. The solution is the server to refuse the change then
       (too late in the cycle for such changes). */
    home_user_dir = mdm_user_get_home_directory (user);
    encryption_path = g_build_filename (home_user_dir, ".ecryptfs", "Private.mnt", NULL);
    encryption_file = g_file_new_for_path (encryption_path);
    if (g_file_load_contents (encryption_file, NULL, &encryption_content, NULL, NULL, NULL)) {
        if (g_str_has_suffix (encryption_content, "\n"))
            encryption_content[strlen(encryption_content)-1] = '\0';
        encryption_mount = g_string_new(encryption_content);
        if (strcmp (encryption_mount->str, home_user_dir) == 0)
            using_encryption = TRUE;
        g_string_free (encryption_mount, TRUE);
    }
    if (encryption_file)
        g_object_unref (encryption_file);
    g_free (encryption_path);

    if (using_encryption) {
        g_debug ("%s is using an encrypted home, not listing him for autologin", mdm_user_get_real_name (user));
        return;
    }

    model = GTK_LIST_STORE (gtk_builder_get_object (ui, "login_user_model"));
    gtk_list_store_append (model, &iter);
    label = g_string_new("");
    g_string_printf (label, "%s (%s)", mdm_user_get_real_name (user), mdm_user_get_user_name (user));
    gtk_list_store_set (model, &iter,
                        0, label->str,
                        1, mdm_user_get_user_name (user),
                        -1);
    g_string_free (label, TRUE);
}


static void add_session (gchar *session_id, gchar *name, gchar *comment)
{
    GtkListStore *model;
    GtkTreeIter iter;
    GString *label;    

    model = GTK_LIST_STORE (gtk_builder_get_object (ui, "session_model"));
    gtk_list_store_append (model, &iter);
    label = g_string_new("");
    g_string_printf (label, "%s", name);
    gtk_list_store_set (model, &iter,
                        0, label->str,
                        1, session_id,
                        -1);
    g_string_free (label, TRUE);
}


static void
users_loaded_cb(MdmUserManager *manager)
{
    GSList *users, *item;

    users = mdm_user_manager_list_users (user_manager);
    
    if (g_slist_length (users) > MAX_USERS_IN_COMBO_BOX) {
        user_list_is_combo = FALSE;
        gtk_widget_hide (user_combo);
        gtk_widget_show (user_entry);
    }
    else {
        for (item = users; item; item = item->next)
            add_user ((MdmUser *) item->data);
    }

    init_default_user ();

    user_login_loaded = TRUE;
    gtk_widget_set_sensitive (option_vbox, user_login_loaded && !locked);
}


static void
user_added_cb (MdmUserManager *manager, MdmUser *user)
{
    if (session_loaded && user_login_loaded)
        add_user (user);
}


static void
load_sessions_cb(void)
{
    gchar     **session_ids;
    int        i;

    session_ids = mdm_get_all_sessions ();

    for (i = 0; session_ids[i] != NULL; i++) {
        gchar *name;
        gchar *comment;
        if (!mdm_get_details_for_session (session_ids[i],
                                          &name, &comment)) {
            continue;
        }
        add_session (session_ids[i], name, comment);
            g_free (name);
            g_free (comment);
    }
    g_strfreev (session_ids);

    init_default_session();
}


static void
split_text (const gchar *text, const gchar *prefix_label_name, const gchar *suffix_label_name)
{
    gchar **tokens;
    GtkWidget *prefix_label, *suffix_label;
    
    prefix_label = GTK_WIDGET (gtk_builder_get_object (ui, prefix_label_name));
    suffix_label = GTK_WIDGET (gtk_builder_get_object (ui, suffix_label_name));
    
    tokens = g_strsplit (text, "%s", 2);
    if (tokens[0] != NULL && tokens[1] != NULL) {
        if (tokens[0][0] != '\0')
            gtk_label_set_text (GTK_LABEL (prefix_label), g_strstrip (tokens[0]));
        else
            gtk_widget_hide (prefix_label);
        if (tokens[1][0] != '\0')
            gtk_label_set_text (GTK_LABEL (suffix_label), g_strstrip (tokens[1]));
        else
            gtk_widget_hide (suffix_label);
    }
    g_strfreev (tokens);
}


int main (int argc, char **argv)
{
    GtkCellRenderer *renderer;
    DBusGConnection *connection;
    GError *error = NULL;
    
    bindtextdomain (GETTEXT_PACKAGE, MATELOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    gtk_init (&argc, &argv);
    
    connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
    if (error) {
        g_warning ("Failed to get system bus: %s", error->message);
        return 1;
    }
    proxy = dbus_g_proxy_new_for_name (connection,
                                       "org.mate.DisplayManager",
                                       "/org/mate/DisplayManager/Settings",
                                       "org.mate.DisplayManager.Settings");

    ui = gtk_builder_new ();
    gtk_builder_add_from_file (ui, UIDIR "/mdmsetup.ui", &error);
    if (error) {
        g_warning ("Failed to load UI: %s", error->message);
        return 1;
    }
    dialog = GTK_WIDGET (gtk_builder_get_object (ui, "mdm_capplet"));
    unlock_button = GTK_WIDGET (gtk_builder_get_object (ui, "unlock_button"));
    option_vbox = GTK_WIDGET (gtk_builder_get_object (ui, "mdm_capplet_vbox"));
    user_combo = GTK_WIDGET (gtk_builder_get_object (ui, "default_user_combo_box"));
    session_combo = GTK_WIDGET (gtk_builder_get_object (ui, "default_session_combo_box"));
    user_entry = GTK_WIDGET (gtk_builder_get_object (ui, "default_user_entry"));
    delay_spin = GTK_WIDGET (gtk_builder_get_object (ui, "login_delay_spin"));
    auto_login_radio = GTK_WIDGET (gtk_builder_get_object (ui, "automatic_login_radio"));
    login_delay_box = GTK_WIDGET (gtk_builder_get_object (ui, "login_delay_box"));
    login_delay_check = GTK_WIDGET (gtk_builder_get_object (ui, "login_delay_check"));
    sound_enable_check = GTK_WIDGET (gtk_builder_get_object (ui, "sound_enable_check"));
    face_browser_enable_check = GTK_WIDGET (gtk_builder_get_object (ui, "face_browser_enable_check"));

    if (g_file_test ("/usr/share/mdm/autostart/LoginWindow/libcanberra-ready-sound.desktop", G_FILE_TEST_EXISTS))
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sound_enable_check), get_sound_enabled ());
    else
        gtk_widget_hide (GTK_WIDGET (sound_enable_check));

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (face_browser_enable_check), get_face_browser_enabled ());
  
    gtk_builder_connect_signals (ui, NULL);

    /* Translators: Label for choosing which user to log in as. '%s' is replaced with an input field. */
    split_text (_("Log in as %s automatically"), "user_prefix_label", "user_suffix_label");
    /* Translators: Label for choosing the login delay. '%s' is replaced with an input field. */
    split_text (_("Allow %s seconds for anyone else to log in first"), "delay_prefix_label", "delay_suffix_label");
    /* Translators: Label for choosing the default session. '%s' is replaced with an input field. */
    split_text (_("Select %s as default session"), "session_prefix_label", "session_suffix_label");
    
    init_login_delay ();
    load_sessions_cb();

    renderer = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (user_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (user_combo), renderer, "text", 0);

    renderer = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (session_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (session_combo), renderer, "text", 0);

    user_manager = mdm_user_manager_ref_default ();
    g_signal_connect (user_manager, "users-loaded", G_CALLBACK (users_loaded_cb), NULL);
    g_signal_connect (user_manager, "user-added", G_CALLBACK (user_added_cb), NULL);
    
    gtk_widget_hide (user_entry);

    gtk_widget_set_sensitive (option_vbox, FALSE);
    gtk_widget_show (dialog);

    gtk_main ();
    
    return 0;
}
