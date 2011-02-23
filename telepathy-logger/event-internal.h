/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Cosimo Alfarano <cosimo.alfarano@collabora.co.uk>
 */

#ifndef __TPL_EVENT_INTERNAL_H__
#define __TPL_EVENT_INTERNAL_H__

#include <telepathy-logger/event.h>

G_BEGIN_DECLS

struct _TplEvent
{
  GObject parent;

  /* Private */
  TplEventPriv *priv;
};

struct _TplEventClass {
  GObjectClass parent_class;

  /* by default log_id is compared, can be overrided */
  gboolean (*equal) (TplEvent *event1, TplEvent *event2);
};

const gchar * _tpl_event_get_log_id (TplEvent *self);

TplEntity * _tpl_event_get_target (TplEvent *self);

const gchar * _tpl_event_get_target_id (TplEvent * self);

gboolean _tpl_event_target_is_room (TplEvent *self);

const gchar * _tpl_event_get_channel_path (TplEvent *self);

G_END_DECLS
#endif // __TPL_EVENT_INTERNAL_H__