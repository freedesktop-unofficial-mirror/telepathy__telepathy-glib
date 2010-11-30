/*
 * TpConnection - proxy for a Telepathy connection (internals)
 *
 * Copyright (C) 2008 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef TP_CONNECTION_INTERNAL_H
#define TP_CONNECTION_INTERNAL_H

#include <telepathy-glib/capabilities.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/contact.h>
#include <telepathy-glib/intset.h>

G_BEGIN_DECLS

typedef void (*TpConnectionProc) (TpConnection *self);

struct _TpConnectionPrivate {
    /* list of TpConnectionProc */
    GList *introspect_needed;

    TpHandle self_handle;
    TpConnectionStatus status;
    TpConnectionStatusReason status_reason;
    gchar *connection_error;
    /* a TP_HASH_TYPE_STRING_VARIANT_MAP */
    GHashTable *connection_error_details;

    /* GArray of GQuark */
    GArray *contact_attribute_interfaces;

    /* items are GQuarks that represent arguments to
     * Connection.AddClientInterests */
    TpIntSet *interests;

    /* TpHandle => weak ref to TpContact */
    GHashTable *contacts;

    TpCapabilities *capabilities;

    TpAvatarRequirements *avatar_requirements;
    GArray *avatar_request_queue;
    guint avatar_request_idle_id;

    TpContactInfoFlags contact_info_flags;
    GList *contact_info_supported_fields;

    TpProxyPendingCall *introspection_call;
    unsigned fetching_rcc:1;
    unsigned fetching_avatar_requirements:1;
    unsigned contact_info_fetched:1;

    unsigned ready:1;
    unsigned tracking_aliases_changed:1;
    unsigned tracking_avatar_updated:1;
    unsigned tracking_avatar_retrieved:1;
    unsigned tracking_presences_changed:1;
    unsigned tracking_presence_update:1;
    unsigned tracking_location_changed:1;
    unsigned tracking_contact_caps_changed:1;
    unsigned tracking_contact_info_changed:1;
    unsigned introspecting_after_connected:1;
    unsigned tracking_client_types_updated:1;
};

void _tp_connection_status_reason_to_gerror (TpConnectionStatusReason reason,
    TpConnectionStatus prev_status,
    const gchar **ret_str,
    GError **error);

void _tp_connection_init_handle_refs (TpConnection *self);
void _tp_connection_clean_up_handle_refs (TpConnection *self);

void _tp_connection_add_contact (TpConnection *self, TpHandle handle,
    TpContact *contact);
void _tp_connection_remove_contact (TpConnection *self, TpHandle handle,
    TpContact *contact);
TpContact *_tp_connection_lookup_contact (TpConnection *self, TpHandle handle);

/* Actually implemented in contact.c, but having a contact-internal header
 * just for this would be overkill */
void _tp_contact_connection_invalidated (TpContact *contact);

/* connection-contact-info.c */
void _tp_connection_maybe_prepare_contact_info (TpProxy *proxy);
TpContactInfoFieldSpec *_tp_contact_info_field_spec_new (const gchar *name,
    GStrv parameters, TpContactInfoFieldFlags flags, guint max);

/* connection-avatars.c */
void _tp_connection_maybe_prepare_avatar_requirements (TpProxy *proxy);

G_END_DECLS

#endif