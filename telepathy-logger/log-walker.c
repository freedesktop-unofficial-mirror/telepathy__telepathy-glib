/*
 * Copyright (C) 2012 Red Hat, Inc.
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
 * Author: Debarshi Ray <debarshir@freedesktop.org>
 */

#include "config.h"

#include "log-walker.h"
#include "log-walker-internal.h"

#include <telepathy-logger/event.h>
#include <telepathy-logger/log-iter-internal.h>

/**
 * SECTION:log-walker
 * @title: TplLogWalker
 * @short_description: Iterate over the logs
 *
 * The #TplLogWalker object allows the user to sequentially iterate
 * over the logs.
 *
 * <example>
 *   <title>Using a TplLogWalker to fetch text events from the logs.</title>
 *   <programlisting>
 *   #include <telepathy-glib/telepathy-glib.h>
 *   #include <telepathy-logger/telepathy-logger.h>
 *
 *   static GMainLoop * loop = NULL;
 *
 *   static void
 *   events_foreach (gpointer data, gpointer user_data)
 *   {
 *     TplEvent *event = TPL_EVENT (data);
 *     const gchar *message;
 *     gint64 timestamp;
 *
 *     timestamp = tpl_event_get_timestamp (event);
 *     message = tpl_text_event_get_message (TPL_TEXT_EVENT (event));
 *     g_message ("%" G_GINT64_FORMAT " %s", timestamp, message);
 *   }
 *
 *   static void
 *   log_walker_get_events_cb (GObject *source_object,
 *       GAsyncResult *res,
 *       gpointer user_data)
 *   {
 *     TplLogWalker *walker = TPL_LOG_WALKER (source_object);
 *     GList *events;
 *
 *     if (!tpl_log_walker_get_events_finish (walker, res, &events, NULL))
 *       {
 *         g_main_loop_quit (loop);
 *         return;
 *       }
 *
 *     g_list_foreach (events, events_foreach, NULL);
 *     g_list_free_full (events, g_object_unref);
 *     if (tpl_log_walker_is_end (walker))
 *       {
 *         g_main_loop_quit (loop);
 *         return;
 *       }
 *
 *     g_message ("");
 *     tpl_log_walker_get_events_async (walker,
 *         5,
 *         log_walker_get_events_cb,
 *         NULL);
 *   }
 *
 *   static void
 *   accounts_foreach (gpointer data, gpointer user_data)
 *   {
 *     TpAccount **account_out = (TpAccount **) user_data;
 *     TpAccount *account = TP_ACCOUNT (data);
 *     const gchar *display_name;
 *
 *     display_name = tp_account_get_display_name (account);
 *     if (0 != g_strcmp0 (display_name, "alice@bar.net"))
 *       return;
 *
 *     g_object_ref (account);
 *     *account_out = account;
 *   }
 *
 *   static void
 *   account_manager_prepare_cb (GObject * source_object,
 *       GAsyncResult * res,
 *       gpointer user_data)
 *   {
 *     TpAccountManager *account_manager = TP_ACCOUNT_MANAGER (source_object);
 *     GList *accounts;
 *     TpAccount *account = NULL;
 *     TplLogManager *log_manager;
 *     TplLogWalker *walker;
 *     TplEntity *target;
 *
 *     if (!tp_proxy_prepare_finish (source_object, res, NULL))
 *       return;
 *
 *     accounts = tp_account_manager_get_valid_accounts (account_manager);
 *     g_list_foreach (accounts, accounts_foreach, &account);
 *     g_list_free_full (accounts, g_object_unref);
 *     if (account == NULL)
 *       {
 *         g_main_loop_quit (loop);
 *         return;
 *       }
 *
 *     log_manager = tpl_log_manager_dup_singleton ();
 *
 *     target = tpl_entity_new ("bob@foo.net", TPL_ENTITY_CONTACT, NULL, NULL);
 *
 *     walker = tpl_log_manager_walk_filtered_events (log_manager,
 *         account,
 *         target,
 *         TPL_EVENT_MASK_TEXT,
 *         NULL,
 *         NULL);
 *
 *     tpl_log_walker_get_events_async (walker,
 *         5,
 *         log_walker_get_events_cb,
 *         NULL);
 *
 *     g_object_unref (walker);
 *     g_object_unref (target);
 *     g_object_unref (log_manager);
 *     g_object_unref (account);
 *   }
 *
 *   int
 *   main (int argc,
 *       char *argv[])
 *   {
 *     GQuark features[] = { TP_ACCOUNT_MANAGER_FEATURE_CORE, 0 };
 *     TpAccountManager * account_manager;
 *
 *     g_type_init ();
 *     loop = g_main_loop_new (NULL, FALSE);
 *
 *     account_manager = tp_account_manager_dup ();
 *     tp_proxy_prepare_async (account_manager,
 *         features,
 *         account_manager_prepare_cb,
 *         NULL);
 *
 *     g_main_loop_run (loop);
 *
 *     g_object_unref (account_manager);
 *     g_main_loop_unref (loop);
 *     return 0;
 *   }
 *   </programlisting>
 * </example>
 */

/**
 * TplLogWalker:
 *
 * An object used to iterate over the logs
 */

struct _TplLogWalkerPriv
{
  GList *caches;
  GList *history;
  GList *iters;
  GMutex mutex;
  gboolean is_start;
  gboolean is_end;
};


G_DEFINE_TYPE (TplLogWalker, tpl_log_walker, G_TYPE_OBJECT);


static const gsize CACHE_SIZE = 5;

typedef struct
{
  GAsyncReadyCallback cb;
  gpointer user_data;
  guint num_events;
} TplLogWalkerAsyncData;

typedef struct
{
  TplLogIter *iter;
  guint count;
} TplLogWalkerHistoryData;


static TplLogWalkerAsyncData *
tpl_log_walker_async_data_new (void)
{
  return g_slice_new0 (TplLogWalkerAsyncData);
}


static void
tpl_log_walker_async_data_free (TplLogWalkerAsyncData *data)
{
  g_slice_free (TplLogWalkerAsyncData, data);
}


static TplLogWalkerHistoryData *
tpl_log_walker_history_data_new (void)
{
  return g_slice_new0 (TplLogWalkerHistoryData);
}


static void
tpl_log_walker_history_data_free (TplLogWalkerHistoryData *data)
{
  g_object_unref (data->iter);
  g_slice_free (TplLogWalkerHistoryData, data);
}


static void
tpl_log_walker_async_operation_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TplLogWalkerAsyncData *async_data = (TplLogWalkerAsyncData *) user_data;

  if (async_data->cb)
    async_data->cb (source_object, result, async_data->user_data);

  tpl_log_walker_async_data_free (async_data);
}


static void
tpl_log_walker_caches_free_func (gpointer data)
{
  g_list_free_full ((GList *) data, g_object_unref);
}


static GList *
tpl_log_walker_get_events (TplLogWalker *walker,
    guint num_events,
    GError **error)
{
  TplLogWalkerPriv *priv;
  GList *events;
  guint i;

  g_return_val_if_fail (TPL_IS_LOG_WALKER (walker), NULL);

  priv = walker->priv;
  events = NULL;
  i = 0;

  g_mutex_lock (&priv->mutex);

  if (priv->is_end == TRUE)
    goto out;

  while (i < num_events && priv->is_end == FALSE)
    {
      GList *k;
      GList *l;
      GList **latest_cache;
      GList *latest_event;
      TplLogIter *latest_iter;
      gint64 latest_timestamp;

      latest_cache = NULL;
      latest_event = NULL;
      latest_iter = NULL;
      latest_timestamp = 0;

      for (k = priv->caches, l = priv->iters;
           k != NULL && l != NULL;
           k = g_list_next (k), l = g_list_next (l))
        {
          GList **cache;
          GList *event;
          TplLogIter *iter;
          gint64 timestamp;

          cache = (GList **) &k->data;
          iter = TPL_LOG_ITER (l->data);

          /* If the cache is empty, try to fill it up. */
          if (*cache == NULL)
            *cache = tpl_log_iter_get_events (iter, CACHE_SIZE, error);

          /* If it could not be filled, then the store must be empty. */
          if (*cache == NULL)
            continue;

          event = g_list_last (*cache);
          timestamp = tpl_event_get_timestamp (TPL_EVENT (event->data));
          if (timestamp > latest_timestamp)
            {
              latest_cache = cache;
              latest_event = event;
              latest_iter = iter;
              latest_timestamp = timestamp;
            }
        }

      if (latest_event != NULL)
        {
          GList *h;
          TplLogWalkerHistoryData *data;

          events = g_list_prepend (events, latest_event->data);
          *latest_cache = g_list_delete_link (*latest_cache, latest_event);
          i++;

          h = priv->history;
          if (h == NULL ||
              ((TplLogWalkerHistoryData *) h->data)->iter != latest_iter)
            {
              data = tpl_log_walker_history_data_new ();
              data->iter = g_object_ref (latest_iter);
              priv->history = g_list_prepend (priv->history, data);
            }
          else
            data = (TplLogWalkerHistoryData *) h->data;

          data->count++;
        }
      else
        priv->is_end = TRUE;
    }

  /* We are still at the beginning if all the log stores were empty. */
  if (priv->history != NULL)
    priv->is_start = FALSE;

 out:
  g_mutex_unlock (&priv->mutex);

  return events;
}


static void
tpl_log_walker_get_events_async_thread (GSimpleAsyncResult *simple,
    GObject *object,
    GCancellable *cancellable)
{
  GError *error = NULL;
  GList *events;
  TplLogWalkerAsyncData *async_data;

  async_data = (TplLogWalkerAsyncData *) g_async_result_get_user_data (
      G_ASYNC_RESULT (simple));

  events = tpl_log_walker_get_events (TPL_LOG_WALKER (object),
      async_data->num_events, &error);

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (simple, error);
      g_error_free (error);
    }

  g_simple_async_result_set_op_res_gpointer (simple, events, NULL);
}


static void
tpl_log_walker_rewind (TplLogWalker *walker,
    guint num_events,
    GError **error)
{
  TplLogWalkerPriv *priv;
  GList *h;
  GList *k;
  GList *l;
  guint i;

  g_return_if_fail (TPL_IS_LOG_WALKER (walker));

  priv = walker->priv;
  i = 0;

  g_mutex_lock (&priv->mutex);

  if (priv->is_start == TRUE || num_events == 0)
    goto out;

  priv->is_end = FALSE;

  for (k = priv->caches, l = priv->iters;
       k != NULL && l != NULL;
       k = g_list_next (k), l = g_list_next (l))
    {
      GList **cache;
      TplLogIter *iter;
      guint length;

      cache = (GList **) &k->data;
      iter = TPL_LOG_ITER (l->data);

      /* Flush the cache. */
      length = g_list_length (*cache);
      tpl_log_iter_rewind (iter, length, error);
      g_list_free_full (*cache, g_object_unref);
      *cache = NULL;
    }

  h = priv->history;

  while (i < num_events && priv->is_start == FALSE)
    {
      TplLogWalkerHistoryData *data = (TplLogWalkerHistoryData *) h->data;

      tpl_log_iter_rewind (data->iter, 1, error);
      data->count--;
      i++;

      if (data->count == 0)
        {
          tpl_log_walker_history_data_free (data);
          priv->history = g_list_delete_link (priv->history, h);
          h = priv->history;
          if (h == NULL)
            priv->is_start = TRUE;
        }
    }

 out:
  g_mutex_unlock (&priv->mutex);
}


static void
tpl_log_walker_rewind_async_thread (GSimpleAsyncResult *simple,
    GObject *object,
    GCancellable *cancellable)
{
  GError *error = NULL;
  TplLogWalkerAsyncData *async_data;

  async_data = (TplLogWalkerAsyncData *) g_async_result_get_user_data (
      G_ASYNC_RESULT (simple));

  tpl_log_walker_rewind (TPL_LOG_WALKER (object),
      async_data->num_events, &error);

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (simple, error);
      g_error_free (error);
    }
}


static void
tpl_log_walker_dispose (GObject *object)
{
  TplLogWalkerPriv *priv;

  priv = TPL_LOG_WALKER (object)->priv;

  g_list_free_full (priv->caches, tpl_log_walker_caches_free_func);
  priv->caches = NULL;

  g_list_free_full (priv->history,
      (GDestroyNotify) tpl_log_walker_history_data_free);
  priv->history = NULL;

  g_list_free_full (priv->iters, g_object_unref);
  priv->iters = NULL;

  G_OBJECT_CLASS (tpl_log_walker_parent_class)->dispose (object);
}


static void
tpl_log_walker_finalize (GObject *object)
{
  TplLogWalkerPriv *priv;

  priv = TPL_LOG_WALKER (object)->priv;
  g_mutex_clear (&priv->mutex);

  G_OBJECT_CLASS (tpl_log_walker_parent_class)->finalize (object);
}


static void
tpl_log_walker_init (TplLogWalker *walker)
{
  TplLogWalkerPriv *priv;

  walker->priv = G_TYPE_INSTANCE_GET_PRIVATE (walker, TPL_TYPE_LOG_WALKER,
      TplLogWalkerPriv);
  priv = walker->priv;

  g_mutex_init (&priv->mutex);

  priv->is_start = TRUE;
  priv->is_end = FALSE;
}


static void
tpl_log_walker_class_init (TplLogWalkerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = tpl_log_walker_dispose;
  object_class->finalize = tpl_log_walker_finalize;

  g_type_class_add_private (klass, sizeof (TplLogWalkerPriv));
}


TplLogWalker *
tpl_log_walker_new (void)
{
  return g_object_new (TPL_TYPE_LOG_WALKER, NULL);
}


void
tpl_log_walker_add_iter (TplLogWalker *walker, TplLogIter *iter)
{
  TplLogWalkerPriv *priv;

  g_return_if_fail (TPL_IS_LOG_WALKER (walker));
  g_return_if_fail (TPL_IS_LOG_ITER (iter));

  priv = walker->priv;

  priv->iters = g_list_prepend (priv->iters, g_object_ref (iter));
  priv->caches = g_list_prepend (priv->caches, NULL);
}


/**
 * tpl_log_walker_get_events_async:
 * @walker: a #TplLogWalker
 * @num_events: number of maximum events to fetch
 * @callback: (scope async) (allow-none): a callback to call when
 * the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Walk the logs to retrieve the next most recent @num_event events.
 */
void
tpl_log_walker_get_events_async (TplLogWalker *walker,
    guint num_events,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  TplLogWalkerAsyncData *async_data;

  g_return_if_fail (TPL_IS_LOG_WALKER (walker));

  async_data = tpl_log_walker_async_data_new ();
  async_data->cb = callback;
  async_data->user_data = user_data;
  async_data->num_events = num_events;

  simple = g_simple_async_result_new (G_OBJECT (walker),
      tpl_log_walker_async_operation_cb, async_data,
      tpl_log_walker_get_events_async);

  g_simple_async_result_run_in_thread (simple,
      tpl_log_walker_get_events_async_thread, G_PRIORITY_DEFAULT,
      NULL);

  g_object_unref (simple);
}


/**
 * tpl_log_walker_get_events_finish:
 * @walker: a #TplLogWalker
 * @result: a #GAsyncResult
 * @events: (out) (transfer full) (element-type TelepathyLogger.Event):
 *  a pointer to a #GList used to return the list #TplEvent
 * @error: a #GError to fill
 *
 * Returns: #TRUE if the operation was successful, otherwise #FALSE.
 */
gboolean
tpl_log_walker_get_events_finish (TplLogWalker *walker,
    GAsyncResult *result,
    GList **events,
    GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (TPL_IS_LOG_WALKER (walker), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
        G_OBJECT (walker), tpl_log_walker_get_events_async), FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  if (events != NULL)
    *events = (GList *) g_simple_async_result_get_op_res_gpointer (simple);

  return TRUE;
}


/**
 * tpl_log_walker_rewind_async:
 * @walker: a #TplLogWalker
 * @num_events: number of events to move back
 * @callback: (scope async) (allow-none): a callback to call when
 * the request is satisfied
 * @user_data: data to pass to @callback
 *
 * Move the @walker back by the last @num_event events that were
 * returned by tpl_log_walker_get_events_async().
 */
void
tpl_log_walker_rewind_async (TplLogWalker *walker,
    guint num_events,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *simple;
  TplLogWalkerAsyncData *async_data;

  g_return_if_fail (TPL_IS_LOG_WALKER (walker));

  async_data = tpl_log_walker_async_data_new ();
  async_data->cb = callback;
  async_data->user_data = user_data;
  async_data->num_events = num_events;

  simple = g_simple_async_result_new (G_OBJECT (walker),
      tpl_log_walker_async_operation_cb, async_data,
      tpl_log_walker_rewind_async);

  g_simple_async_result_run_in_thread (simple,
      tpl_log_walker_rewind_async_thread, G_PRIORITY_DEFAULT,
      NULL);

  g_object_unref (simple);
}


/**
 * tpl_log_walker_rewind_finish:
 * @walker: a #TplLogWalker
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Returns: #TRUE if the operation was successful, otherwise #FALSE.
 */
gboolean
tpl_log_walker_rewind_finish (TplLogWalker *walker,
    GAsyncResult *result,
    GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (TPL_IS_LOG_WALKER (walker), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
        G_OBJECT (walker), tpl_log_walker_rewind_async), FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  return TRUE;
}


/**
 * tpl_log_walker_is_start:
 * @walker: a #TplLogWalker
 *
 * Determines whether @walker is pointing at the most recent event in
 * the logs. This is the case when @walker has not yet returned any
 * events or has been rewound completely.
 *
 * Returns: #TRUE if @walker is pointing at the most recent event,
 * otherwise #FALSE.
 */
gboolean
tpl_log_walker_is_start (TplLogWalker *walker)
{
  TplLogWalkerPriv *priv;
  gboolean retval;

  priv = walker->priv;

  g_mutex_lock (&priv->mutex);
  retval = priv->is_start;
  g_mutex_unlock (&priv->mutex);

  return retval;
}


/**
 * tpl_log_walker_is_end:
 * @walker: a #TplLogWalker
 *
 * Determines whether @walker has run out of events. This is the case
 * when @walker has returned all the events from the logs.
 *
 * Returns: #TRUE if @walker has run out of events, otherwise #FALSE.
 */
gboolean
tpl_log_walker_is_end (TplLogWalker *walker)
{
  TplLogWalkerPriv *priv;
  gboolean retval;

  priv = walker->priv;

  g_mutex_lock (&priv->mutex);
  retval = priv->is_end;
  g_mutex_unlock (&priv->mutex);

  return retval;
}