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


#ifndef __MDM_SETTINGS_H
#define __MDM_SETTINGS_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#define MDM_TYPE_SETTINGS         (mdm_settings_get_type ())
#define MDM_SETTINGS(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MDM_TYPE_SETTINGS, MdmSettings))
#define MDM_SETTINGS_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MDM_TYPE_SETTINGS, MdmSettingsClass))
#define MDM_IS_SETTINGS(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MDM_TYPE_SETTINGS))
#define MDM_IS_SETTINGS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MDM_TYPE_SETTINGS))
#define MDM_SETTINGS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MDM_TYPE_SETTINGS, MdmSettingsClass))

typedef struct MdmSettingsPrivate MdmSettingsPrivate;

typedef struct
{
        GObject             parent;
        MdmSettingsPrivate *priv;
} MdmSettings;

typedef struct
{
        GObjectClass   parent_class;

        void          (* value_changed)    (MdmSettings *settings,
                                            const char  *key,
                                            const char  *old_value,
                                            const char **new_value);
} MdmSettingsClass;

typedef enum
{
        MDM_SETTINGS_ERROR_GENERAL,
        MDM_SETTINGS_ERROR_KEY_NOT_FOUND
} MdmSettingsError;

#define MDM_SETTINGS_ERROR mdm_settings_error_quark ()

GQuark              mdm_settings_error_quark                    (void);
GType               mdm_settings_get_type                       (void);

MdmSettings *       mdm_settings_new                            (void);

/* exported */

gboolean            mdm_settings_get_value                      (MdmSettings *settings,
                                                                 const char  *key,
                                                                 char       **value,
                                                                 GError     **error);
gboolean            mdm_settings_get_sound_enabled              (MdmSettings *settings,
                                                                 gboolean    *value,
                                                                 GError     **error);
gboolean            mdm_settings_get_face_browser_enabled       (MdmSettings *settings,
                                                                 gboolean    *value,
                                                                 GError     **error);
gboolean            mdm_settings_unlock                         (MdmSettings *settings,
                                                                 DBusGMethodInvocation *context);
gboolean            mdm_settings_set_value                      (MdmSettings *settings,
                                                                 const char  *key,
                                                                 const char  *value,
                                                                 DBusGMethodInvocation *context);
gboolean            mdm_settings_set_sound_enabled              (MdmSettings *settings,
                                                                 gboolean     value,
                                                                 DBusGMethodInvocation *context);
gboolean            mdm_settings_set_face_browser_enabled       (MdmSettings *settings,
                                                                 gboolean     value,
                                                                 DBusGMethodInvocation *context);

G_END_DECLS

#endif /* __MDM_SETTINGS_H */
