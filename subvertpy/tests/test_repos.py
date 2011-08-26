# Copyright (C) 2005-2007 Jelmer Vernooij <jelmer@samba.org>
 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.

# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""Subversion repository library tests."""

from cStringIO import StringIO
import os
import textwrap

from subvertpy import repos, SubversionException
from subvertpy.tests import TestCaseInTempDir, TestCase


class VersionTest(TestCase):

    def test_version_length(self):
        self.assertEquals(4, len(repos.version()))

    def test_api_version_length(self):
        self.assertEquals(4, len(repos.api_version()))

    def test_api_version_later_same(self):
        self.assertTrue(repos.api_version() <= repos.version())


class TestClient(TestCaseInTempDir):

    def setUp(self):
        super(TestClient, self).setUp()

    def test_create(self):
        repos.create(os.path.join(self.test_dir, "foo"))

    def test_capability(self):
        r = repos.create(os.path.join(self.test_dir, "foo"))
        if repos.api_version() < (1, 5):
            self.assertRaises(NotImplementedError, r.has_capability, "mergeinfo")
        else:
            self.assertIsInstance(r.has_capability("mergeinfo"), bool)

    def test_open(self):
        repos.create(os.path.join(self.test_dir, "foo"))
        repos.Repository("foo")

    def test_uuid(self):
        repos.create(os.path.join(self.test_dir, "foo"))
        self.assertIsInstance(repos.Repository("foo").fs().get_uuid(), str)

    def test_youngest_rev(self):
        repos.create(os.path.join(self.test_dir, "foo"))
        self.assertEquals(0, repos.Repository("foo").fs().youngest_revision())

    def test_rev_root(self):
        repos.create(os.path.join(self.test_dir, "foo"))
        self.assertTrue(repos.Repository("foo").fs().revision_root(0) is not None)

    def test_load_fs_invalid(self):
        r = repos.create(os.path.join(self.test_dir, "foo"))
        dumpfile = "Malformed"
        feedback = StringIO()
        self.assertRaises(SubversionException, r.load_fs, StringIO(dumpfile),
            feedback, repos.LOAD_UUID_DEFAULT)

    def test_load_fs(self):
        r = repos.create(os.path.join(self.test_dir, "foo"))
        dumpfile = textwrap.dedent("""\
        SVN-fs-dump-format-version: 2

        UUID: 38f0a982-fd1f-4e00-aa6b-a20720f4b9ca

        Revision-number: 0
        Prop-content-length: 56
        Content-length: 56

        K 8
        svn:date
        V 27
        2011-08-26T13:08:30.187858Z
        PROPS-END
        """)
        feedback = StringIO()
        r.load_fs(StringIO(dumpfile), feedback, repos.LOAD_UUID_DEFAULT)
        self.assertEquals(r.fs().get_uuid(), "38f0a982-fd1f-4e00-aa6b-a20720f4b9ca")

    def test_rev_props(self):
        repos.create(os.path.join(self.test_dir, "foo"))
        self.assertEquals(["svn:date"], repos.Repository("foo").fs().revision_proplist(0).keys())

    def test_rev_root_invalid(self):
        repos.create(os.path.join(self.test_dir, "foo"))
        self.assertRaises(SubversionException, repos.Repository("foo").fs().revision_root, 1)

    def test_paths_changed(self):
        repos.create(os.path.join(self.test_dir, "foo"))
        root = repos.Repository("foo").fs().revision_root(0)
        self.assertEquals({}, root.paths_changed())

    def test_is_dir(self):
        repos.create(os.path.join(self.test_dir, "foo"))
        root = repos.Repository("foo").fs().revision_root(0)
        self.assertEquals(True, root.is_dir(""))
        self.assertEquals(False, root.is_dir("nonexistant"))

    def test_is_file(self):
        repos.create(os.path.join(self.test_dir, "foo"))
        root = repos.Repository("foo").fs().revision_root(0)
        self.assertEquals(False, root.is_file(""))
        self.assertEquals(False, root.is_file("nonexistant"))


class StreamTests(TestCase):

    def test_read(self):
        s = repos.Stream()
        if repos.api_version() < (1, 6):
            self.assertRaises(NotImplementedError, s.read)
        else:
            self.assertEquals("", s.read())
            self.assertEquals("", s.read(15))
            s.close()

    def test_write(self):
        s = repos.Stream()
        self.assertEquals(0, s.write(""))
        self.assertEquals(2, s.write("ab"))
        s.close()
