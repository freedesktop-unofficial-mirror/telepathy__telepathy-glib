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

#ifndef __TPL_UTIL_H__
#define __TPL_UTIL_H__

#include <glib-object.h>
#include <gio/gio.h>

#define TPL_GET_PRIV(obj,type) ((type##Priv *) ((type *) obj)->priv)
#define TPL_STR_EMPTY(x) ((x) == NULL || (x)[0] == '\0')

#define tpl_call_with_err_if_fail(guard, obj, PREFIX, POSTFIX, msg, func, user_data) \
  if (!(guard)) \
    { \
      if (func != NULL) \
        { \
          GSimpleAsyncResult *result=NULL; \
          g_simple_async_result_set_error (result, PREFIX ## _ERROR, \
              PREFIX ## _ERROR_ ## POSTFIX, \
              msg); \
          return func (G_OBJECT (obj), G_ASYNC_RESULT (result), user_data); \
        } \
      return; \
    }

#define tpl_object_unref_if_not_null(obj) if (obj != NULL && G_IS_OBJECT (obj)) \
                                            g_object_unref (obj);
#define tpl_object_ref_if_not_null(obj) if (obj != NULL && G_IS_OBJECT (obj)) \
                                            g_object_ref (obj);


gboolean tpl_strequal (const gchar *left, const gchar *right);

#endif // __TPL_UTIL_H__
