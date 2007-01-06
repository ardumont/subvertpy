# Copyright (C) 2006 Jelmer Vernooij <jelmer@samba.org>

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

from bzrlib.branch import Branch
from bzrlib.bzrdir import BzrDir
from bzrlib.errors import NotBranchError
from bzrlib.repository import Repository
from bzrlib.tests import TestCase, TestCaseInTempDir
from bzrlib.trace import mutter

import os
from convert import convert_repository, NotDumpFile, load_dumpfile
from repository import MAPPING_VERSION
from scheme import TrunkBranchingScheme, NoBranchingScheme
from tests import TestCaseWithSubversionRepository

import svn.repos

class TestLoadDumpfile(TestCaseInTempDir):
    def test_loaddumpfile(self):
        dumpfile = os.path.join(self.test_dir, "dumpfile")
        open(dumpfile, 'w').write(
"""SVN-fs-dump-format-version: 2

UUID: 6987ef2d-cd6b-461f-9991-6f1abef3bd59

Revision-number: 0
Prop-content-length: 56
Content-length: 56

K 8
svn:date
V 27
2006-07-02T13:14:51.972532Z
PROPS-END
""")
        load_dumpfile(dumpfile, "d")
        repos = svn.repos.open("d")
        fs = svn.repos.fs(repos)
        self.assertEqual("6987ef2d-cd6b-461f-9991-6f1abef3bd59", 
                svn.fs.get_uuid(fs))

    def test_loaddumpfile_invalid(self):
        dumpfile = os.path.join(self.test_dir, "dumpfile")
        open(dumpfile, 'w').write("""FooBar""")
        self.assertRaises(NotDumpFile, load_dumpfile, dumpfile, "d")


class TestConversion(TestCaseWithSubversionRepository):
    def setUp(self):
        super(TestConversion, self).setUp()
        self.repos_url = self.make_client('d', 'dc')
        self.build_tree({'dc/trunk/file': 'data', 'dc/branches/abranch/anotherfile': 'data2'})
        self.client_add("dc/trunk")
        self.client_add("dc/branches")
        self.client_commit("dc", "create repos")
        self.build_tree({'dc/trunk/file': 'otherdata'})
        self.client_commit("dc", "change")

    def test_fetch_alive(self):
        self.build_tree({'dc/branches/somebranch/somefile': 'data'})
        self.client_add("dc/branches/somebranch")
        self.client_commit("dc", "add a branch")
        self.client_delete("dc/branches/somebranch")
        self.client_commit("dc", "remove branch")
        convert_repository(self.repos_url, "e", TrunkBranchingScheme(), 
                           all=False, create_shared_repo=True)
        oldrepos = Repository.open(self.repos_url)
        newrepos = Repository.open("e")
        self.assertFalse(newrepos.has_revision("svn-v%d:2@%s-branches%%2fsomebranch" % (MAPPING_VERSION, oldrepos.uuid)))

    def test_fetch_filebranch(self):
        self.build_tree({'dc/branches/somebranch': 'data'})
        self.client_add("dc/branches/somebranch")
        self.client_commit("dc", "add a branch")
        convert_repository(self.repos_url, "e", TrunkBranchingScheme())
        oldrepos = Repository.open(self.repos_url)
        newrepos = Repository.open("e")
        self.assertFalse(newrepos.has_revision("svn-v%d:2@%s-branches%%2fsomebranch" % (MAPPING_VERSION, oldrepos.uuid)))


    def test_fetch_dead(self):
        self.build_tree({'dc/branches/somebranch/somefile': 'data'})
        self.client_add("dc/branches/somebranch")
        self.client_commit("dc", "add a branch")
        self.client_delete("dc/branches/somebranch")
        self.client_commit("dc", "remove branch")
        convert_repository(self.repos_url, "e", TrunkBranchingScheme(), 
                           all=True, create_shared_repo=True)
        oldrepos = Repository.open(self.repos_url)
        newrepos = Repository.open("e")
        mutter('q: %r' % newrepos.all_revision_ids())
        self.assertTrue(newrepos.has_revision("svn-v%d:3@%s-branches%%2fsomebranch" % (MAPPING_VERSION, oldrepos.uuid)))

    def test_shared_import_continue(self):
        BzrDir.create_repository("e", shared=True)

        convert_repository("svn+"+self.repos_url, "e", 
                TrunkBranchingScheme(), True)

        self.assertTrue(Repository.open("e").is_shared())

    def test_shared_import_with_wt(self):
        BzrDir.create_repository("e", shared=True)

        convert_repository("svn+"+self.repos_url, "e", 
                TrunkBranchingScheme(), True, True)

        self.assertTrue(os.path.isfile(os.path.join(
                        self.test_dir, "e", "trunk", "file")))

    def test_shared_import_without_wt(self):
        BzrDir.create_repository("e", shared=True)

        convert_repository("svn+"+self.repos_url, "e", 
                TrunkBranchingScheme(), True, False)

        self.assertFalse(os.path.isfile(os.path.join(
                        self.test_dir, "e", "trunk", "file")))

    def test_shared_import_continue_branch(self):
        convert_repository("svn+"+self.repos_url, "e", 
                TrunkBranchingScheme(), True)

        self.build_tree({'dc/trunk/file': 'foodata'})
        self.client_commit("dc", "msg")

        self.assertEqual("svn-v%d:2@%s-trunk" % 
                        (MAPPING_VERSION, Repository.open(self.repos_url).uuid),
                        Branch.open("e/trunk").last_revision())

        convert_repository("svn+"+self.repos_url, "e", 
                TrunkBranchingScheme(), True)

        self.assertEqual("svn-v%d:3@%s-trunk" % 
                        (MAPPING_VERSION, Repository.open(self.repos_url).uuid),
                        Branch.open("e/trunk").last_revision())

 
    def test_shared_import(self):
        convert_repository("svn+"+self.repos_url, "e", 
                TrunkBranchingScheme(), True)

        self.assertTrue(Repository.open("e").is_shared())
    
    def test_simple(self):
        convert_repository("svn+"+self.repos_url, os.path.join(self.test_dir, "e"), TrunkBranchingScheme())
        self.assertTrue(os.path.isdir(os.path.join(self.test_dir, "e", "trunk")))
        self.assertTrue(os.path.isdir(os.path.join(self.test_dir, "e", "branches", "abranch")))

    def test_notshared_import(self):
        convert_repository("svn+"+self.repos_url, "e", TrunkBranchingScheme(), 
                           False)

        self.assertRaises(NotBranchError, Repository.open, "e")

class TestConversionFromDumpfile(TestCaseWithSubversionRepository):
    def test_dumpfile_open_empty(self):
        dumpfile = os.path.join(self.test_dir, "dumpfile")
        open(dumpfile, 'w').write(
"""SVN-fs-dump-format-version: 2

UUID: 6987ef2d-cd6b-461f-9991-6f1abef3bd59

Revision-number: 0
Prop-content-length: 56
Content-length: 56

K 8
svn:date
V 27
2006-07-02T13:14:51.972532Z
PROPS-END
""")
        branch_path = os.path.join(self.test_dir, "f")
        convert_repository(dumpfile, branch_path, NoBranchingScheme())
        branch = Repository.open(branch_path)
        self.assertEqual([], branch.all_revision_ids())
        Branch.open(branch_path)

    def test_dumpfile_open_empty_trunk(self):
        dumpfile = os.path.join(self.test_dir, "dumpfile")
        open(dumpfile, 'w').write(
"""SVN-fs-dump-format-version: 2

UUID: 6987ef2d-cd6b-461f-9991-6f1abef3bd59

Revision-number: 0
Prop-content-length: 56
Content-length: 56

K 8
svn:date
V 27
2006-07-02T13:14:51.972532Z
PROPS-END
""")
        branch_path = os.path.join(self.test_dir, "f")
        convert_repository(dumpfile, branch_path, TrunkBranchingScheme())
        repository = Repository.open(branch_path)
        self.assertEqual([], repository.all_revision_ids())
        self.assertRaises(NotBranchError, Branch.open, branch_path)

    def test_open_internal(self):
        filename = os.path.join(self.test_dir, "dumpfile")
        open(filename, 'w').write(
"""SVN-fs-dump-format-version: 2

UUID: 6987ef2d-cd6b-461f-9991-6f1abef3bd59

Revision-number: 0
Prop-content-length: 56
Content-length: 56

K 8
svn:date
V 27
2006-07-02T13:14:51.972532Z
PROPS-END

Revision-number: 1
Prop-content-length: 109
Content-length: 109

K 7
svn:log
V 9
Add trunk
K 10
svn:author
V 6
jelmer
K 8
svn:date
V 27
2006-07-02T13:58:02.528258Z
PROPS-END

Node-path: trunk
Node-kind: dir
Node-action: add
Prop-content-length: 10
Content-length: 10

PROPS-END


Node-path: trunk/bla
Node-kind: file
Node-action: add
Prop-content-length: 10
Text-content-length: 5
Text-content-md5: 6137cde4893c59f76f005a8123d8e8e6
Content-length: 15

PROPS-END
data


""")
        convert_repository(filename, os.path.join(self.test_dir, "e"), 
                           TrunkBranchingScheme())
        branch = Branch.open(os.path.join(self.test_dir, "e", "trunk"))
        self.assertEqual("file://%s/e/trunk" % self.test_dir, branch.base.rstrip("/"))
        self.assertEqual("svn-v%d:1@6987ef2d-cd6b-461f-9991-6f1abef3bd59-trunk" % MAPPING_VERSION, branch.last_revision())
