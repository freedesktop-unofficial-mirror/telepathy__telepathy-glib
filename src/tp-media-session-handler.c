/*
 * tp-media-session-handler.c - Source for TpMediaSessionHandler
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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
 */

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "tp-media-session-handler.h"
#include "tp-media-session-handler-signals-marshal.h"

#include "tp-media-session-handler-glue.h"

G_DEFINE_TYPE(TpMediaSessionHandler, tp_media_session_handler, G_TYPE_OBJECT)

/* signal enum */
enum
{
    NEW_MEDIA_STREAM_HANDLER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _TpMediaSessionHandlerPrivate TpMediaSessionHandlerPrivate;

struct _TpMediaSessionHandlerPrivate
{
  gboolean dispose_has_run;
};

#define TP_MEDIA_SESSION_HANDLER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), TP_TYPE_MEDIA_SESSION_HANDLER, TpMediaSessionHandlerPrivate))

static void
tp_media_session_handler_init (TpMediaSessionHandler *obj)
{
  TpMediaSessionHandlerPrivate *priv = TP_MEDIA_SESSION_HANDLER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void tp_media_session_handler_dispose (GObject *object);
static void tp_media_session_handler_finalize (GObject *object);

static void
tp_media_session_handler_class_init (TpMediaSessionHandlerClass *tp_media_session_handler_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (tp_media_session_handler_class);

  g_type_class_add_private (tp_media_session_handler_class, sizeof (TpMediaSessionHandlerPrivate));

  object_class->dispose = tp_media_session_handler_dispose;
  object_class->finalize = tp_media_session_handler_finalize;

  signals[NEW_MEDIA_STREAM_HANDLER] =
    g_signal_new ("new-media-stream-handler",
                  G_OBJECT_CLASS_TYPE (tp_media_session_handler_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  tp_media_session_handler_marshal_VOID__STRING_INT_INT,
                  G_TYPE_NONE, 3, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INT, G_TYPE_INT);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (tp_media_session_handler_class), &dbus_glib_tp_media_session_handler_object_info);
}

void
tp_media_session_handler_dispose (GObject *object)
{
  TpMediaSessionHandler *self = TP_MEDIA_SESSION_HANDLER (object);
  TpMediaSessionHandlerPrivate *priv = TP_MEDIA_SESSION_HANDLER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (tp_media_session_handler_parent_class)->dispose)
    G_OBJECT_CLASS (tp_media_session_handler_parent_class)->dispose (object);
}

void
tp_media_session_handler_finalize (GObject *object)
{
  TpMediaSessionHandler *self = TP_MEDIA_SESSION_HANDLER (object);
  TpMediaSessionHandlerPrivate *priv = TP_MEDIA_SESSION_HANDLER_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (tp_media_session_handler_parent_class)->finalize (object);
}



/**
 * tp_media_session_handler_error
 *
 * Implements DBus method Error
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean tp_media_session_handler_error (TpMediaSessionHandler *obj, gint errno, const gchar * message, GError **error)
{
  return TRUE;
}


/**
 * tp_media_session_handler_introspect
 *
 * Implements DBus method Introspect
 * on interface org.freedesktop.DBus.Introspectable
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean tp_media_session_handler_introspect (TpMediaSessionHandler *obj, gchar ** ret, GError **error)
{
  return TRUE;
}

