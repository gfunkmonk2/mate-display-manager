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

/*
 * Facade object for user data, owned by MdmUserManager
 */

#ifndef __MDM_USER__
#define __MDM_USER__ 1

#include <sys/types.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

#define MDM_TYPE_USER (mdm_user_get_type ())
#define MDM_USER(object) (G_TYPE_CHECK_INSTANCE_CAST ((object), MDM_TYPE_USER, MdmUser))
#define MDM_IS_USER(object) (G_TYPE_CHECK_INSTANCE_TYPE ((object), MDM_TYPE_USER))

typedef struct _MdmUser MdmUser;

GType                 mdm_user_get_type            (void) G_GNUC_CONST;

uid_t                 mdm_user_get_uid             (MdmUser   *user);
const char  *mdm_user_get_user_name       (MdmUser   *user);
const char  *mdm_user_get_real_name       (MdmUser   *user);
const char  *mdm_user_get_home_directory  (MdmUser   *user);
const char  *mdm_user_get_shell           (MdmUser   *user);
guint                 mdm_user_get_num_sessions    (MdmUser   *user);
GList                *mdm_user_get_sessions        (MdmUser   *user);
gulong                mdm_user_get_login_frequency (MdmUser   *user);

GdkPixbuf            *mdm_user_render_icon         (MdmUser   *user,
                                                    gint       icon_size);

gint                  mdm_user_collate             (MdmUser   *user1,
                                                    MdmUser   *user2);

G_END_DECLS

#endif
