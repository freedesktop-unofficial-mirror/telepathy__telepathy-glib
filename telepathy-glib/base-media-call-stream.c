/*
 * base-media-call-stream.c - Source for TpBaseMediaCallStream
 * Copyright (C) 2009-2011 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 * @author Jonny Lamb <jonny.lamb@collabora.co.uk>
 * @author David Laban <david.laban@collabora.co.uk>
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

/**
 * SECTION:base-media-call-stream
 * @title: TpBaseMediaCallStream
 * @short_description: base class for #TpSvcCallStreamInterfaceMedia
 *  implementations
 * @see_also: #TpSvcCallStreamInterfaceMedia, #TpBaseCallChannel,
 *  #TpBaseCallStream and #TpBaseCallContent
 *
 * This base class makes it easier to write #TpSvcCallStreamInterfaceMedia
 * implementations by implementing some of its properties and methods.
 *
 * Subclasses must still implement #TpBaseCallStream's virtual methods plus
 * #TpBaseMediaCallStreamClass.add_local_candidates and
 * #TpBaseMediaCallStreamClass.finish_initial_candidates.
 *
 * Since: 0.UNRELEASED
 */

/**
 * TpBaseMediaCallStream:
 *
 * A base class for media call stream implementations
 *
 * Since: 0.UNRELEASED
 */

/**
 * TpBaseMediaCallStreamClass:
 * @add_local_candidates: called when new candidates are added
 * @finish_initial_candidates: called when the initial batch of candidates has
 *  been added, and should now be processed/sent to the remote side
 *
 * The class structure for #TpBaseMediaCallStream
 *
 * Since: 0.UNRELEASED
 */

/**
 * TpBaseMediaCallStreamVoidFunc:
 * @self: a #TpBaseMediaCallStream
 *
 * Signature of an implementation of
 * #TpBaseMediaCallStreamClass.finish_initial_candidates.
 *
 * Since: 0.UNRELEASED
 */

/**
 * TpBaseMediaCallStreamAddCandidatesFunc:
 * @self: a #TpBaseMediaCallStream
 * @candidates: a #GPtrArray of #GValueArray containing candidates info
 * @error: a #GError to fill
 *
 * Signature of an implementation of
 * #TpBaseMediaCallStreamClass.add_local_candidates.
 *
 * Implementation should validate the added @candidates and return a subset
 * (or all) of them that are accepted. Implementation should return a new
 * #GPtrArray build in a way that g_ptr_array_unref() is enough to free all its
 * memory. It is fine to just add element pointers from @candidates to the
 * returned #GPtrArray without deep-copy them.
 *
 * Since: 0.UNRELEASED
 */

#include "config.h"
#include "base-media-call-stream.h"

#define DEBUG_FLAG TP_DEBUG_CALL
#include "telepathy-glib/base-connection.h"
#include "telepathy-glib/call-stream-endpoint.h"
#include "telepathy-glib/dbus.h"
#include "telepathy-glib/debug-internal.h"
#include "telepathy-glib/enums.h"
#include "telepathy-glib/gtypes.h"
#include "telepathy-glib/interfaces.h"
#include "telepathy-glib/svc-properties-interface.h"
#include "telepathy-glib/svc-call.h"
#include "telepathy-glib/util.h"

static void call_stream_media_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(TpBaseMediaCallStream, tp_base_media_call_stream,
   TP_TYPE_BASE_CALL_STREAM,
   G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CALL_STREAM_INTERFACE_MEDIA,
    call_stream_media_iface_init);
  );

static const gchar *tp_base_media_call_stream_interfaces[] = {
    TP_IFACE_CALL_STREAM_INTERFACE_MEDIA,
    NULL
};

/* properties */
enum
{
  PROP_SENDING_STATE = 1,
  PROP_RECEIVING_STATE,
  PROP_TRANSPORT,
  PROP_LOCAL_CANDIDATES,
  PROP_LOCAL_CREDENTIALS,
  PROP_STUN_SERVERS,
  PROP_RELAY_INFO,
  PROP_HAS_SERVER_INFO,
  PROP_ENDPOINTS,
  PROP_ICE_RESTART_PENDING
};

/* private structure */
struct _TpBaseMediaCallStreamPrivate
{
  TpStreamFlowState sending_state;
  TpStreamFlowState receiving_state;
  TpStreamTransportType transport;
  /* GPtrArray of owned GValueArray (dbus struct) */
  GPtrArray *local_candidates;
  gchar *username;
  gchar *password;
  /* GPtrArray of owned GValueArray (dbus struct) */
  GPtrArray *stun_servers;
  /* GPtrArray of reffed GHashTable (asv) */
  GPtrArray *relay_info;
  gboolean has_server_info;
  /* GList of reffed TpCallStreamEndpoint */
  GList *endpoints;
  gboolean ice_restart_pending;
};

static void
tp_base_media_call_stream_init (TpBaseMediaCallStream *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      TP_TYPE_BASE_MEDIA_CALL_STREAM, TpBaseMediaCallStreamPrivate);

  self->priv->local_candidates = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_value_array_free);
}

static void
endpoints_list_destroy (GList *endpoints)
{
  g_list_free_full (endpoints, g_object_unref);
}

static void
tp_base_media_call_stream_dispose (GObject *object)
{
  TpBaseMediaCallStream *self = TP_BASE_MEDIA_CALL_STREAM (object);

  tp_clear_pointer (&self->priv->endpoints, endpoints_list_destroy);

  if (G_OBJECT_CLASS (tp_base_media_call_stream_parent_class)->dispose)
    G_OBJECT_CLASS (tp_base_media_call_stream_parent_class)->dispose (object);
}

static void
tp_base_media_call_stream_finalize (GObject *object)
{
  TpBaseMediaCallStream *self = TP_BASE_MEDIA_CALL_STREAM (object);

  tp_clear_pointer (&self->priv->local_candidates, g_ptr_array_unref);
  tp_clear_pointer (&self->priv->stun_servers, g_ptr_array_unref);
  tp_clear_pointer (&self->priv->relay_info, g_ptr_array_unref);
  tp_clear_pointer (&self->priv->username, g_free);
  tp_clear_pointer (&self->priv->password, g_free);

  G_OBJECT_CLASS (tp_base_media_call_stream_parent_class)->finalize (object);
}

static void
tp_base_media_call_stream_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpBaseMediaCallStream *self = TP_BASE_MEDIA_CALL_STREAM (object);

  switch (property_id)
    {
      case PROP_SENDING_STATE:
        g_value_set_uint (value, self->priv->sending_state);
        break;
      case PROP_RECEIVING_STATE:
        g_value_set_uint (value, self->priv->receiving_state);
        break;
      case PROP_TRANSPORT:
        g_value_set_uint (value, self->priv->transport);
        break;
      case PROP_LOCAL_CANDIDATES:
        g_value_set_boxed (value, self->priv->local_candidates);
        break;
      case PROP_LOCAL_CREDENTIALS:
        {
          g_value_take_boxed (value, tp_value_array_build (2,
              G_TYPE_STRING, self->priv->username,
              G_TYPE_STRING, self->priv->password,
              G_TYPE_INVALID));
          break;
        }
      case PROP_STUN_SERVERS:
        {
          if (self->priv->stun_servers != NULL)
            g_value_set_boxed (value, self->priv->stun_servers);
          else
            g_value_take_boxed (value, g_ptr_array_new ());
          break;
        }
      case PROP_RELAY_INFO:
        {
          if (self->priv->relay_info != NULL)
            g_value_set_boxed (value, self->priv->relay_info);
          else
            g_value_take_boxed (value, g_ptr_array_new ());
          break;
        }
      case PROP_HAS_SERVER_INFO:
        g_value_set_boolean (value, self->priv->has_server_info);
        break;
      case PROP_ENDPOINTS:
        {
          GPtrArray *arr = g_ptr_array_sized_new (1);
          GList *l;

          for (l = self->priv->endpoints; l != NULL; l = g_list_next (l))
            {
              TpCallStreamEndpoint *e = l->data;

              g_ptr_array_add (arr,
                  g_strdup (tp_call_stream_endpoint_get_object_path (e)));
            }

          g_value_take_boxed (value, arr);
          break;
        }
      case PROP_ICE_RESTART_PENDING:
        g_value_set_boolean (value, self->priv->ice_restart_pending);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
tp_base_media_call_stream_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpBaseMediaCallStream *self = TP_BASE_MEDIA_CALL_STREAM (object);

  switch (property_id)
    {
      case PROP_TRANSPORT:
        self->priv->transport = g_value_get_uint (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
tp_base_media_call_stream_class_init (TpBaseMediaCallStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;
  TpBaseCallStreamClass *bcs_class = TP_BASE_CALL_STREAM_CLASS (klass);

  static TpDBusPropertiesMixinPropImpl stream_media_props[] = {
    { "SendingState", "sending-state", NULL },
    { "ReceivingState", "receiving-state", NULL },
    { "Transport", "transport", NULL },
    { "LocalCandidates", "local-candidates", NULL },
    { "LocalCredentials", "local-credentials", NULL },
    { "STUNServers", "stun-servers", NULL },
    { "RelayInfo", "relay-info", NULL },
    { "HasServerInfo", "has-server-info", NULL },
    { "Endpoints", "endpoints", NULL },
    { "ICERestartPending", "ice-restart-pending", NULL },
    { NULL }
  };

  g_type_class_add_private (klass, sizeof (TpBaseMediaCallStreamPrivate));

  object_class->set_property = tp_base_media_call_stream_set_property;
  object_class->get_property = tp_base_media_call_stream_get_property;
  object_class->dispose = tp_base_media_call_stream_dispose;
  object_class->finalize = tp_base_media_call_stream_finalize;

  bcs_class->extra_interfaces = tp_base_media_call_stream_interfaces;

  /**
   * TpBaseMediaCallStream:sending-state:
   *
   * The sending #TpStreamFlowState.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_uint ("sending-state", "SendingState",
      "The sending state",
      0, G_MAXUINT, TP_STREAM_FLOW_STATE_STOPPED,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SENDING_STATE,
      param_spec);

  /**
   * TpBaseMediaCallStream:receiving-state:
   *
   * The receiving #TpStreamFlowState.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_uint ("receiving-state", "ReceivingState",
      "The receiving state",
      0, G_MAXUINT, TP_STREAM_FLOW_STATE_STOPPED,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RECEIVING_STATE,
      param_spec);

  /**
   * TpBaseMediaCallStream:transport:
   *
   * The #TpStreamTransportType of this stream.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_uint ("transport", "Transport",
      "The transport type of this stream",
      0, G_MAXUINT, TP_STREAM_TRANSPORT_TYPE_UNKNOWN,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TRANSPORT,
      param_spec);

  /**
   * TpBaseMediaCallStream:local-candidates:
   *
   * #GPtrArray{candidate #GValueArray}
   * List of local candidates.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("local-candidates", "LocalCandidates",
      "List of local candidates",
      TP_ARRAY_TYPE_CANDIDATE_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCAL_CANDIDATES,
      param_spec);

  /**
   * TpBaseMediaCallStream:local-credentials:
   *
   * #GValueArray{username string, password string}
   * ufrag and pwd as defined by ICE.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("local-credentials", "LocalCredentials",
      "ufrag and pwd as defined by ICE",
      TP_STRUCT_TYPE_STREAM_CREDENTIALS,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCAL_CREDENTIALS,
      param_spec);

  /**
   * TpBaseMediaCallStream:stun-servers:
   *
   * #GPtrArray{stun-server #GValueArray}
   * List of STUN servers.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("stun-servers", "STUNServers",
      "List of STUN servers",
      TP_ARRAY_TYPE_SOCKET_ADDRESS_IP_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVERS,
      param_spec);

  /**
   * TpBaseMediaCallStream:relay-info:
   *
   * #GPtrArray{relay-info asv}
   * List of relay information.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("relay-info", "RelayInfo",
      "List of relay information",
      TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RELAY_INFO,
      param_spec);

  /**
   * TpBaseMediaCallStream:has-server-info:
   *
   * %TRUE if #TpBaseMediaCallStream:relay-info and
   * #TpBaseMediaCallStream:stun-servers have been set.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_boolean ("has-server-info", "HasServerInfo",
      "True if the server information about STUN and "
      "relay servers has been retrieved",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_HAS_SERVER_INFO,
      param_spec);

  /**
   * TpBaseMediaCallStream:endpoints:
   *
   * #GPtrArray{object-path string}
   * The endpoints of this content.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("endpoints", "Endpoints",
      "The endpoints of this content",
      TP_ARRAY_TYPE_OBJECT_PATH_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ENDPOINTS,
      param_spec);

  /**
   * TpBaseMediaCallStream:ice-restart-pending:
   *
   * %TRUE when ICERestartRequested signal is emitted, and %FALSE when
   * SetCredentials is called. Useful for debugging.
   *
   * Since: 0.UNRELEASED
   */
  param_spec = g_param_spec_boolean ("ice-restart-pending", "ICERestartPending",
      "True when ICERestartRequested signal is emitted, and False when "
      "SetCredentials is called. Useful for debugging",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ICE_RESTART_PENDING,
      param_spec);

  tp_dbus_properties_mixin_implement_interface (object_class,
      TP_IFACE_QUARK_CALL_STREAM_INTERFACE_MEDIA,
      tp_dbus_properties_mixin_getter_gobject_properties,
      NULL,
      stream_media_props);
}

/**
 * tp_base_media_call_stream_get_username:
 * @self: a #TpBaseMediaCallStream
 *
 * <!-- -->
 *
 * Returns: the username part of #TpBaseMediaCallStream:local-credentials
 * Since: 0.UNRELEASED
 */
const gchar *
tp_base_media_call_stream_get_username (TpBaseMediaCallStream *self)
{
  g_return_val_if_fail (TP_IS_BASE_MEDIA_CALL_STREAM (self), NULL);

  return self->priv->username;
}

/**
 * tp_base_media_call_stream_get_password:
 * @self: a #TpBaseMediaCallStream
 *
 * <!-- -->
 *
 * Returns: the password part of #TpBaseMediaCallStream:local-credentials
 * Since: 0.UNRELEASED
 */
const gchar *
tp_base_media_call_stream_get_password (TpBaseMediaCallStream *self)
{
  g_return_val_if_fail (TP_IS_BASE_MEDIA_CALL_STREAM (self), NULL);

  return self->priv->password;
}

static void
maybe_got_server_info (TpBaseMediaCallStream *self)
{
  if (self->priv->has_server_info ||
      self->priv->stun_servers == NULL ||
      self->priv->relay_info == NULL)
    return;

  self->priv->has_server_info = TRUE;
  tp_svc_call_stream_interface_media_emit_server_info_retrieved (self);
}

/**
 * tp_base_media_call_stream_set_stun_servers:
 * @self: a #TpBaseMediaCallStream
 * @stun_servers: the new stun servers
 *
 * Set the STUN servers. The #GPtrArray should have a free_func defined such as
 * g_ptr_array_ref() is enough to keep the data and g_ptr_array_unref() is
 * enough to release it later.
 *
 * Note that this replaces the previously set STUN servers, it is not an
 * addition.
 *
 * Since: 0.UNRELEASED
 */
void
tp_base_media_call_stream_set_stun_servers (TpBaseMediaCallStream *self,
    GPtrArray *stun_servers)
{
  g_return_if_fail (TP_IS_BASE_MEDIA_CALL_STREAM (self));
  g_return_if_fail (stun_servers != NULL);

  tp_clear_pointer (&self->priv->stun_servers, g_ptr_array_unref);
  self->priv->stun_servers = g_ptr_array_ref (stun_servers);

  tp_svc_call_stream_interface_media_emit_stun_servers_changed (self,
      self->priv->stun_servers);

  maybe_got_server_info (self);
}

/**
 * tp_base_media_call_stream_set_relay_info:
 * @self: a #TpBaseMediaCallStream
 * @relays: the new relays info
 *
 * Set the relays info. The #GPtrArray should have a free_func defined such as
 * g_ptr_array_ref() is enough to keep the data and g_ptr_array_unref() is
 * enough to release it later.
 *
 * Note that this replaces the previously set relays, it is not an addition.
 *
 * Since: 0.UNRELEASED
 */
void
tp_base_media_call_stream_set_relay_info (TpBaseMediaCallStream *self,
    GPtrArray *relays)
{
  g_return_if_fail (TP_IS_BASE_MEDIA_CALL_STREAM (self));
  g_return_if_fail (relays != NULL);

  tp_clear_pointer (&self->priv->relay_info, g_ptr_array_unref);
  self->priv->relay_info = g_ptr_array_ref (relays);

  tp_svc_call_stream_interface_media_emit_relay_info_changed (self,
      self->priv->relay_info);

  maybe_got_server_info (self);
}

/**
 * tp_base_media_call_stream_add_endpoint:
 * @self: a #TpBaseMediaCallStream
 * @endpoint: a #TpCallStreamEndpoint
 *
 * Add @endpoint to #TpBaseMediaCallStream:endpoints list, and emits
 * EndpointsChanged DBus signal.
 *
 * Since: 0.UNRELEASED
 */
void
tp_base_media_call_stream_add_endpoint (TpBaseMediaCallStream *self,
    TpCallStreamEndpoint *endpoint)
{
  GPtrArray *added;
  GPtrArray *removed;

  g_return_if_fail (TP_IS_BASE_MEDIA_CALL_STREAM (self));
  g_return_if_fail (TP_IS_CALL_STREAM_ENDPOINT (endpoint));

  self->priv->endpoints = g_list_append (self->priv->endpoints,
      g_object_ref (endpoint));

  added = g_ptr_array_new ();
  removed = g_ptr_array_new ();
  g_ptr_array_add (added,
      (gpointer) tp_call_stream_endpoint_get_object_path (endpoint));

  tp_svc_call_stream_interface_media_emit_endpoints_changed (self,
      added, removed);

  g_ptr_array_unref (added);
  g_ptr_array_unref (removed);
}

/**
 * tp_base_media_call_stream_get_endpoints:
 * @self: a #TpBaseMediaCallStream
 *
 * Same as #TpBaseMediaCallStream:endpoints but as a #GList of
 * #TpCallStreamEndpoint.
 *
 * Returns: Borrowed #GList of #TpCallStreamEndpoint.
 * Since: 0.UNRELEASED
 */
GList *
tp_base_media_call_stream_get_endpoints (TpBaseMediaCallStream *self)
{
  g_return_val_if_fail (TP_IS_BASE_MEDIA_CALL_STREAM (self), NULL);

  return self->priv->endpoints;
}

static void
tp_base_media_call_stream_set_credentials (TpSvcCallStreamInterfaceMedia *iface,
    const gchar *username,
    const gchar *password,
    DBusGMethodInvocation *context)
{
  TpBaseMediaCallStream *self = TP_BASE_MEDIA_CALL_STREAM (iface);

  g_free (self->priv->username);
  g_free (self->priv->password);
  self->priv->username = g_strdup (username);
  self->priv->password = g_strdup (password);

  tp_clear_pointer (&self->priv->local_candidates, g_ptr_array_unref);
  self->priv->local_candidates = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_value_array_free);

  g_object_notify (G_OBJECT (self), "local-candidates");
  g_object_notify (G_OBJECT (self), "local-credentials");

  tp_svc_call_stream_interface_media_emit_local_credentials_changed (self,
      username, password);

  tp_svc_call_stream_interface_media_return_from_set_credentials (context);
}

static void
tp_base_media_call_stream_add_candidates (TpSvcCallStreamInterfaceMedia *iface,
    const GPtrArray *candidates,
    DBusGMethodInvocation *context)
{
  TpBaseMediaCallStream *self = TP_BASE_MEDIA_CALL_STREAM (iface);
  TpBaseMediaCallStreamClass *klass =
      TP_BASE_MEDIA_CALL_STREAM_GET_CLASS (self);
  GPtrArray *accepted_candidates = NULL;
  guint i;
  GError *error = NULL;

  if (klass->add_local_candidates == NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Connection Manager did not implement "
          "TpBaseMediaCallStream::add_local_candidates vmethod" };
      dbus_g_method_return_error (context, &e);
      return;
    }

  accepted_candidates = klass->add_local_candidates (self, candidates, &error);
  if (accepted_candidates == NULL)
    {
      dbus_g_method_return_error (context, error);
      g_clear_error (&error);
      return;
    }

  for (i = 0; i < accepted_candidates->len; i++)
    {
      GValueArray *c = g_ptr_array_index (accepted_candidates, i);

      g_ptr_array_add (self->priv->local_candidates,
          g_value_array_copy (c));
    }

  tp_svc_call_stream_interface_media_emit_local_candidates_added (self,
      accepted_candidates);
  tp_svc_call_stream_interface_media_return_from_add_candidates (context);

  g_ptr_array_unref (accepted_candidates);
}

static void
tp_base_media_call_stream_finish_initial_candidates (
    TpSvcCallStreamInterfaceMedia *iface,
    DBusGMethodInvocation *context)
{
  TpBaseMediaCallStream *self = TP_BASE_MEDIA_CALL_STREAM (iface);
  TpBaseMediaCallStreamClass *klass =
      TP_BASE_MEDIA_CALL_STREAM_GET_CLASS (self);

  if (klass->finish_initial_candidates == NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Connection Manager did not implement "
          "TpBaseMediaCallStream::finish_initial_candidates vmethod" };
      dbus_g_method_return_error (context, &e);
      return;
    }

  klass->finish_initial_candidates (self);

  tp_svc_call_stream_interface_media_return_from_finish_initial_candidates (
      context);
}

static void
call_stream_media_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcCallStreamInterfaceMediaClass *klass =
      (TpSvcCallStreamInterfaceMediaClass *) g_iface;

#define IMPLEMENT(x) tp_svc_call_stream_interface_media_implement_##x (\
    klass, tp_base_media_call_stream_##x)
  //IMPLEMENT(complete_sending_state_change);
  //IMPLEMENT(report_sending_failure);
  //IMPLEMENT(complete_receiving_state_change);
  //IMPLEMENT(report_receiving_failure);
  IMPLEMENT(set_credentials);
  IMPLEMENT(add_candidates);
  IMPLEMENT(finish_initial_candidates);
  //IMPLEMENT(fail);
#undef IMPLEMENT
}
