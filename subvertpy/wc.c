/*
 * Copyright © 2008 Jelmer Vernooij <jelmer@jelmer.uk>
 * -*- coding: utf-8 -*-
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#include <Python.h>
#include <apr_general.h>
#include <svn_wc.h>
#include <svn_path.h>
#include <svn_props.h>
#include <structmember.h>
#include <stdbool.h>
#include <apr_md5.h>
#include <apr_sha1.h>

#include "util.h"
#include "editor.h"
#include "wc.h"

#ifndef T_BOOL
#define T_BOOL T_BYTE
#endif

#if ONLY_BEFORE_SVN(1, 5)
struct svn_wc_committed_queue_t
{
	apr_pool_t *pool;
	apr_array_header_t *queue;
	svn_boolean_t have_recursive;
};

typedef struct
{
	const char *path;
	svn_wc_adm_access_t *adm_access;
	svn_boolean_t recurse;
	svn_boolean_t remove_lock;
	apr_array_header_t *wcprop_changes;
	unsigned char *digest;
} committed_queue_item_t;

svn_wc_committed_queue_t *svn_wc_committed_queue_create(apr_pool_t *pool)
{
	svn_wc_committed_queue_t *q;

	q = apr_palloc(pool, sizeof(*q));
	q->pool = pool;
	q->queue = apr_array_make(pool, 1, sizeof(committed_queue_item_t *));
	q->have_recursive = FALSE;

	return q;
}

svn_error_t *svn_wc_queue_committed(svn_wc_committed_queue_t **queue,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        svn_boolean_t recurse,
                        apr_array_header_t *wcprop_changes,
                        svn_boolean_t remove_lock,
                        svn_boolean_t remove_changelist,
                        const unsigned char *digest,
                        apr_pool_t *scratch_pool)
{
  committed_queue_item_t *cqi;

  (*queue)->have_recursive |= recurse;

  /* Use the same pool as the one QUEUE was allocated in,
     to prevent lifetime issues.  Intermediate operations
     should use SCRATCH_POOL. */

  /* Add to the array with paths and options */
  cqi = apr_palloc((*queue)->pool, sizeof(*cqi));
  cqi->path = path;
  cqi->adm_access = adm_access;
  cqi->recurse = recurse;
  cqi->remove_lock = remove_lock;
  cqi->wcprop_changes = wcprop_changes;
  cqi->digest = digest;

  APR_ARRAY_PUSH((*queue)->queue, committed_queue_item_t *) = cqi;

  return SVN_NO_ERROR;
}

#endif

typedef struct {
	PyObject_VAR_HEAD
	apr_pool_t *pool;
	svn_wc_committed_queue_t *queue;
} CommittedQueueObject;

svn_wc_committed_queue_t *PyObject_GetCommittedQueue(PyObject *obj)
{
    return ((CommittedQueueObject *)obj)->queue;
}

#if ONLY_SINCE_SVN(1, 5)
static svn_error_t *py_ra_report3_set_path(void *baton, const char *path, svn_revnum_t revision, svn_depth_t depth, int start_empty, const char *lock_token, apr_pool_t *pool)
{
	PyObject *self = (PyObject *)baton, *py_lock_token, *ret;
	PyGILState_STATE state = PyGILState_Ensure();
	if (lock_token == NULL) {
		py_lock_token = Py_None;
		Py_INCREF(py_lock_token);
	} else {
		py_lock_token = PyBytes_FromString(lock_token);
	}
	ret = PyObject_CallMethod(self, "set_path", "slbOi", path, revision, start_empty, py_lock_token, depth);
	Py_DECREF(py_lock_token);
	CB_CHECK_PYRETVAL(ret);
	Py_DECREF(ret);
	PyGILState_Release(state);
	return NULL;
}

static svn_error_t *py_ra_report3_link_path(void *report_baton, const char *path, const char *url, svn_revnum_t revision, svn_depth_t depth, int start_empty, const char *lock_token, apr_pool_t *pool)
{
	PyObject *self = (PyObject *)report_baton, *ret, *py_lock_token;
	PyGILState_STATE state = PyGILState_Ensure();
	if (lock_token == NULL) {
		py_lock_token = Py_None;
		Py_INCREF(py_lock_token);
	} else {
		py_lock_token = PyBytes_FromString(lock_token);
	}
	ret = PyObject_CallMethod(self, "link_path", "sslbOi", path, url, revision, start_empty, py_lock_token, depth);
	Py_DECREF(py_lock_token);
	CB_CHECK_PYRETVAL(ret);
	Py_DECREF(ret);
	PyGILState_Release(state);
	return NULL;
}

#endif

static svn_error_t *py_ra_report2_set_path(void *baton, const char *path, svn_revnum_t revision, int start_empty, const char *lock_token, apr_pool_t *pool)
{
	PyObject *self = (PyObject *)baton, *py_lock_token, *ret;
	PyGILState_STATE state = PyGILState_Ensure();
	if (lock_token == NULL) {
		py_lock_token = Py_None;
		Py_INCREF(py_lock_token);
	} else {
		py_lock_token = PyBytes_FromString(lock_token);
	}
	ret = PyObject_CallMethod(self, "set_path", "slbOi", path, revision, start_empty, py_lock_token, svn_depth_infinity);
	CB_CHECK_PYRETVAL(ret);
	Py_DECREF(ret);
	PyGILState_Release(state);
	return NULL;
}

static svn_error_t *py_ra_report2_link_path(void *report_baton, const char *path, const char *url, svn_revnum_t revision, int start_empty, const char *lock_token, apr_pool_t *pool)
{
	PyObject *self = (PyObject *)report_baton, *ret, *py_lock_token;
	PyGILState_STATE state = PyGILState_Ensure();
	if (lock_token == NULL) {
		py_lock_token = Py_None;
		Py_INCREF(py_lock_token);
	} else {
		py_lock_token = PyBytes_FromString(lock_token);
	}
	ret = PyObject_CallMethod(self, "link_path", "sslbOi", path, url, revision, start_empty, py_lock_token, svn_depth_infinity);
	CB_CHECK_PYRETVAL(ret);
	Py_DECREF(ret);
	PyGILState_Release(state);
	return NULL;
}

static svn_error_t *py_ra_report_delete_path(void *baton, const char *path, apr_pool_t *pool)
{
	PyObject *self = (PyObject *)baton, *ret;
	PyGILState_STATE state = PyGILState_Ensure();
	ret = PyObject_CallMethod(self, "delete_path", "s", path);
	CB_CHECK_PYRETVAL(ret);
	Py_DECREF(ret);
	PyGILState_Release(state);
	return NULL;
}

static svn_error_t *py_ra_report_finish(void *baton, apr_pool_t *pool)
{
	PyObject *self = (PyObject *)baton, *ret;
	PyGILState_STATE state = PyGILState_Ensure();
	ret = PyObject_CallMethod(self, "finish", "");
	CB_CHECK_PYRETVAL(ret);
	Py_DECREF(ret);
	PyGILState_Release(state);
	return NULL;
}

static svn_error_t *py_ra_report_abort(void *baton, apr_pool_t *pool)
{
	PyObject *self = (PyObject *)baton, *ret;
	PyGILState_STATE state = PyGILState_Ensure();
	ret = PyObject_CallMethod(self, "abort", "");
	CB_CHECK_PYRETVAL(ret);
	Py_DECREF(ret);
	PyGILState_Release(state);
	return NULL;
}

#if ONLY_SINCE_SVN(1, 5)
const svn_ra_reporter3_t py_ra_reporter3 = {
	py_ra_report3_set_path,
	py_ra_report_delete_path,
	py_ra_report3_link_path,
	py_ra_report_finish,
	py_ra_report_abort,
};
#endif

const svn_ra_reporter2_t py_ra_reporter2 = {
	py_ra_report2_set_path,
	py_ra_report_delete_path,
	py_ra_report2_link_path,
	py_ra_report_finish,
	py_ra_report_abort,
};


/**
 * Get runtime libsvn_wc version information.
 *
 * :return: tuple with major, minor, patch version number and tag.
 */
static PyObject *version(PyObject *self)
{
	const svn_version_t *ver = svn_wc_version();
	return Py_BuildValue("(iiis)", ver->major, ver->minor,
						 ver->patch, ver->tag);
}

SVN_VERSION_DEFINE(svn_api_version);

/**
 * Get compile-time libsvn_wc version information.
 *
 * :return: tuple with major, minor, patch version number and tag.
 */
static PyObject *api_version(PyObject *self)
{
	const svn_version_t *ver = &svn_api_version;
	return Py_BuildValue("(iiis)", ver->major, ver->minor,
						 ver->patch, ver->tag);
}


void py_wc_notify_func(void *baton, const svn_wc_notify_t *notify, apr_pool_t *pool)
{
	PyObject *func = baton, *ret;
	if (func == Py_None)
		return;

	if (notify->err != NULL) {
		PyObject *excval = PyErr_NewSubversionException(notify->err);
		ret = PyObject_CallFunction(func, "O", excval);
		Py_DECREF(excval);
		Py_XDECREF(ret);
		/* If ret was NULL, the cancel func should abort the operation. */
	}
}
bool py_dict_to_wcprop_changes(PyObject *dict, apr_pool_t *pool, apr_array_header_t **ret)
{
	PyObject *key, *val;
	Py_ssize_t idx;

	if (dict == Py_None) {
		*ret = NULL;
		return true;
	}

	if (!PyDict_Check(dict)) {
		PyErr_SetString(PyExc_TypeError, "Expected dictionary with property changes");
		return false;
	}

	*ret = apr_array_make(pool, PyDict_Size(dict), sizeof(char *));

	while (PyDict_Next(dict, &idx, &key, &val)) {
		svn_prop_t *prop = apr_palloc(pool, sizeof(svn_prop_t));
		prop->name = py_object_to_svn_string(key, pool);
		if (prop->name == NULL) {
			return false;
		}
		if (val == Py_None) {
			prop->value = NULL;
		} else {
			if (!PyBytes_Check(val)) {
				PyErr_SetString(PyExc_TypeError, "property values should be bytes");
				return false;
			}
			prop->value = svn_string_ncreate(PyBytes_AsString(val), PyBytes_Size(val), pool);
		}
		APR_ARRAY_PUSH(*ret, svn_prop_t *) = prop;
	}

	return true;
}

#if ONLY_SINCE_SVN(1, 6)
svn_error_t *wc_validator3(void *baton, const char *uuid, const char *url, const char *root_url, apr_pool_t *pool)
{
	PyObject *py_validator = baton, *ret;

	if (py_validator == Py_None) {
		return NULL;
	}

	ret = PyObject_CallFunction(py_validator, "sss", uuid, url, root_url);
	if (ret == NULL) {
		return py_svn_error();
	}

	Py_DECREF(ret);

	return NULL;
}

#endif

svn_error_t *wc_validator2(void *baton, const char *uuid, const char *url, svn_boolean_t root, apr_pool_t *pool)
{
	PyObject *py_validator = baton, *ret;

	if (py_validator == Py_None) {
		return NULL;
	}

	ret = PyObject_CallFunction(py_validator, "ssO", uuid, url, Py_None);
	if (ret == NULL) {
		return py_svn_error();
	}

	Py_DECREF(ret);

	return NULL;
}

static PyObject *get_actual_target(PyObject *self, PyObject *args)
{
	const char *path;
	const char *anchor = NULL, *target = NULL;
	apr_pool_t *temp_pool;
	PyObject *ret, *py_path;

	if (!PyArg_ParseTuple(args, "O", &py_path))
		return NULL;

	temp_pool = Pool(NULL);
	if (temp_pool == NULL) {
		return NULL;
	}

	path = py_object_to_svn_dirent(py_path, temp_pool);
	if (path == NULL) {
		apr_pool_destroy(temp_pool);
		return NULL;
	}

	RUN_SVN_WITH_POOL(temp_pool,
		  svn_wc_get_actual_target(path,
								   &anchor, &target, temp_pool));

	ret = Py_BuildValue("(ss)", anchor, target);

	apr_pool_destroy(temp_pool);

	return ret;
}

/**
 * Determine the revision status of a specified working copy.
 *
 * :return: Tuple with minimum and maximum revnums found, whether the
 * working copy was switched and whether it was modified.
 */
static PyObject *revision_status(PyObject *self, PyObject *args, PyObject *kwargs)
{
	char *kwnames[] = { "wc_path", "trail_url", "committed",  NULL };
	const char *wc_path;
	char *trail_url=NULL;
	bool committed=false;
	PyObject *ret, *py_wc_path;
	svn_wc_revision_status_t *revstatus;
	apr_pool_t *temp_pool;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|zb", kwnames, &py_wc_path,
									 &trail_url, &committed))
		return NULL;

	temp_pool = Pool(NULL);
	if (temp_pool == NULL) {
		return NULL;
	}

	wc_path = py_object_to_svn_dirent(py_wc_path, temp_pool);
	if (wc_path == NULL) {
		apr_pool_destroy(temp_pool);
		return NULL;
	}
	RUN_SVN_WITH_POOL(temp_pool,
			svn_wc_revision_status(
				&revstatus, wc_path, trail_url,
				 committed, py_cancel_check, NULL, temp_pool));
	ret = Py_BuildValue("(llbb)", revstatus->min_rev, revstatus->max_rev,
			revstatus->switched, revstatus->modified);
	apr_pool_destroy(temp_pool);
	return ret;
}

static PyObject *is_normal_prop(PyObject *self, PyObject *args)
{
	char *name;

	if (!PyArg_ParseTuple(args, "s", &name))
		return NULL;

	return PyBool_FromLong(svn_wc_is_normal_prop(name));
}

static PyObject *is_adm_dir(PyObject *self, PyObject *args)
{
	char *name;
	apr_pool_t *pool;
	svn_boolean_t ret;

	if (!PyArg_ParseTuple(args, "s", &name))
		return NULL;

	pool = Pool(NULL);
	if (pool == NULL)
		return NULL;

	ret = svn_wc_is_adm_dir(name, pool);

	apr_pool_destroy(pool);

	return PyBool_FromLong(ret);
}

static PyObject *is_wc_prop(PyObject *self, PyObject *args)
{
	char *name;

	if (!PyArg_ParseTuple(args, "s", &name))
		return NULL;

	return PyBool_FromLong(svn_wc_is_wc_prop(name));
}

static PyObject *is_entry_prop(PyObject *self, PyObject *args)
{
	char *name;

	if (!PyArg_ParseTuple(args, "s", &name))
		return NULL;

	return PyBool_FromLong(svn_wc_is_entry_prop(name));
}

static PyObject *get_adm_dir(PyObject *self)
{
	apr_pool_t *pool;
	PyObject *ret;
	const char *dir;
	pool = Pool(NULL);
	if (pool == NULL)
		return NULL;
	dir = svn_wc_get_adm_dir(pool);
	ret = py_object_from_svn_abspath(dir);
	apr_pool_destroy(pool);
	return ret;
}

static PyObject *set_adm_dir(PyObject *self, PyObject *args)
{
	apr_pool_t *temp_pool;
	char *name;
	PyObject *py_name;

	if (!PyArg_ParseTuple(args, "O", &py_name))
		return NULL;

	temp_pool = Pool(NULL);
	if (temp_pool == NULL)
		return NULL;
	name = py_object_to_svn_string(py_name, temp_pool);
	if (name == NULL) {
		apr_pool_destroy(temp_pool);
		return NULL;
	}
	RUN_SVN_WITH_POOL(temp_pool, svn_wc_set_adm_dir(name, temp_pool));
	apr_pool_destroy(temp_pool);
	Py_RETURN_NONE;
}

static PyObject *get_pristine_copy_path(PyObject *self, PyObject *args)
{
	apr_pool_t *pool;
	const char *pristine_path;
	const char *path;
	PyObject *py_path;
	PyObject *ret;

	if (!PyArg_ParseTuple(args, "O", &py_path))
		return NULL;

	pool = Pool(NULL);
	if (pool == NULL)
		return NULL;

	path = py_object_to_svn_dirent(py_path, pool);
	if (path == NULL) {
		apr_pool_destroy(pool);
		return NULL;
	}

	PyErr_WarnEx(PyExc_DeprecationWarning, "get_pristine_copy_path is deprecated. Use get_pristine_contents instead.", 2);
	RUN_SVN_WITH_POOL(pool,
		  svn_wc_get_pristine_copy_path(path,
										&pristine_path, pool));
	ret = py_object_from_svn_abspath(pristine_path);
	apr_pool_destroy(pool);
	return ret;
}

static PyObject *get_pristine_contents(PyObject *self, PyObject *args)
{
	const char *path;
	apr_pool_t *temp_pool;
	PyObject *py_path;
#if ONLY_SINCE_SVN(1, 6)
	apr_pool_t *stream_pool;
	StreamObject *ret;
	svn_stream_t *stream;
#else
	PyObject *ret;
	const char *pristine_path;
#endif

	if (!PyArg_ParseTuple(args, "O", &py_path))
		return NULL;

#if ONLY_SINCE_SVN(1, 6)
	stream_pool = Pool(NULL);
	if (stream_pool == NULL)
		return NULL;

	temp_pool = Pool(stream_pool);
	if (temp_pool == NULL) {
		apr_pool_destroy(stream_pool);
		return NULL;
	}

	path = py_object_to_svn_dirent(py_path, temp_pool);
	if (path == NULL) {
		apr_pool_destroy(temp_pool);
		return NULL;
	}

	RUN_SVN_WITH_POOL(stream_pool, svn_wc_get_pristine_contents(&stream, path, stream_pool, temp_pool));
	apr_pool_destroy(temp_pool);

	if (stream == NULL) {
		apr_pool_destroy(stream_pool);
		Py_RETURN_NONE;
	}

	ret = PyObject_New(StreamObject, &Stream_Type);
	if (ret == NULL)
		return NULL;

	ret->pool = stream_pool;
	ret->closed = FALSE;
	ret->stream = stream;

	return (PyObject *)ret;
#else
	temp_pool = Pool(NULL);
	if (temp_pool == NULL)
		return NULL;
	RUN_SVN_WITH_POOL(temp_pool, svn_wc_get_pristine_copy_path(path, &pristine_path, temp_pool));
	ret = PyFile_FromString((char *)pristine_path, "rb");
	apr_pool_destroy(temp_pool);
	return ret;
#endif
}

static PyObject *ensure_adm(PyObject *self, PyObject *args, PyObject *kwargs)
{
	const char *path;
	char *uuid, *url;
	PyObject *py_path;
	char *repos=NULL;
	svn_revnum_t rev=-1;
	apr_pool_t *pool;
	char *kwnames[] = { "path", "uuid", "url", "repos", "rev", "depth", NULL };
	svn_depth_t depth = svn_depth_infinity;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oss|sli", kwnames,
									 &py_path, &uuid, &url, &repos, &rev, &depth))
		return NULL;

	pool = Pool(NULL);
	if (pool == NULL) {
		return NULL;
	}

	path = py_object_to_svn_dirent(py_path, pool);
	if (path == NULL) {
		apr_pool_destroy(pool);
		return NULL;
	}

#if ONLY_SINCE_SVN(1, 5)
	RUN_SVN_WITH_POOL(pool,
					  svn_wc_ensure_adm3(path,
										 uuid, url, repos, rev, depth, pool));
#else
	if (depth != svn_depth_infinity) {
		PyErr_SetString(PyExc_NotImplementedError,
						"depth != infinity not supported with svn < 1.5");
		apr_pool_destroy(pool);
		return NULL;
	}
	RUN_SVN_WITH_POOL(pool,
					  svn_wc_ensure_adm2(path,
										 uuid, url, repos, rev, pool));
#endif
	apr_pool_destroy(pool);
	Py_RETURN_NONE;
}

static PyObject *check_wc(PyObject *self, PyObject *args)
{
	const char *path;
	apr_pool_t *pool;
	int wc_format;
	PyObject *py_path;

	if (!PyArg_ParseTuple(args, "O", &py_path))
		return NULL;

	pool = Pool(NULL);
	if (pool == NULL) {
		return NULL;
	}

	path = py_object_to_svn_dirent(py_path, pool);
	if (path == NULL) {
		apr_pool_destroy(pool);
		return NULL;
	}

	RUN_SVN_WITH_POOL(pool, svn_wc_check_wc(path, &wc_format, pool));
	apr_pool_destroy(pool);
	return PyLong_FromLong(wc_format);
}

static PyObject *cleanup_wc(PyObject *self, PyObject *args, PyObject *kwargs)
{
	const char *path;
	char *diff3_cmd = NULL;
	char *kwnames[] = { "path", "diff3_cmd", NULL };
	apr_pool_t *temp_pool;
	PyObject *py_path;

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|z", kwnames,
									 &py_path, &diff3_cmd))
		return NULL;

	temp_pool = Pool(NULL);
	if (temp_pool == NULL) {
		return NULL;
	}

	path = py_object_to_svn_dirent(py_path, temp_pool);
	if (path == NULL) {
		apr_pool_destroy(temp_pool);
		return NULL;
	}

	RUN_SVN_WITH_POOL(temp_pool,
				svn_wc_cleanup2(path, diff3_cmd, py_cancel_check, NULL,
								temp_pool));

	apr_pool_destroy(temp_pool);

	Py_RETURN_NONE;
}

static PyObject *match_ignore_list(PyObject *self, PyObject *args)
{
#if ONLY_SINCE_SVN(1, 5)
	char *str;
	PyObject *py_list;
	apr_array_header_t *list;
	apr_pool_t *temp_pool;
	svn_boolean_t ret;

	if (!PyArg_ParseTuple(args, "sO", &str, &py_list))
		return NULL;

	temp_pool = Pool(NULL);

	if (!string_list_to_apr_array(temp_pool, py_list, &list)) {
		apr_pool_destroy(temp_pool);
		return NULL;
	}

	ret = svn_wc_match_ignore_list(str, list, temp_pool);

	apr_pool_destroy(temp_pool);

	return PyBool_FromLong(ret);
#else
	PyErr_SetNone(PyExc_NotImplementedError);
	return NULL;
#endif
}

static PyMethodDef wc_methods[] = {
	{ "check_wc", check_wc, METH_VARARGS, "check_wc(path) -> version\n"
		"Check whether path contains a Subversion working copy\n"
		"return the workdir version"},
	{ "cleanup", (PyCFunction)cleanup_wc, METH_VARARGS|METH_KEYWORDS, "cleanup(path, diff3_cmd=None)\n" },
	{ "ensure_adm", (PyCFunction)ensure_adm, METH_KEYWORDS|METH_VARARGS,
		"ensure_adm(path, uuid, url, repos=None, rev=None)" },
	{ "get_adm_dir", (PyCFunction)get_adm_dir, METH_NOARGS,
		"get_adm_dir() -> name" },
	{ "set_adm_dir", (PyCFunction)set_adm_dir, METH_VARARGS,
		"set_adm_dir(name)" },
	{ "get_pristine_copy_path", get_pristine_copy_path, METH_VARARGS,
		"get_pristine_copy_path(path) -> path" },
	{ "get_pristine_contents", get_pristine_contents, METH_VARARGS,
		"get_pristine_contents(path) -> stream" },
	{ "is_adm_dir", is_adm_dir, METH_VARARGS,
		"is_adm_dir(name) -> bool" },
	{ "is_normal_prop", is_normal_prop, METH_VARARGS,
		"is_normal_prop(name) -> bool" },
	{ "is_entry_prop", is_entry_prop, METH_VARARGS,
		"is_entry_prop(name) -> bool" },
	{ "is_wc_prop", is_wc_prop, METH_VARARGS,
		"is_wc_prop(name) -> bool" },
	{ "revision_status", (PyCFunction)revision_status, METH_KEYWORDS|METH_VARARGS, "revision_status(wc_path, trail_url=None, committed=False) -> (min_rev, max_rev, switched, modified)" },
	{ "version", (PyCFunction)version, METH_NOARGS,
		"version() -> (major, minor, patch, tag)\n\n"
		"Version of libsvn_wc currently used."
	},
	{ "api_version", (PyCFunction)api_version, METH_NOARGS,
		"api_version() -> (major, minor, patch, tag)\n\n"
		"Version of libsvn_wc Subvertpy was compiled against."
	},
	{ "match_ignore_list", (PyCFunction)match_ignore_list, METH_VARARGS,
		"match_ignore_list(str, patterns) -> bool" },
	{ "get_actual_target", (PyCFunction)get_actual_target, METH_VARARGS,
		"get_actual_target(path) -> (anchor, target)" },
	{ NULL, }
};

static void committed_queue_dealloc(PyObject *self)
{
	apr_pool_destroy(((CommittedQueueObject *)self)->pool);
	PyObject_Del(self);
}

static PyObject *committed_queue_repr(PyObject *self)
{
	CommittedQueueObject *cqobj = (CommittedQueueObject *)self;

	return PyRepr_FromFormat("<wc.CommittedQueue at 0x%p>", cqobj->queue);
}

static PyObject *committed_queue_init(PyTypeObject *self, PyObject *args, PyObject *kwargs)
{
	CommittedQueueObject *ret;
	char *kwnames[] = { NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "", kwnames))
		return NULL;

	ret = PyObject_New(CommittedQueueObject, &CommittedQueue_Type);
	if (ret == NULL)
		return NULL;

	ret->pool = Pool(NULL);
	if (ret->pool == NULL)
		return NULL;
	ret->queue = svn_wc_committed_queue_create(ret->pool);
	if (ret->queue == NULL) {
		PyObject_Del(ret);
		PyErr_NoMemory();
		return NULL;
	}

	return (PyObject *)ret;
}

static PyObject *committed_queue_queue(CommittedQueueObject *self, PyObject *args, PyObject *kwargs)
{
	char *path;
	PyObject *admobj;
	PyObject *py_wcprop_changes = Py_None;
    svn_wc_adm_access_t *adm;
	bool remove_lock = false, remove_changelist = false;
	char *md5_digest = NULL, *sha1_digest = NULL;
	bool recurse = false;
	apr_pool_t *temp_pool;
	apr_array_header_t *wcprop_changes;
	int md5_digest_len, sha1_digest_len;
	char *kwnames[] = { "path", "adm", "recurse", "wcprop_changes", "remove_lock", "remove_changelist", "md5_digest", "sha1_digest", NULL };

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sO!|bObbz#z#", kwnames,
									 &path, &Adm_Type, &admobj,
						  &recurse, &py_wcprop_changes, &remove_lock,
						  &remove_changelist, &md5_digest, &md5_digest_len,
						  &sha1_digest, &sha1_digest_len))
		return NULL;

	temp_pool = Pool(NULL);
	if (temp_pool == NULL)
		return NULL;

	if (!py_dict_to_wcprop_changes(py_wcprop_changes, self->pool, &wcprop_changes)) {
		apr_pool_destroy(temp_pool);
		return NULL;
	}

	path = apr_pstrdup(self->pool, path);
	if (path == NULL) {
		PyErr_NoMemory();
		return NULL;
	}

	if (md5_digest != NULL) {
		if (md5_digest_len != APR_MD5_DIGESTSIZE) {
			PyErr_SetString(PyExc_ValueError, "Invalid size for md5 digest");
			apr_pool_destroy(temp_pool);
			return NULL;
		}
		md5_digest = apr_pstrdup(temp_pool, md5_digest);
		if (md5_digest == NULL) {
			PyErr_NoMemory();
			return NULL;
		}
	}

	if (sha1_digest != NULL) {
		if (sha1_digest_len != APR_SHA1_DIGESTSIZE) {
			PyErr_SetString(PyExc_ValueError, "Invalid size for sha1 digest");
			apr_pool_destroy(temp_pool);
			return NULL;
		}
		sha1_digest = apr_pstrdup(temp_pool, sha1_digest);
		if (sha1_digest == NULL) {
			PyErr_NoMemory();
			return NULL;
		}
	}

    adm = PyObject_GetAdmAccess(admobj);

#if ONLY_SINCE_SVN(1, 6)
	{
	svn_checksum_t svn_checksum, *svn_checksum_p = &svn_checksum;

	if (sha1_digest != NULL) {
		svn_checksum.digest = (unsigned char *)sha1_digest;
		svn_checksum.kind = svn_checksum_sha1;
	} else if (md5_digest != NULL) {
		svn_checksum.digest = (unsigned char *)md5_digest;
		svn_checksum.kind = svn_checksum_md5;
	} else {
		svn_checksum_p = NULL;
	}
	RUN_SVN_WITH_POOL(temp_pool,
		svn_wc_queue_committed2(self->queue, path, adm, recurse?TRUE:FALSE,
							   wcprop_changes, remove_lock?TRUE:FALSE, remove_changelist?TRUE:FALSE,
							   svn_checksum_p, temp_pool));
	}
#else
	RUN_SVN_WITH_POOL(temp_pool,
		svn_wc_queue_committed(&self->queue, path, adm, recurse?TRUE:FALSE,
							   wcprop_changes, remove_lock?TRUE:FALSE, remove_changelist?TRUE:FALSE,
							   (unsigned char *)md5_digest, temp_pool));
#endif

	apr_pool_destroy(temp_pool);

	Py_RETURN_NONE;
}

static PyMethodDef committed_queue_methods[] = {
	{ "queue", (PyCFunction)committed_queue_queue, METH_VARARGS|METH_KEYWORDS,
		"S.queue(path, adm, recurse=False, wcprop_changes=[], remove_lock=False, remove_changelist=False, digest=None)" },
	{ NULL }
};

PyTypeObject CommittedQueue_Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"wc.CommittedQueue", /*	const char *tp_name;  For printing, in format "<module>.<name>" */
	sizeof(CommittedQueueObject),
	0,/*	Py_ssize_t tp_basicsize, tp_itemsize;  For allocation */

	/* Methods to implement standard operations */

	committed_queue_dealloc, /*	destructor tp_dealloc;	*/
	NULL, /*	printfunc tp_print;	*/
	NULL, /*	getattrfunc tp_getattr;	*/
	NULL, /*	setattrfunc tp_setattr;	*/
	NULL, /*	cmpfunc tp_compare;	*/
	committed_queue_repr, /*	reprfunc tp_repr;	*/

	/* Method suites for standard classes */

	NULL, /*	PyNumberMethods *tp_as_number;	*/
	NULL, /*	PySequenceMethods *tp_as_sequence;	*/
	NULL, /*	PyMappingMethods *tp_as_mapping;	*/

	/* More standard operations (here for binary compatibility) */

	NULL, /*	hashfunc tp_hash;	*/
	NULL, /*	ternaryfunc tp_call;	*/
	NULL, /*	reprfunc tp_str;	*/
	NULL, /*	getattrofunc tp_getattro;	*/
	NULL, /*	setattrofunc tp_setattro;	*/

	/* Functions to access object as input/output buffer */
	NULL, /*	PyBufferProcs *tp_as_buffer;	*/

	/* Flags to define presence of optional/expanded features */
	0, /*	long tp_flags;	*/

	"Committed queue", /*	const char *tp_doc;  Documentation string */

	/* Assigned meaning in release 2.0 */
	/* call function for all accessible objects */
	NULL, /*	traverseproc tp_traverse;	*/

	/* delete references to contained objects */
	NULL, /*	inquiry tp_clear;	*/

	/* Assigned meaning in release 2.1 */
	/* rich comparisons */
	NULL, /*	richcmpfunc tp_richcompare;	*/

	/* weak reference enabler */
	0, /*	Py_ssize_t tp_weaklistoffset;	*/

	/* Added in release 2.2 */
	/* Iterators */
	NULL, /*	getiterfunc tp_iter;	*/
	NULL, /*	iternextfunc tp_iternext;	*/

	/* Attribute descriptor and subclassing stuff */
	committed_queue_methods, /*	struct PyMethodDef *tp_methods;	*/
	NULL, /*	struct PyMemberDef *tp_members;	*/
	NULL, /*	struct PyGetSetDef *tp_getset;	*/
	NULL, /*	struct _typeobject *tp_base;	*/
	NULL, /*	PyObject *tp_dict;	*/
	NULL, /*	descrgetfunc tp_descr_get;	*/
	NULL, /*	descrsetfunc tp_descr_set;	*/
	0, /*	Py_ssize_t tp_dictoffset;	*/
	NULL, /*	initproc tp_init;	*/
	NULL, /*	allocfunc tp_alloc;	*/
	committed_queue_init, /*	newfunc tp_new;	*/
};



static PyObject *
moduleinit(void)
{
	PyObject *mod;

	if (PyType_Ready(&Entry_Type) < 0)
		return NULL;

	if (PyType_Ready(&Status_Type) < 0)
		return NULL;

	if (PyType_Ready(&Adm_Type) < 0)
		return NULL;

	if (PyType_Ready(&Editor_Type) < 0)
		return NULL;

	if (PyType_Ready(&FileEditor_Type) < 0)
		return NULL;

	if (PyType_Ready(&DirectoryEditor_Type) < 0)
		return NULL;

	if (PyType_Ready(&TxDeltaWindowHandler_Type) < 0)
		return NULL;

	if (PyType_Ready(&Stream_Type) < 0)
		return NULL;

	if (PyType_Ready(&CommittedQueue_Type) < 0)
		return NULL;

	apr_initialize();

#if PY_MAJOR_VERSION >= 3
	static struct PyModuleDef moduledef = {
	  PyModuleDef_HEAD_INIT,
	  "wc",         /* m_name */
	  "Working Copies", /* m_doc */
	  -1,              /* m_size */
	  wc_methods, /* m_methods */
	  NULL,            /* m_reload */
	  NULL,            /* m_traverse */
	  NULL,            /* m_clear*/
	  NULL,            /* m_free */
	};
	mod = PyModule_Create(&moduledef);
#else
	mod = Py_InitModule3("wc", wc_methods, "Working Copies");
#endif
	if (mod == NULL)
		return NULL;

	PyModule_AddIntConstant(mod, "SCHEDULE_NORMAL", 0);
	PyModule_AddIntConstant(mod, "SCHEDULE_ADD", 1);
	PyModule_AddIntConstant(mod, "SCHEDULE_DELETE", 2);
	PyModule_AddIntConstant(mod, "SCHEDULE_REPLACE", 3);

#if ONLY_SINCE_SVN(1, 5)
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_POSTPONE",
							svn_wc_conflict_choose_postpone);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_BASE",
							svn_wc_conflict_choose_base);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_THEIRS_FULL",
							svn_wc_conflict_choose_theirs_full);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_MINE_FULL",
							svn_wc_conflict_choose_mine_full);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_THEIRS_CONFLICT",
							svn_wc_conflict_choose_theirs_conflict);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_MINE_CONFLICT",
							svn_wc_conflict_choose_mine_conflict);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_MERGED",
							svn_wc_conflict_choose_merged);
#endif

	PyModule_AddIntConstant(mod, "STATUS_NONE", svn_wc_status_none);
	PyModule_AddIntConstant(mod, "STATUS_UNVERSIONED", svn_wc_status_unversioned);
	PyModule_AddIntConstant(mod, "STATUS_NORMAL", svn_wc_status_normal);
	PyModule_AddIntConstant(mod, "STATUS_ADDED", svn_wc_status_added);
	PyModule_AddIntConstant(mod, "STATUS_MISSING", svn_wc_status_missing);
	PyModule_AddIntConstant(mod, "STATUS_DELETED", svn_wc_status_deleted);
	PyModule_AddIntConstant(mod, "STATUS_REPLACED", svn_wc_status_replaced);
	PyModule_AddIntConstant(mod, "STATUS_MODIFIED", svn_wc_status_modified);
	PyModule_AddIntConstant(mod, "STATUS_MERGED", svn_wc_status_merged);
	PyModule_AddIntConstant(mod, "STATUS_CONFLICTED", svn_wc_status_conflicted);
	PyModule_AddIntConstant(mod, "STATUS_IGNORED", svn_wc_status_ignored);
	PyModule_AddIntConstant(mod, "STATUS_OBSTRUCTED", svn_wc_status_obstructed);
	PyModule_AddIntConstant(mod, "STATUS_EXTERNAL", svn_wc_status_external);
	PyModule_AddIntConstant(mod, "STATUS_INCOMPLETE", svn_wc_status_incomplete);

	PyModule_AddIntConstant(mod, "TRANSLATE_FROM_NF", SVN_WC_TRANSLATE_FROM_NF);
	PyModule_AddIntConstant(mod, "TRANSLATE_TO_NF", SVN_WC_TRANSLATE_TO_NF);
	PyModule_AddIntConstant(mod, "TRANSLATE_FORCE_EOL_REPAIR", SVN_WC_TRANSLATE_FORCE_EOL_REPAIR);
	PyModule_AddIntConstant(mod, "TRANSLATE_NO_OUTPUT_CLEANUP", SVN_WC_TRANSLATE_NO_OUTPUT_CLEANUP);
	PyModule_AddIntConstant(mod, "TRANSLATE_FORCE_COPY", SVN_WC_TRANSLATE_FORCE_COPY);
	PyModule_AddIntConstant(mod, "TRANSLATE_USE_GLOBAL_TMP", SVN_WC_TRANSLATE_USE_GLOBAL_TMP);

#if ONLY_SINCE_SVN(1, 5)
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_POSTPONE", svn_wc_conflict_choose_postpone);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_BASE", svn_wc_conflict_choose_base);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_THEIRS_FULL", svn_wc_conflict_choose_theirs_full);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_MINE_FULL", svn_wc_conflict_choose_mine_full);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_THEIRS_CONFLICT", svn_wc_conflict_choose_theirs_conflict);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_MINE_CONFLICT", svn_wc_conflict_choose_mine_conflict);
	PyModule_AddIntConstant(mod, "CONFLICT_CHOOSE_MERGED", svn_wc_conflict_choose_merged);
#endif

	PyModule_AddObject(mod, "Adm", (PyObject *)&Adm_Type);
	Py_INCREF(&Adm_Type);

	PyModule_AddObject(mod, "CommittedQueue", (PyObject *)&CommittedQueue_Type);
	Py_INCREF(&CommittedQueue_Type);

	return mod;
}

#if PY_MAJOR_VERSION >= 3
PyMODINIT_FUNC
PyInit_wc(void)
{
	return moduleinit();
}
#else
PyMODINIT_FUNC
initwc(void)
{
	moduleinit();
}
#endif
