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
#include <errno.h>
#include <pwd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "mdm-display-factory.h"
#include "mdm-local-display-factory.h"
#include "mdm-local-display-factory-glue.h"

#include "mdm-display-store.h"
#include "mdm-static-display.h"
#include "mdm-transient-display.h"
#include "mdm-static-factory-display.h"
#include "mdm-product-display.h"

#define MDM_LOCAL_DISPLAY_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MDM_TYPE_LOCAL_DISPLAY_FACTORY, MdmLocalDisplayFactoryPrivate))

#define CK_SEAT1_PATH                       "/org/freedesktop/ConsoleKit/Seat1"

#define MDM_DBUS_PATH                       "/org/mate/DisplayManager"
#define MDM_LOCAL_DISPLAY_FACTORY_DBUS_PATH MDM_DBUS_PATH "/LocalDisplayFactory"
#define MDM_MANAGER_DBUS_NAME               "org.mate.DisplayManager.LocalDisplayFactory"

#define GUEST_USERNAME                          "guest"

#define MAX_DISPLAY_FAILURES 5

struct MdmLocalDisplayFactoryPrivate
{
        DBusGConnection *connection;
        DBusGProxy      *proxy;
        GHashTable      *displays;

        /* FIXME: this needs to be per seat? */
        guint            num_failures;
};

enum {
        PROP_0,
};

static void     mdm_local_display_factory_class_init    (MdmLocalDisplayFactoryClass *klass);
static void     mdm_local_display_factory_init          (MdmLocalDisplayFactory      *factory);
static void     mdm_local_display_factory_finalize      (GObject                     *object);

static MdmDisplay *create_display                       (MdmLocalDisplayFactory      *factory);

static gpointer local_display_factory_object = NULL;

G_DEFINE_TYPE (MdmLocalDisplayFactory, mdm_local_display_factory, MDM_TYPE_DISPLAY_FACTORY)

GQuark
mdm_local_display_factory_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("mdm_local_display_factory_error");
        }

        return ret;
}

static void
listify_hash (gpointer    key,
              MdmDisplay *display,
              GList     **list)
{
        *list = g_list_prepend (*list, key);
}

static int
sort_nums (gpointer a,
           gpointer b)
{
        guint32 num_a;
        guint32 num_b;

        num_a = GPOINTER_TO_UINT (a);
        num_b = GPOINTER_TO_UINT (b);

        if (num_a > num_b) {
                return 1;
        } else if (num_a < num_b) {
                return -1;
        } else {
                return 0;
        }
}

static guint32
take_next_display_number (MdmLocalDisplayFactory *factory)
{
        GList  *list;
        GList  *l;
        guint32 ret;

        ret = 0;
        list = NULL;

        g_hash_table_foreach (factory->priv->displays, (GHFunc)listify_hash, &list);
        if (list == NULL) {
                goto out;
        }

        /* sort low to high */
        list = g_list_sort (list, (GCompareFunc)sort_nums);

        g_debug ("MdmLocalDisplayFactory: Found the following X displays:");
        for (l = list; l != NULL; l = l->next) {
                g_debug ("MdmLocalDisplayFactory: %u", GPOINTER_TO_UINT (l->data));
        }

        for (l = list; l != NULL; l = l->next) {
                guint32 num;
                num = GPOINTER_TO_UINT (l->data);

                /* always fill zero */
                if (l->prev == NULL && num != 0) {
                        ret = 0;
                        break;
                }
                /* now find the first hole */
                if (l->next == NULL || GPOINTER_TO_UINT (l->next->data) != (num + 1)) {
                        ret = num + 1;
                        break;
                }
        }
 out:

        /* now reserve this number */
        g_debug ("MdmLocalDisplayFactory: Reserving X display: %u", ret);
        g_hash_table_insert (factory->priv->displays, GUINT_TO_POINTER (ret), NULL);

        return ret;
}

static void
on_display_disposed (MdmLocalDisplayFactory *factory,
                     MdmDisplay             *display)
{
        g_debug ("MdmLocalDisplayFactory: Display %p disposed", display);
}

static void
store_display (MdmLocalDisplayFactory *factory,
               guint32                 num,
               MdmDisplay             *display)
{
        MdmDisplayStore *store;

        g_object_weak_ref (G_OBJECT (display), (GWeakNotify)on_display_disposed, factory);

        store = mdm_display_factory_get_display_store (MDM_DISPLAY_FACTORY (factory));
        mdm_display_store_add (store, display);

        /* now fill our reserved spot */
        g_hash_table_insert (factory->priv->displays, GUINT_TO_POINTER (num), NULL);
}

/*
  Example:
  dbus-send --system --dest=org.mate.DisplayManager \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/mate/DisplayManager/Manager \
  org.mate.DisplayManager.Manager.GetDisplays
*/
gboolean
mdm_local_display_factory_create_transient_display (MdmLocalDisplayFactory *factory,
                                                    char                  **id,
                                                    GError                **error)
{
        gboolean         ret;
        MdmDisplay      *display;
        guint32          num;

        g_return_val_if_fail (MDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        ret = FALSE;

        num = take_next_display_number (factory);

        g_debug ("MdmLocalDisplayFactory: Creating transient display %d", num);

        display = mdm_transient_display_new (num);

        /* FIXME: don't hardcode seat1? */
        g_object_set (display, "seat-id", CK_SEAT1_PATH, NULL);

        store_display (factory, num, display);

        if (! mdm_display_manage (display)) {
                display = NULL;
                goto out;
        }

        if (! mdm_display_get_id (display, id, NULL)) {
                display = NULL;
                goto out;
        }

        ret = TRUE;
 out:
        /* ref either held by store or not at all */
        g_object_unref (display);

        return ret;
}

/* MdmGuestDisplay */

typedef struct
{
        MdmTransientDisplayClass   parent_class;
} MdmGuestDisplayClass;

typedef struct
{
        MdmTransientDisplay        parent;
        MdmTransientDisplayPrivate *priv;
} MdmGuestDisplay;

#define MDM_TYPE_GUEST_DISPLAY         (mdm_guest_display_get_type ())
#define MDM_GUEST_DISPLAY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MDM_TYPE_GUEST_DISPLAY, MdmGuestDisplayClass))
GType           mdm_guest_display_get_type     (void);
static void     mdm_guest_display_class_init   (MdmGuestDisplayClass *klass);
static void     mdm_guest_display_init         (MdmGuestDisplay      *display) {}
MdmDisplay *    mdm_guest_display_new          (int display_number);
static gboolean mdm_guest_display_finish       (MdmDisplay           *display);

G_DEFINE_TYPE (MdmGuestDisplay, mdm_guest_display, MDM_TYPE_TRANSIENT_DISPLAY);

/* override timed_login_details for guest session */
static void
mdm_guest_display_get_timed_login_details (MdmDisplay *display,
                                           gboolean   *enabledp,
                                           char      **usernamep,
                                           int        *delayp)
{
	g_debug ("MdmLocalDisplayFactory: Getting guest timed login details");
	*enabledp = TRUE;
	*usernamep = g_strdup(GUEST_USERNAME);
	*delayp = 0;
}

static void
mdm_guest_display_class_init (MdmGuestDisplayClass *klass)
{
        MdmDisplayClass *display_class = MDM_DISPLAY_CLASS (klass);

        display_class->get_timed_login_details = mdm_guest_display_get_timed_login_details;
        display_class->finish = mdm_guest_display_finish;
}

MdmDisplay *
mdm_guest_display_new (int display_number)
{
        GObject *object;
        char    *x11_display;

        x11_display = g_strdup_printf (":%d", display_number);
        object = g_object_new (MDM_TYPE_GUEST_DISPLAY,
                               "x11-display-number", display_number,
                               "x11-display-name", x11_display,
                               NULL);
        g_free (x11_display);

        return MDM_DISPLAY (object);
}

static
gboolean
mdm_guest_display_finish (MdmDisplay *display)
{
        GError *err = NULL;
        gboolean result;
        gint status;
        struct sigaction dfl, old_act;
        const char* argv[] = {
            "/usr/share/mdm/guest-session/guest-session-cleanup.sh",
            GUEST_USERNAME, NULL};

        /* temporarily reset SIGCHLD, we need it for g_spawn_sync */
        dfl.sa_handler = SIG_DFL;
        dfl.sa_flags = SA_RESTART|SA_NOCLDSTOP;
        sigemptyset (&dfl.sa_mask);
        g_assert (sigaction (SIGCHLD, &dfl, &old_act) == 0);

        /* destroy guest user again */
        result = g_spawn_sync ("/", (gchar**) argv, NULL, 0, NULL, NULL, NULL,
                NULL, &status, &err);

        g_assert (sigaction (SIGCHLD, &old_act, NULL) == 0);

        if (!result) {
                g_warning ("mdm_guest_display_finish: Calling '%s %s' failed: %s", argv[0],
                           argv[1], err->message);
                g_error_free (err);
        }

        return MDM_DISPLAY_CLASS (mdm_guest_display_parent_class)->finish (display);
}

/* End MdmGuestDisplay */

static gboolean
mdm_local_display_factory_setup_guest_account (const char *current_user_session)
{
        GError *err = NULL;
        gboolean result;
        gchar *sout, *serr;
        char *username;
        gint status;
        int len;
        struct sigaction dfl, old_act;
        const char* argv[] = {
            "/usr/share/mdm/guest-session/guest-session-setup.sh",
            current_user_session, NULL, NULL}; /* leave enough room for a second argument */
        
        g_debug ("mdm_local_display_factory_setup_guest_account: Calling guest-session-setup.sh %s", current_user_session);
        
        /* temporarily reset SIGCHLD, we need it for g_spawn_sync */
        dfl.sa_handler = SIG_DFL;
        dfl.sa_flags = SA_RESTART|SA_NOCLDSTOP;
        sigemptyset (&dfl.sa_mask);
        if (sigaction (SIGCHLD, &dfl, &old_act) < 0) {
            g_warning("mdm_local_display_factory_setup_guest_account: failure to temporarily restore SIGCHLD: %s",
                strerror(errno));
                return FALSE;
        }
        
        /* call guest setup script */
        result = g_spawn_sync ("/", (gchar**) argv, NULL, 0, NULL, NULL, &sout,
                               &serr, &status, &err);
        g_assert (sigaction (SIGCHLD, &old_act, NULL) == 0);
        if (!result) {
                g_warning ("mdm_local_display_factory_setup_guest_account: Calling %s failed: %s", argv[0],
                           err->message);
                g_error_free (err);
                return FALSE;
        }
        if (status != 0) {
                g_warning ("mdm_local_display_factory_setup_guest_account: %s failed with status %i:\n%s\n%s",
                           argv[0], status, sout, serr);
                g_free(sout);
                g_free(serr);
                return FALSE;
        }
        g_free (serr);
        
        /* extract user name from stdout */
        len = strlen (sout);
        if (sout[len-1] == '\n')
                sout[len-1] = 0;
        username = strrchr (sout, '\n');
        if (!username || strcmp (username + 1, GUEST_USERNAME)) {
                g_warning ("mdm_local_display_factory_setup_guest_account: no output, last line of stdout must have username; or username is not 'guest'");
                g_free (sout);
                return FALSE;
        }
        g_debug ("mdm_local_display_factory_setup_guest_account: %s succeeded, username: '%s'", argv[0], username+1);
        /* if we ever need to pass it to outside: */
        /* username = g_strdup (username + 1); */
        g_free (sout);
        
        return TRUE;
}

static gboolean
switch_to_guest_display (MdmLocalDisplayFactory *factory)
{
        struct passwd *password;
        DBusGProxy *proxy;
        GPtrArray *sessions = NULL;
        GError *error = NULL;
        gboolean result = FALSE;

        password = getpwnam (GUEST_USERNAME);
        if (!password) {
                return FALSE;
        }

        proxy = dbus_g_proxy_new_for_name (factory->priv->connection,
                                           "org.freedesktop.ConsoleKit",
                                           "/org/freedesktop/ConsoleKit/Manager",
                                           "org.freedesktop.ConsoleKit.Manager");
    
        dbus_g_proxy_call (proxy, "GetSessionsForUnixUser", &error,
                           G_TYPE_UINT, password->pw_uid, G_TYPE_INVALID,
                           dbus_g_type_get_collection("GPtrArray", DBUS_TYPE_G_OBJECT_PATH), &sessions, G_TYPE_INVALID);
        g_object_unref(proxy);
        if (error != NULL) {
                g_warning ("Error getting guest sessions: %s", error->message);
                g_error_free (error);
        }
    
        if (sessions && sessions->len > 0) {
                gchar *session_id = sessions->pdata[0];

                g_debug ("MdmLocalDisplayFactory: Switching to guest session %s", session_id);

                proxy = dbus_g_proxy_new_for_name (factory->priv->connection,
                                                   "org.freedesktop.ConsoleKit",
                                                   session_id,
                                                   "org.freedesktop.ConsoleKit.Session");
                result = dbus_g_proxy_call (proxy, "Activate", &error, G_TYPE_INVALID, G_TYPE_INVALID);
                g_object_unref (proxy);
                if (error != NULL)
                {
                        g_warning ("Error activating guest session: %s", error->message);
                        g_error_free (error);
                }
        }
        
        if (sessions != NULL) {
                gint i;
                for (i = 0; i < sessions->len; i++) {
                        g_free (sessions->pdata[i]);
                }
                g_ptr_array_free (sessions, TRUE);
        }

        return result;
}

gboolean
mdm_local_display_factory_start_guest_session (MdmLocalDisplayFactory *factory,
                                               const char             *current_user_session,
                                               char                  **id,
                                               GError                **error)
{
        MdmDisplay      *display = NULL;
        guint32          num;

        g_return_val_if_fail (MDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        /* Switch to existing guest display */
        if (switch_to_guest_display (factory)) {
                /* FIXME: How to return the ID of the guest display?  It should
                 *        be easy but I can't find how to get it */
                /*if (id != NULL) {
                        *id = g_strdup ("FIXME");
                }*/

                /* FIXME: We should return TRUE here but this causes MDM to go
                 *        crazy.  Luckily we can return FALSE as we don't need
                 *        any values returned from this call */
                return FALSE;
        }

        if (!mdm_local_display_factory_setup_guest_account(current_user_session)) {
                return FALSE;
        }

        num = take_next_display_number (factory);

        g_debug ("MdmLocalDisplayFactory: Starting Guest %s Session %d", current_user_session, num);

        display = mdm_guest_display_new (num);

        /* FIXME: don't hardcode seat1? */
        g_object_set (display, "seat-id", CK_SEAT1_PATH, NULL);

        store_display (factory, num, display);

        if (! mdm_display_manage (display) || ! mdm_display_get_id (display, id, NULL)) {
                return FALSE;
        } else {
                g_object_unref (display);
                return TRUE;
        }
}

static gboolean
switch_to_user_display (MdmLocalDisplayFactory *factory, char *username)
{
        struct passwd *password;
        DBusGProxy *proxy;
        GPtrArray *sessions = NULL;
        GError *error = NULL;
        gboolean result = FALSE;

        password = getpwnam (username);
        if (!password) {
                return FALSE;
        }

        proxy = dbus_g_proxy_new_for_name (factory->priv->connection,
                                           "org.freedesktop.ConsoleKit",
                                           "/org/freedesktop/ConsoleKit/Manager",
                                           "org.freedesktop.ConsoleKit.Manager");
    
        dbus_g_proxy_call (proxy, "GetSessionsForUnixUser", &error,
                           G_TYPE_UINT, password->pw_uid, G_TYPE_INVALID,
                           dbus_g_type_get_collection("GPtrArray", DBUS_TYPE_G_OBJECT_PATH), &sessions, G_TYPE_INVALID);
        g_object_unref(proxy);
        if (error != NULL) {
                g_warning ("Error getting sessions for user %s: %s", username, error->message);
                g_error_free (error);
        }
    
        if (sessions && sessions->len > 0) {
                gchar *session_id = sessions->pdata[0];

                g_debug ("MdmLocalDisplayFactory: Switching to session %s (user %s)", session_id, username);

                proxy = dbus_g_proxy_new_for_name (factory->priv->connection,
                                                   "org.freedesktop.ConsoleKit",
                                                   session_id,
                                                   "org.freedesktop.ConsoleKit.Session");
                result = dbus_g_proxy_call (proxy, "Activate", &error, G_TYPE_INVALID, G_TYPE_INVALID);
                g_object_unref (proxy);
                if (error != NULL)
                {
                        g_warning ("Error activating session for user %s: %s", username, error->message);
                        g_error_free (error);
                }
        }
        
        if (sessions != NULL) {
                gint i;
                for (i = 0; i < sessions->len; i++) {
                        g_free (sessions->pdata[i]);
                }
                g_ptr_array_free (sessions, TRUE);
        }

        return result;
}

gboolean
mdm_local_display_factory_switch_to_user (MdmLocalDisplayFactory *factory,
                                          char                   *username,
                                          char                  **id,
                                          GError                **error)
{
        gboolean         ret;
        MdmDisplay      *display;
        guint32          num;

        g_return_val_if_fail (MDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        /* Switch to existing user display */
        if (switch_to_user_display (factory, username)) {
                /* FIXME: How to return the ID of the user display?  It should
                 *        be easy but I can't find how to get it */
                /*if (id != NULL) {
                        *id = g_strdup ("FIXME");
                }*/

                /* FIXME: We should return TRUE here but this causes MDM to go
                 *        crazy.  Luckily we can return FALSE as we don't need
                 *        any values returned from this call */
                return FALSE;
        }

        ret = FALSE;

        num = take_next_display_number (factory);

        g_debug ("MdmLocalDisplayFactory: Switching to user %s on display %d", username, num);

        display = mdm_transient_display_new (num);

        /* FIXME: don't hardcode seat1? */
        g_object_set (display, "seat-id", CK_SEAT1_PATH, NULL);
        g_object_set (display, "username", username, NULL);

        store_display (factory, num, display);

        if (! mdm_display_manage (display)) {
                display = NULL;
                goto out;
        }

        if (! mdm_display_get_id (display, id, NULL)) {
                display = NULL;
                goto out;
        }

        ret = TRUE;
 out:
        /* ref either held by store or not at all */
        g_object_unref (display);

        return ret;
}

gboolean
mdm_local_display_factory_create_product_display (MdmLocalDisplayFactory *factory,
                                                  const char             *parent_display_id,
                                                  const char             *relay_address,
                                                  char                  **id,
                                                  GError                **error)
{
        gboolean    ret;
        MdmDisplay *display;
        guint32     num;

        g_return_val_if_fail (MDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        ret = FALSE;

        g_debug ("MdmLocalDisplayFactory: Creating product display parent %s address:%s",
                 parent_display_id, relay_address);

        num = take_next_display_number (factory);

        g_debug ("MdmLocalDisplayFactory: got display num %u", num);

        display = mdm_product_display_new (num, relay_address);

        /* FIXME: don't hardcode seat1? */
        g_object_set (display, "seat-id", CK_SEAT1_PATH, NULL);

        store_display (factory, num, display);

        if (! mdm_display_manage (display)) {
                display = NULL;
                goto out;
        }

        if (! mdm_display_get_id (display, id, NULL)) {
                display = NULL;
                goto out;
        }

        ret = TRUE;
 out:
        /* ref either held by store or not at all */
        g_object_unref (display);

        return ret;
}

static void
on_static_display_status_changed (MdmDisplay             *display,
                                  GParamSpec             *arg1,
                                  MdmLocalDisplayFactory *factory)
{
        int              status;
        MdmDisplayStore *store;
        int              num;

        num = -1;
        mdm_display_get_x11_display_number (display, &num, NULL);
        g_assert (num != -1);

        store = mdm_display_factory_get_display_store (MDM_DISPLAY_FACTORY (factory));

        status = mdm_display_get_status (display);

        g_debug ("MdmLocalDisplayFactory: static display status changed: %d", status);
        switch (status) {
        case MDM_DISPLAY_FINISHED:
                /* remove the display number from factory->priv->displays
                   so that it may be reused */
                g_hash_table_remove (factory->priv->displays, GUINT_TO_POINTER (num));
                mdm_display_store_remove (store, display);
                /* reset num failures */
                factory->priv->num_failures = 0;
                create_display (factory);
                break;
        case MDM_DISPLAY_FAILED:
                /* leave the display number in factory->priv->displays
                   so that it doesn't get reused */
                mdm_display_store_remove (store, display);
                factory->priv->num_failures++;
                if (factory->priv->num_failures > MAX_DISPLAY_FAILURES) {
                        /* oh shit */
                        g_warning ("MdmLocalDisplayFactory: maximum number of X display failures reached: check X server log for errors");
                        /* FIXME: should monitor hardware changes to
                           try again when seats change */
                } else {
                        create_display (factory);
                }
                break;
        case MDM_DISPLAY_UNMANAGED:
                break;
        case MDM_DISPLAY_PREPARED:
                break;
        case MDM_DISPLAY_MANAGED:
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static MdmDisplay *
create_display (MdmLocalDisplayFactory *factory)
{
        MdmDisplay *display;
        guint32     num;

        num = take_next_display_number (factory);

#if 0
        display = mdm_static_factory_display_new (num);
#else
        display = mdm_static_display_new (num);
#endif
        if (display == NULL) {
                g_warning ("Unable to create display: %d", num);
                return NULL;
        }

        /* FIXME: don't hardcode seat1? */
        g_object_set (display, "seat-id", CK_SEAT1_PATH, NULL);

        g_signal_connect (display,
                          "notify::status",
                          G_CALLBACK (on_static_display_status_changed),
                          factory);

        store_display (factory, num, display);

        /* let store own the ref */
        g_object_unref (display);

        if (! mdm_display_manage (display)) {
                mdm_display_unmanage (display);
        }

        return display;
}

static gboolean
mdm_local_display_factory_start (MdmDisplayFactory *base_factory)
{
        gboolean                ret;
        MdmLocalDisplayFactory *factory = MDM_LOCAL_DISPLAY_FACTORY (base_factory);
        MdmDisplay             *display;

        g_return_val_if_fail (MDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        ret = TRUE;

        /* FIXME: use seat configuration */
        display = create_display (factory);
        if (display == NULL) {
                ret = FALSE;
        }

        return ret;
}

static gboolean
mdm_local_display_factory_stop (MdmDisplayFactory *base_factory)
{
        MdmLocalDisplayFactory *factory = MDM_LOCAL_DISPLAY_FACTORY (base_factory);

        g_return_val_if_fail (MDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        return TRUE;
}

static void
mdm_local_display_factory_set_property (GObject       *object,
                                        guint          prop_id,
                                        const GValue  *value,
                                        GParamSpec    *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
mdm_local_display_factory_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
register_factory (MdmLocalDisplayFactory *factory)
{
        GError *error = NULL;

        error = NULL;
        factory->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (factory->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        dbus_g_connection_register_g_object (factory->priv->connection, MDM_LOCAL_DISPLAY_FACTORY_DBUS_PATH, G_OBJECT (factory));

        return TRUE;
}

static GObject *
mdm_local_display_factory_constructor (GType                  type,
                                       guint                  n_construct_properties,
                                       GObjectConstructParam *construct_properties)
{
        MdmLocalDisplayFactory      *factory;
        gboolean                     res;

        factory = MDM_LOCAL_DISPLAY_FACTORY (G_OBJECT_CLASS (mdm_local_display_factory_parent_class)->constructor (type,
                                                                                                                   n_construct_properties,
                                                                                                                   construct_properties));

        res = register_factory (factory);
        if (! res) {
                g_warning ("Unable to register local display factory with system bus");
        }

        return G_OBJECT (factory);
}

static void
mdm_local_display_factory_class_init (MdmLocalDisplayFactoryClass *klass)
{
        GObjectClass           *object_class = G_OBJECT_CLASS (klass);
        MdmDisplayFactoryClass *factory_class = MDM_DISPLAY_FACTORY_CLASS (klass);

        object_class->get_property = mdm_local_display_factory_get_property;
        object_class->set_property = mdm_local_display_factory_set_property;
        object_class->finalize = mdm_local_display_factory_finalize;
        object_class->constructor = mdm_local_display_factory_constructor;

        factory_class->start = mdm_local_display_factory_start;
        factory_class->stop = mdm_local_display_factory_stop;

        g_type_class_add_private (klass, sizeof (MdmLocalDisplayFactoryPrivate));

        dbus_g_object_type_install_info (MDM_TYPE_LOCAL_DISPLAY_FACTORY, &dbus_glib_mdm_local_display_factory_object_info);
}

static void
mdm_local_display_factory_init (MdmLocalDisplayFactory *factory)
{
        factory->priv = MDM_LOCAL_DISPLAY_FACTORY_GET_PRIVATE (factory);

        factory->priv->displays = g_hash_table_new (NULL, NULL);
}

static void
mdm_local_display_factory_finalize (GObject *object)
{
        MdmLocalDisplayFactory *factory;

        g_return_if_fail (object != NULL);
        g_return_if_fail (MDM_IS_LOCAL_DISPLAY_FACTORY (object));

        factory = MDM_LOCAL_DISPLAY_FACTORY (object);

        g_return_if_fail (factory->priv != NULL);

        g_hash_table_destroy (factory->priv->displays);

        G_OBJECT_CLASS (mdm_local_display_factory_parent_class)->finalize (object);
}

MdmLocalDisplayFactory *
mdm_local_display_factory_new (MdmDisplayStore *store)
{
        if (local_display_factory_object != NULL) {
                g_object_ref (local_display_factory_object);
        } else {
                local_display_factory_object = g_object_new (MDM_TYPE_LOCAL_DISPLAY_FACTORY,
                                                             "display-store", store,
                                                             NULL);
                g_object_add_weak_pointer (local_display_factory_object,
                                           (gpointer *) &local_display_factory_object);
        }

        return MDM_LOCAL_DISPLAY_FACTORY (local_display_factory_object);
}
