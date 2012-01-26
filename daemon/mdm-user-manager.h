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

#ifndef __MDM_USER_MANAGER_H
#define __MDM_USER_MANAGER_H

#include <glib-object.h>

#include "mdm-user.h"

G_BEGIN_DECLS

#define MDM_TYPE_USER_MANAGER         (mdm_user_manager_get_type ())
#define MDM_USER_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), MDM_TYPE_USER_MANAGER, MdmUserManager))
#define MDM_USER_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), MDM_TYPE_USER_MANAGER, MdmUserManagerClass))
#define MDM_IS_USER_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), MDM_TYPE_USER_MANAGER))
#define MDM_IS_USER_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), MDM_TYPE_USER_MANAGER))
#define MDM_USER_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MDM_TYPE_USER_MANAGER, MdmUserManagerClass))

typedef struct MdmUserManagerPrivate MdmUserManagerPrivate;

typedef struct
{
        GObject                parent;
        MdmUserManagerPrivate *priv;
} MdmUserManager;

typedef struct
{
        GObjectClass   parent_class;

        void          (* loading_users)             (MdmUserManager *user_manager);
        void          (* users_loaded)              (MdmUserManager *user_manager);
        void          (* user_added)                (MdmUserManager *user_manager,
                                                     gint64          uid);
        void          (* user_removed)              (MdmUserManager *user_manager,
                                                     gint64          uid);
        void          (* user_updated)              (MdmUserManager *user_manager,
                                                     gint64          uid);
} MdmUserManagerClass;

typedef enum
{
        MDM_USER_MANAGER_ERROR_GENERAL,
        MDM_USER_MANAGER_ERROR_KEY_NOT_FOUND
} MdmUserManagerError;

#define MDM_USER_MANAGER_ERROR mdm_user_manager_error_quark ()

GQuark              mdm_user_manager_error_quark           (void);
GType               mdm_user_manager_get_type              (void);

MdmUserManager *    mdm_user_manager_ref_default           (void);

GSList *            mdm_user_manager_list_users            (MdmUserManager *manager);
MdmUser *           mdm_user_manager_get_user              (MdmUserManager *manager,
                                                            const char     *user_name);
MdmUser *           mdm_user_manager_get_user_by_uid       (MdmUserManager *manager,
                                                            uid_t           uid);

gboolean            mdm_user_manager_count_users           (MdmUserManager *user_manager,
                                                            gint           *user_count,
                                                            GError        **error);

gboolean            mdm_user_manager_get_user_list         (MdmUserManager *user_manager,
                                                            GArray        **user_list,
                                                            GError        **error);

gboolean            mdm_user_manager_get_user_info         (MdmUserManager *user_manager,
                                                            gint64          uid,
                                                            gchar         **user_name,
                                                            gchar         **real_name,
                                                            gchar         **shell,
                                                            gint           *login_count,
                                                            gchar         **icon_url,
                                                            GError        **error);

gboolean            mdm_user_manager_get_users_info        (MdmUserManager *user_manager,
                                                            GArray         *uids,
                                                            GPtrArray     **user_info,
                                                            GError        **error);

gboolean            mdm_user_manager_get_users_loaded      (MdmUserManager *user_manager,
                                                            gboolean       *is_loaded,
                                                            GError        **error);

G_END_DECLS

#endif /* __MDM_USER_MANAGER_H */
