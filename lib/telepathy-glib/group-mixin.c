/*
 * group-mixin.c - Source for TpGroupMixin
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
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

#include <telepathy-glib/debug-ansi.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/group-mixin.h>

#define DEBUG_FLAG TP_DEBUG_GROUPS

#include "internal-debug.h"

#include "_gen/signals-marshal.h"

#define TP_CHANNEL_GROUP_LOCAL_PENDING_WITH_INFO_ENTRY_TYPE \
    (dbus_g_type_get_struct ("GValueArray", \
        G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, \
        G_TYPE_INVALID))

static const char *group_change_reason_str(guint reason)
{
  switch (reason)
    {
    case TP_CHANNEL_GROUP_CHANGE_REASON_NONE:
      return "unspecified reason";
    case TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE:
      return "offline";
    case TP_CHANNEL_GROUP_CHANGE_REASON_KICKED:
      return "kicked";
    case TP_CHANNEL_GROUP_CHANGE_REASON_BUSY:
      return "busy";
    case TP_CHANNEL_GROUP_CHANGE_REASON_INVITED:
      return "invited";
    case TP_CHANNEL_GROUP_CHANGE_REASON_BANNED:
      return "banned";
    default:
      return "(unknown reason code)";
    }
}

typedef struct {
  TpHandle actor;
  guint reason;
  const gchar *message;
  TpHandleRepoIface *repo;
} LocalPendingInfo;

static LocalPendingInfo *
local_pending_info_new(TpHandleRepoIface *repo, 
                       TpHandle actor, 
                       guint reason, 
                       const gchar *message) 
{
  LocalPendingInfo *info = g_slice_new0 (LocalPendingInfo);
  info->actor = actor;
  info->reason = reason;
  info->message = g_strdup (message);
  info->repo = repo;
  tp_handle_ref (repo, actor);

  return info;
}

static void
local_pending_info_free(LocalPendingInfo *info) 
{
  g_free ((gchar *)info->message);
  tp_handle_unref (info->repo, info->actor);
  g_slice_free (LocalPendingInfo, info);
}

struct _TpGroupMixinPrivate {
    TpHandleSet *actors;
    GHashTable *handle_owners;
    GHashTable *local_pending_info;
};


/**
 * tp_group_mixin_class_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObjectClass
 */
GQuark
tp_group_mixin_class_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("TpGroupMixinClassOffsetQuark");
  return offset_quark;
}

/**
 * tp_group_mixin_get_offset_quark:
 *
 * Returns: the quark used for storing mixin offset on a GObject
 */
GQuark
tp_group_mixin_get_offset_quark ()
{
  static GQuark offset_quark = 0;
  if (!offset_quark)
    offset_quark = g_quark_from_static_string("TpGroupMixinOffsetQuark");
  return offset_quark;
}

void tp_group_mixin_class_init (TpSvcChannelInterfaceGroupClass *obj_cls,
                                glong offset,
                                TpGroupMixinAddMemberFunc add_func,
                                TpGroupMixinRemMemberFunc rem_func)
{
  TpGroupMixinClass *mixin_cls;

  g_assert (G_IS_OBJECT_CLASS (obj_cls));

  g_type_set_qdata (G_OBJECT_CLASS_TYPE (obj_cls),
                    TP_GROUP_MIXIN_CLASS_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin_cls = TP_GROUP_MIXIN_CLASS (obj_cls);

  mixin_cls->add_member = add_func;
  mixin_cls->remove_member = rem_func;
}

void tp_group_mixin_init (TpSvcChannelInterfaceGroup *obj,
                          glong offset,
                          TpHandleRepoIface *handle_repo,
                          TpHandle self_handle)
{
  TpGroupMixin *mixin;

  g_assert (G_IS_OBJECT (obj));

  g_type_set_qdata (G_OBJECT_TYPE (obj),
                    TP_GROUP_MIXIN_OFFSET_QUARK,
                    GINT_TO_POINTER (offset));

  mixin = TP_GROUP_MIXIN (obj);

  mixin->handle_repo = handle_repo;
  mixin->self_handle = self_handle;

  mixin->group_flags = 0;

  mixin->members = tp_handle_set_new (handle_repo);
  mixin->local_pending = tp_handle_set_new (handle_repo);
  mixin->remote_pending = tp_handle_set_new (handle_repo);

  mixin->priv = g_new0 (TpGroupMixinPrivate, 1);
  mixin->priv->handle_owners = g_hash_table_new (g_direct_hash, g_direct_equal);
  mixin->priv->local_pending_info = g_hash_table_new_full (
                                                     g_direct_hash, 
                                                     g_direct_equal,
                                                     NULL,
                                                     (GDestroyNotify)
                                                       local_pending_info_free);
  mixin->priv->actors = tp_handle_set_new (handle_repo);
}

static void
handle_owners_foreach_unref (gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  TpGroupMixin *mixin = user_data;

  tp_handle_unref (mixin->handle_repo, GPOINTER_TO_UINT (key));
  tp_handle_unref (mixin->handle_repo, GPOINTER_TO_UINT (value));
}

void tp_group_mixin_finalize (TpSvcChannelInterfaceGroup *obj)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  tp_handle_set_destroy (mixin->priv->actors);

  g_hash_table_foreach (mixin->priv->handle_owners,
                        handle_owners_foreach_unref,
                        mixin);

  g_hash_table_destroy (mixin->priv->handle_owners);
  g_hash_table_destroy (mixin->priv->local_pending_info);

  g_free (mixin->priv);

  tp_handle_set_destroy (mixin->members);
  tp_handle_set_destroy (mixin->local_pending);
  tp_handle_set_destroy (mixin->remote_pending);
}

gboolean
tp_group_mixin_get_self_handle (TpSvcChannelInterfaceGroup *obj, guint *ret, GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  if (tp_handle_set_is_member (mixin->members, mixin->self_handle) ||
      tp_handle_set_is_member (mixin->local_pending, mixin->self_handle) ||
      tp_handle_set_is_member (mixin->remote_pending, mixin->self_handle))
    {
      *ret = mixin->self_handle;
    }
  else
    {
      *ret = 0;
    }

  return TRUE;
}

static void
tp_group_mixin_get_self_handle_async (TpSvcChannelInterfaceGroup *obj,
                                      DBusGMethodInvocation *context)
{
  guint ret;
  GError *error = NULL;

  if (tp_group_mixin_get_self_handle (obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_self_handle (
          context, ret);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

gboolean
tp_group_mixin_get_group_flags (TpSvcChannelInterfaceGroup *obj, guint *ret, GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *ret = mixin->group_flags;

  return TRUE;
}

static void
tp_group_mixin_get_group_flags_async (TpSvcChannelInterfaceGroup *obj,
                                      DBusGMethodInvocation *context)
{
  guint ret;
  GError *error = NULL;

  if (tp_group_mixin_get_group_flags (obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_group_flags (
          context, ret);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

gboolean
tp_group_mixin_add_members (TpSvcChannelInterfaceGroup *obj, const GArray *contacts, const gchar *message, GError **error)
{
  TpGroupMixinClass *mixin_cls = TP_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  guint i;
  TpHandle handle;

  /* reject invalid handles */
  if (!tp_handles_are_valid (mixin->handle_repo, contacts, FALSE, error))
    return FALSE;

  /* check that adding is allowed by flags */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, TpHandle, i);

      if ((mixin->group_flags & TP_CHANNEL_GROUP_FLAG_CAN_ADD) == 0 &&
          !tp_handle_set_is_member (mixin->local_pending, handle))
        {
          DEBUG ("handle %u cannot be added to members without GROUP_FLAG_CAN_ADD",
              handle);

          g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
              "handle %u cannot be added to members without GROUP_FLAG_CAN_ADD",
              handle);

          return FALSE;
        }
    }

  /* add handle by handle */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, TpHandle, i);

      if (tp_handle_set_is_member (mixin->members, handle))
        {
          DEBUG ("handle %u is already a member, skipping", handle);

          continue;
        }

      if (!mixin_cls->add_member (obj, handle, message, error))
        {
          return FALSE;
        }
    }

  return TRUE;
}

static void
tp_group_mixin_add_members_async (TpSvcChannelInterfaceGroup *obj,
                                  const GArray *contacts,
                                  const gchar *message,
                                  DBusGMethodInvocation *context)
{
  GError *error = NULL;

  if (tp_group_mixin_add_members (obj, contacts, message, &error))
    {
      tp_svc_channel_interface_group_return_from_add_members (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

gboolean
tp_group_mixin_remove_members (TpSvcChannelInterfaceGroup *obj, const GArray *contacts, const gchar *message, GError **error)
{
  TpGroupMixinClass *mixin_cls = TP_GROUP_MIXIN_CLASS (G_OBJECT_GET_CLASS (obj));
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  guint i;
  TpHandle handle;

  /* reject invalid handles */
  if (!tp_handles_are_valid (mixin->handle_repo, contacts, FALSE, error))
    return FALSE;

  /* check removing is allowed by flags */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, TpHandle, i);

      if (tp_handle_set_is_member (mixin->members, handle))
        {
          if ((mixin->group_flags & TP_CHANNEL_GROUP_FLAG_CAN_REMOVE) == 0)
            {
              DEBUG ("handle %u cannot be removed from members without GROUP_FLAG_CAN_REMOVE",
                  handle);

              g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                  "handle %u cannot be removed from members without GROUP_FLAG_CAN_REMOVE",
                  handle);

              return FALSE;
            }
        }
      else if (tp_handle_set_is_member (mixin->remote_pending, handle))
        {
          if ((mixin->group_flags & TP_CHANNEL_GROUP_FLAG_CAN_RESCIND) == 0)
            {
              DEBUG ("handle %u cannot be removed from remote pending without GROUP_FLAG_CAN_RESCIND",
                  handle);

              g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
                  "handle %u cannot be removed from remote pending without GROUP_FLAG_CAN_RESCIND",
                  handle);

              return FALSE;
            }
        }
      else if (!tp_handle_set_is_member (mixin->local_pending, handle))
        {
          DEBUG ("handle %u is not a current or pending member",
                   handle);

          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "handle %u is not a current or pending member", handle);

          return FALSE;
        }
    }

  /* remove handle by handle */
  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, TpHandle, i);

      if (!mixin_cls->remove_member (obj, handle, message, error))
        {
          return FALSE;
        }
    }

  return TRUE;
}

static void
tp_group_mixin_remove_members_async (TpSvcChannelInterfaceGroup *obj,
                                     const GArray *contacts,
                                     const gchar *message,
                                     DBusGMethodInvocation *context)
{
  GError *error = NULL;

  if (tp_group_mixin_remove_members (obj, contacts, message, &error))
    {
      tp_svc_channel_interface_group_return_from_remove_members (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

gboolean
tp_group_mixin_get_members (TpSvcChannelInterfaceGroup *obj, GArray **ret, GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *ret = tp_handle_set_to_array (mixin->members);

  return TRUE;
}

static void
tp_group_mixin_get_members_async (TpSvcChannelInterfaceGroup *obj,
                                  DBusGMethodInvocation *context)
{
  GArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_members (obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_members (
          context, ret);
      g_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

gboolean
tp_group_mixin_get_local_pending_members (TpSvcChannelInterfaceGroup *obj, GArray **ret, GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *ret = tp_handle_set_to_array (mixin->local_pending);

  return TRUE;
}

static void
tp_group_mixin_get_local_pending_members_async (TpSvcChannelInterfaceGroup *obj,
                                                DBusGMethodInvocation *context)
{
  GArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_local_pending_members (obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_local_pending_members (
          context, ret);
      g_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

static void
local_pending_members_with_info_foreach(TpHandleSet *set, 
                                        TpHandle i, 
                                        gpointer userdata) 
{
  gpointer *data = (gpointer *)userdata;
  TpGroupMixin *mixin = (TpGroupMixin *) data[0];
  TpGroupMixinPrivate *priv = mixin->priv;
  GPtrArray *array = (GPtrArray *)data[1];
  GValue entry = { 0, };
  LocalPendingInfo *info = g_hash_table_lookup (priv->local_pending_info, 
                                                GUINT_TO_POINTER(i));
  g_assert(info != NULL);

  g_value_init(&entry, TP_CHANNEL_GROUP_LOCAL_PENDING_WITH_INFO_ENTRY_TYPE);
  g_value_take_boxed(&entry, 
      dbus_g_type_specialized_construct(
          TP_CHANNEL_GROUP_LOCAL_PENDING_WITH_INFO_ENTRY_TYPE));

  dbus_g_type_struct_set(&entry,
      0, i,
      1, info->actor,
      2, info->reason,
      3, info->message);


  g_ptr_array_add (array, g_value_get_boxed (&entry));
}

gboolean 
tp_group_mixin_get_local_pending_members_with_info (
                                               TpSvcChannelInterfaceGroup *obj,
                                               GPtrArray **ret, 
                                               GError **error) 
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  gpointer data[2] = { mixin, NULL };

  *ret = g_ptr_array_new();
  data[1] = *ret;

  tp_handle_set_foreach (mixin->local_pending, 
                        local_pending_members_with_info_foreach, data);

  return TRUE;
}

static void
tp_group_mixin_get_local_pending_members_with_info_async (
                                                TpSvcChannelInterfaceGroup *obj,
                                                DBusGMethodInvocation *context)
{
  GPtrArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_local_pending_members_with_info (obj, &ret, &error))
    {
      guint i;
      tp_svc_channel_interface_group_return_from_get_local_pending_members_with_info (
          context, ret);
      for (i = 0 ; i < ret->len; i++) {
        g_value_array_free (g_ptr_array_index (ret,i));
      }
      g_ptr_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

gboolean
tp_group_mixin_get_remote_pending_members (TpSvcChannelInterfaceGroup *obj, GArray **ret, GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *ret = tp_handle_set_to_array (mixin->remote_pending);

  return TRUE;
}

static void
tp_group_mixin_get_remote_pending_members_async (TpSvcChannelInterfaceGroup *obj,
                                                 DBusGMethodInvocation *context)
{
  GArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_remote_pending_members (obj, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_remote_pending_members (
          context, ret);
      g_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

gboolean
tp_group_mixin_get_all_members (TpSvcChannelInterfaceGroup *obj, GArray **ret, GArray **ret1, GArray **ret2, GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  *ret = tp_handle_set_to_array (mixin->members);
  *ret1 = tp_handle_set_to_array (mixin->local_pending);
  *ret2 = tp_handle_set_to_array (mixin->remote_pending);

  return TRUE;
}

static void
tp_group_mixin_get_all_members_async (TpSvcChannelInterfaceGroup *obj,
                                      DBusGMethodInvocation *context)
{
  GArray *mem, *local, *remote;
  GError *error = NULL;

  if (tp_group_mixin_get_all_members (obj, &mem, &local, &remote, &error))
    {
      tp_svc_channel_interface_group_return_from_get_all_members (
          context, mem, local, remote);
      g_array_free (mem, TRUE);
      g_array_free (local, TRUE);
      g_array_free (remote, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

gboolean
tp_group_mixin_get_handle_owners (TpSvcChannelInterfaceGroup *obj,
                                      const GArray *handles,
                                      GArray **ret,
                                      GError **error)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpGroupMixinPrivate *priv = mixin->priv;
  guint i;

  if ((mixin->group_flags &
        TP_CHANNEL_GROUP_FLAG_CHANNEL_SPECIFIC_HANDLES) == 0)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "channel doesn't have channel specific handles");

      return FALSE;
    }

  if (!tp_handles_are_valid (mixin->handle_repo, handles, FALSE, error))
    {
      return FALSE;
    }

  *ret = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), handles->len);

  for (i = 0; i < handles->len; i++)
    {
      TpHandle local_handle = g_array_index (handles, TpHandle, i);
      TpHandle owner_handle;

      if (!tp_handle_set_is_member (mixin->members, local_handle))
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "handle %u is not a member", local_handle);

          g_array_free (*ret, TRUE);
          *ret = NULL;

          return FALSE;
        }

      owner_handle = GPOINTER_TO_UINT (
          g_hash_table_lookup (priv->handle_owners,
                               GUINT_TO_POINTER (local_handle)));

      g_array_append_val (*ret, owner_handle);
    }

  return TRUE;
}

static void
tp_group_mixin_get_handle_owners_async (TpSvcChannelInterfaceGroup *obj,
                                        const GArray *handles,
                                        DBusGMethodInvocation *context)
{
  GArray *ret;
  GError *error = NULL;

  if (tp_group_mixin_get_handle_owners (obj, handles, &ret, &error))
    {
      tp_svc_channel_interface_group_return_from_get_handle_owners (
          context, ret);
      g_array_free (ret, TRUE);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

#define GFTS_APPEND_FLAG_IF_SET(flag) \
  if (flags & flag) \
    { \
      if (i++ > 0) \
        g_string_append (str, "|"); \
      g_string_append (str, #flag + 22); \
    }

static gchar *
group_flags_to_string (TpChannelGroupFlags flags)
{
  gint i = 0;
  GString *str;

  str = g_string_new ("[" TP_ANSI_BOLD_OFF);

  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_CAN_ADD);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_CAN_REMOVE);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_ACCEPT);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_REJECT);
  GFTS_APPEND_FLAG_IF_SET (TP_CHANNEL_GROUP_FLAG_MESSAGE_RESCIND);

  g_string_append (str, TP_ANSI_BOLD_ON "]");

  return g_string_free (str, FALSE);
}

/**
 * tp_group_mixin_change_flags:
 *
 * Request a change to be made to the flags. Emits the
 * signal with the changes which were made.
 */
void
tp_group_mixin_change_flags (TpSvcChannelInterfaceGroup *obj,
                                 TpChannelGroupFlags add,
                                 TpChannelGroupFlags remove)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpChannelGroupFlags added, removed;

  added = add & ~mixin->group_flags;
  mixin->group_flags |= added;

  removed = remove & mixin->group_flags;
  mixin->group_flags &= ~removed;

  if (add != 0 || remove != 0)
    {
      gchar *str_added, *str_removed, *str_flags;

      if (DEBUGGING)
        {
          str_added = group_flags_to_string (added);
          str_removed = group_flags_to_string (removed);
          str_flags = group_flags_to_string (mixin->group_flags);

          printf (TP_ANSI_BOLD_ON TP_ANSI_FG_WHITE
                  "%s: emitting group flags changed\n"
                  "  added    : %s\n"
                  "  removed  : %s\n"
                  "  flags now: %s\n" TP_ANSI_RESET,
                  G_STRFUNC, str_added, str_removed, str_flags);

          fflush (stdout);

          g_free (str_added);
          g_free (str_removed);
          g_free (str_flags);
        }

      tp_svc_channel_interface_group_emit_group_flags_changed (obj, added, removed);
    }
}

static gchar *
member_array_to_string (TpHandleRepoIface *repo, 
                        const GArray *array)
{
  GString *str;
  guint i;

  str = g_string_new ("[" TP_ANSI_BOLD_OFF);

  for (i = 0; i < array->len; i++)
    {
      TpHandle handle;
      const gchar *handle_str;

      handle = g_array_index (array, guint32, i);
      handle_str = tp_handle_inspect (repo, handle);

      g_string_append_printf (str, "%s%u (%s)",
          (i > 0) ? "\n              " : "",
          handle, handle_str);
    }

  g_string_append (str, TP_ANSI_BOLD_ON "]");

  return g_string_free (str, FALSE);
}

static void remove_handle_owners_if_exist (TpSvcChannelInterfaceGroup *obj, GArray *array);

static void 
local_pending_added_foreach(guint i, 
                            gpointer userdata) 
{
  gpointer *data = (gpointer *)userdata;
  TpGroupMixin *mixin = (TpGroupMixin *) data[0]; 
  TpGroupMixinPrivate *priv = mixin->priv;
  LocalPendingInfo *info = (LocalPendingInfo *)data[1];

  g_hash_table_insert (priv->local_pending_info, 
                       GUINT_TO_POINTER (i), 
                       local_pending_info_new (mixin->handle_repo,
                       info->actor, info->reason, info->message));
}

static void
local_pending_added(TpGroupMixin *mixin, 
                    TpIntSet *added, 
                    TpHandle actor, 
                    guint reason, 
                    const gchar *message) 
{
  LocalPendingInfo info;
  gpointer data[2] = { mixin, &info };
  info.actor = actor;
  info.reason = reason;
  info.message = message;

  tp_intset_foreach (added, local_pending_added_foreach, data);
}

void 
local_pending_remove_foreach(guint i, 
                             gpointer userdata) 
{
  TpGroupMixin *mixin = (TpGroupMixin *) userdata;
  TpGroupMixinPrivate *priv = mixin->priv;

  g_hash_table_remove (priv->local_pending_info, GUINT_TO_POINTER(i));
}

static void
local_pending_remove(TpGroupMixin *mixin, 
                     TpIntSet *removed) 
{ 
  tp_intset_foreach (removed, local_pending_remove_foreach, mixin);
}

/**
 * tp_group_mixin_change_members:
 *
 * Request members to be added, removed or marked as local or remote pending.
 * Changes member sets, references, and emits the MembersChanged signal.
 */
gboolean
tp_group_mixin_change_members (TpSvcChannelInterfaceGroup *obj,
                                   const gchar *message,
                                   TpIntSet *add,
                                   TpIntSet *remove,
                                   TpIntSet *local_pending,
                                   TpIntSet *remote_pending,
                                   TpHandle actor,
                                   guint reason)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpIntSet *new_add, *new_remove, *new_local_pending,
           *new_remote_pending, *tmp, *tmp2, *empty;
  gboolean ret;

  empty = tp_intset_new ();

  if (add == NULL)
    add = empty;

  if (remove == NULL)
    remove = empty;

  if (local_pending == NULL)
    local_pending = empty;

  if (remote_pending == NULL)
    remote_pending = empty;

  /* members + add */
  new_add = tp_handle_set_update (mixin->members, add);

  /* members - remove */
  new_remove = tp_handle_set_difference_update (mixin->members, remove);

  /* members - local_pending */
  tmp = tp_handle_set_difference_update (mixin->members, local_pending);
  tp_intset_destroy (tmp);

  /* members - remote_pending */
  tmp = tp_handle_set_difference_update (mixin->members, remote_pending);
  tp_intset_destroy (tmp);


  /* local pending + local_pending */
  new_local_pending = tp_handle_set_update (mixin->local_pending, local_pending);
  local_pending_added (mixin, local_pending, actor, reason, message);

  /* local pending - add */
  tmp = tp_handle_set_difference_update (mixin->local_pending, add);
  local_pending_remove (mixin, tmp);
  tp_intset_destroy (tmp);

  /* local pending - remove */
  tmp = tp_handle_set_difference_update (mixin->local_pending, remove);
  local_pending_remove (mixin, tmp);

  tmp2 = tp_intset_union (new_remove, tmp);
  tp_intset_destroy (new_remove);
  tp_intset_destroy (tmp);
  new_remove = tmp2;

  /* local pending - remote_pending */
  tmp = tp_handle_set_difference_update (mixin->local_pending, remote_pending);
  local_pending_remove (mixin, tmp);
  tp_intset_destroy (tmp);


  /* remote pending + remote_pending */
  new_remote_pending = tp_handle_set_update (mixin->remote_pending, remote_pending);

  /* remote pending - add */
  tmp = tp_handle_set_difference_update (mixin->remote_pending, add);
  tp_intset_destroy (tmp);

  /* remote pending - remove */
  tmp = tp_handle_set_difference_update (mixin->remote_pending, remove);
  tmp2 = tp_intset_union (new_remove, tmp);
  tp_intset_destroy (new_remove);
  tp_intset_destroy (tmp);
  new_remove = tmp2;

  /* remote pending - local_pending */
  tmp = tp_handle_set_difference_update (mixin->remote_pending, local_pending);
  tp_intset_destroy (tmp);

  if (tp_intset_size (new_add) > 0 ||
      tp_intset_size (new_remove) > 0 ||
      tp_intset_size (new_local_pending) > 0 ||
      tp_intset_size (new_remote_pending) > 0)
    {
      GArray *arr_add, *arr_remove, *arr_local, *arr_remote;
      gchar *add_str, *rem_str, *local_str, *remote_str;

      /* translate intsets to arrays */
      arr_add = tp_intset_to_array (new_add);
      arr_remove = tp_intset_to_array (new_remove);
      arr_local = tp_intset_to_array (new_local_pending);
      arr_remote = tp_intset_to_array (new_remote_pending);

      /* remove any handle owner mappings */
      remove_handle_owners_if_exist (obj, arr_remove);

      if (DEBUGGING)
        {
          add_str = member_array_to_string (mixin->handle_repo, arr_add);
          rem_str = member_array_to_string (mixin->handle_repo, arr_remove);
          local_str = member_array_to_string (mixin->handle_repo, arr_local);
          remote_str = member_array_to_string (mixin->handle_repo, arr_remote);

          printf (TP_ANSI_BOLD_ON TP_ANSI_FG_CYAN
                  "%s: emitting members changed\n"
                  "  message       : \"%s\"\n"
                  "  added         : %s\n"
                  "  removed       : %s\n"
                  "  local_pending : %s\n"
                  "  remote_pending: %s\n"
                  "  actor         : %u\n"
                  "  reason        : %u: %s\n" TP_ANSI_RESET,
                  G_STRFUNC, message, add_str, rem_str, local_str, remote_str,
                  actor, reason, group_change_reason_str(reason));

          fflush (stdout);

          g_free (add_str);
          g_free (rem_str);
          g_free (local_str);
          g_free (remote_str);
        }

      if (actor)
        {
          tp_handle_set_add (mixin->priv->actors, actor);
        }
      /* emit signal */
      tp_svc_channel_interface_group_emit_members_changed (obj, message,
          arr_add, arr_remove, arr_local, arr_remote, actor, reason);

      /* free arrays */
      g_array_free (arr_add, TRUE);
      g_array_free (arr_remove, TRUE);
      g_array_free (arr_local, TRUE);
      g_array_free (arr_remote, TRUE);

      ret = TRUE;
    }
  else
    {
      DEBUG ("not emitting signal, nothing changed");

      ret = FALSE;
    }

  /* free intsets */
  tp_intset_destroy (new_add);
  tp_intset_destroy (new_remove);
  tp_intset_destroy (new_local_pending);
  tp_intset_destroy (new_remote_pending);
  tp_intset_destroy (empty);

  return ret;
}

void
tp_group_mixin_add_handle_owner (TpSvcChannelInterfaceGroup *obj,
                                     TpHandle local_handle,
                                     TpHandle owner_handle)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpGroupMixinPrivate *priv = mixin->priv;

  g_hash_table_insert (priv->handle_owners, GUINT_TO_POINTER (local_handle),
                       GUINT_TO_POINTER (owner_handle));

  tp_handle_ref (mixin->handle_repo, local_handle);
  tp_handle_ref (mixin->handle_repo, owner_handle);
}

static void
remove_handle_owners_if_exist (TpSvcChannelInterfaceGroup *obj, GArray *array)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpGroupMixinPrivate *priv = mixin->priv;
  guint i;

  for (i = 0; i < array->len; i++)
    {
      TpHandle handle = g_array_index (array, guint32, i);
      gpointer local_handle, owner_handle;

      if (g_hash_table_lookup_extended (priv->handle_owners,
                                        GUINT_TO_POINTER (handle),
                                        &local_handle,
                                        &owner_handle))
        {
          tp_handle_unref (mixin->handle_repo, GPOINTER_TO_UINT (local_handle));
          tp_handle_unref (mixin->handle_repo, GPOINTER_TO_UINT (owner_handle));

          g_hash_table_remove (priv->handle_owners, GUINT_TO_POINTER (handle));
        }
    }
}

void tp_group_mixin_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceGroupClass *klass = (TpSvcChannelInterfaceGroupClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_group_implement_##x (klass,\
    tp_group_mixin_##x##_async)
  IMPLEMENT(add_members);
  IMPLEMENT(get_all_members);
  IMPLEMENT(get_group_flags);
  IMPLEMENT(get_handle_owners);
  IMPLEMENT(get_local_pending_members);
  IMPLEMENT(get_local_pending_members_with_info);
  IMPLEMENT(get_members);
  IMPLEMENT(get_remote_pending_members);
  IMPLEMENT(get_self_handle);
  IMPLEMENT(remove_members);
#undef IMPLEMENT
}
