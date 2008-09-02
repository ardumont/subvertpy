# Copyright (C) 2006-2007 Jelmer Vernooij <jelmer@samba.org>

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

"""Revision id generation and caching."""

from bzrlib.errors import (InvalidRevisionId, NoSuchRevision)
from bzrlib.trace import mutter

from bzrlib.plugins.svn.cache import CacheTable
from bzrlib.plugins.svn.core import SubversionException
from bzrlib.plugins.svn.errors import InvalidPropertyValue, ERR_FS_NO_SUCH_REVISION, InvalidBzrSvnRevision
from bzrlib.plugins.svn.mapping import (parse_revision_id, BzrSvnMapping, 
                     SVN_PROP_BZR_REVISION_ID, parse_revid_property,
                     find_mapping, parse_mapping_name, is_bzr_revision_revprops)

class RevidMap(object):
    def __init__(self, repos):
        self.repos = repos

    def get_revision_id(self, revnum, path, mapping, revprops, fileprops):
        if mapping.supports_roundtripping():
            # See if there is a bzr:revision-id revprop set
            try:
                (bzr_revno, revid) = mapping.get_revision_id(path, revprops, fileprops)
            except SubversionException, (_, num):
                if num == ERR_FS_NO_SUCH_REVISION:
                    raise NoSuchRevision(path, revnum)
                raise
        else:
            revid = None

        # Or generate it
        if revid is None:
            return mapping.revision_id_foreign_to_bzr((self.repos.uuid, revnum, path))

        return revid

    def get_branch_revnum(self, revid, layout, project=None):
        """Find the (branch, revnum) tuple for a revision id."""
        # Try a simple parse
        try:
            (uuid, branch_path, revnum, mapping) = parse_revision_id(revid)
            assert isinstance(branch_path, str)
            assert isinstance(mapping, BzrSvnMapping)
            if uuid == self.repos.uuid:
                return (branch_path, revnum, mapping)
            # If the UUID doesn't match, this may still be a valid revision
            # id; a revision from another SVN repository may be pushed into 
            # this one.
        except InvalidRevisionId:
            pass

        last_revnum = self.repos.get_latest_revnum()
        fileprops_to_revnum = last_revnum
        for entry_revid, branch, revno, mapping in self.discover_revprop_revids(0, last_revnum):
            if revid == entry_revid:
                return (branch, revno, mapping.name)
            fileprops_to_revnum = min(fileprops_to_revnum, revno)

        for entry_revid, branch, revno, mapping in self.discover_fileprop_revids(layout, 0, fileprops_to_revnum, project):
            if revid == entry_revid:
                (bp, revnum, mapping_name) = self.bisect_revid_revnum(revid, branch, 0, revno)
                return (bp, revnum, mapping_name)
        raise NoSuchRevision(self, revid)

    def discover_revprop_revids(self, from_revnum, to_revnum):
        """Discover bzr-svn revision properties between from_revnum and to_revnum.

        :return: First revision number on which a revision property was found, or None
        """
        if self.repos.transport.has_capability("log-revprops") != True:
            return
        for (_, revno, revprops) in self.repos._log.iter_changes(None, from_revnum, to_revnum):
            if is_bzr_revision_revprops(revprops):
                mapping = find_mapping(revprops, {})
                (_, revid) = mapping.get_revision_id(None, revprops, {})
                if revid is not None:
                    yield (revid, mapping.get_branch_root(revprops).strip("/"), revno, mapping)

    def discover_fileprop_revids(self, layout, from_revnum, to_revnum, project=None):
        reuse_policy = self.repos.get_config().get_reuse_revisions()
        assert reuse_policy in ("other-branches", "removed-branches", "none") 
        check_removed = (reuse_policy == "removed-branches")
        for (branch, revno, exists) in self.repos.find_fileprop_paths(layout, from_revnum, to_revnum, project, check_removed=check_removed, find_branches=True, find_tags=True):
            assert isinstance(branch, str)
            assert isinstance(revno, int)
            # Look at their bzr:revision-id-vX
            revids = set()
            try:
                props = self.repos.branchprop_list.get_properties(branch, revno)
                for propname, propvalue in props.items():
                    if not propname.startswith(SVN_PROP_BZR_REVISION_ID):
                        continue
                    mapping_name = propname[len(SVN_PROP_BZR_REVISION_ID):]
                    for line in propvalue.splitlines():
                        try:
                            revids.add((parse_revid_property(line), mapping_name))
                        except InvalidPropertyValue, ie:
                            mutter(str(ie))
            except SubversionException, (_, ERR_FS_NOT_DIRECTORY):
                    continue

            # If there are any new entries that are not yet in the cache, 
            # add them
            for ((entry_revno, entry_revid), mapping_name) in revids:
                yield (entry_revid, branch, revno, parse_mapping_name(mapping_name))

    def bisect_revid_revnum(self, revid, branch_path, min_revnum, max_revnum):
        """Find out what the actual revnum was that corresponds to a revid.

        :param revid: Revision id to search for
        :param branch_path: Branch path at which to start searching
        :param min_revnum: Last revnum to check
        :param max_revnum: First revnum to check
        :return: Tuple with actual branchpath, revnum and mapping
        """
        assert min_revnum <= max_revnum
        # Find the branch property between min_revnum and max_revnum that 
        # added revid
        for revmeta in self.repos.iter_reverse_branch_changes(branch_path, max_revnum, min_revnum):
            for propname, propvalue in revmeta.get_changed_fileprops().items():
                if not propname.startswith(SVN_PROP_BZR_REVISION_ID):
                    continue
                try:
                    (entry_revno, entry_revid) = parse_revid_property(
                        propvalue.splitlines()[-1])
                except InvalidPropertyValue:
                    # Don't warn about encountering an invalid property, 
                    # that will already have happened earlier
                    continue
                if entry_revid == revid:
                    mapping_name = propname[len(SVN_PROP_BZR_REVISION_ID):]
                    mapping = parse_mapping_name(mapping_name)
                    assert (mapping.is_tag(revmeta.branch_path) or 
                            mapping.is_branch(revmeta.branch_path))
                    return (revmeta.branch_path, revmeta.revnum, mapping)

        raise InvalidBzrSvnRevision(revid)


class CachingRevidMap(object):
    def __init__(self, actual, cachedb=None):
        self.cache = RevisionIdMapCache(cachedb)
        self.actual = actual
        self.revid_seen = set()

    def get_revision_id(self, revnum, path, mapping, changed_fileprops, revprops):
        # Look in the cache to see if it already has a revision id
        revid = self.cache.lookup_branch_revnum(revnum, path, mapping.name)
        if revid is not None:
            return revid

        revid = self.actual.get_revision_id(revnum, path, mapping, changed_fileprops, revprops)

        self.cache.insert_revid(revid, path, revnum, revnum, mapping.name)
        self.cache.commit_conditionally()

        return revid

    def get_branch_revnum(self, revid, layout, project=None):
        # Try a simple parse
        try:
            (uuid, branch_path, revnum, mapping) = parse_revision_id(revid)
            assert isinstance(branch_path, str)
            assert isinstance(mapping, BzrSvnMapping)
            if uuid == self.actual.repos.uuid:
                return (branch_path, revnum, mapping)
            # If the UUID doesn't match, this may still be a valid revision
            # id; a revision from another SVN repository may be pushed into 
            # this one.
        except InvalidRevisionId:
            pass

        # Check the record out of the cache, if it exists
        try:
            (branch_path, min_revnum, max_revnum, \
                    mapping) = self.cache.lookup_revid(revid)
            assert isinstance(branch_path, str)
            assert isinstance(mapping, str)
            # Entry already complete?
            assert min_revnum <= max_revnum
            if min_revnum == max_revnum:
                return (branch_path, min_revnum, parse_mapping_name(mapping))
        except NoSuchRevision, e:
            last_revnum = self.actual.repos.get_latest_revnum()
            last_checked = self.cache.last_revnum_checked(repr((layout, project)))
            if (last_revnum <= last_checked):
                # All revision ids in this repository for the current 
                # layout have already been discovered. No need to 
                # check again.
                raise e
            found = None
            fileprops_to_revnum = last_revnum
            for entry_revid, branch, revno, mapping in self.actual.discover_revprop_revids(last_checked, last_revnum):
                fileprops_to_revnum = min(fileprops_to_revnum, revno)
                if entry_revid == revid:
                    found = (branch, revno, revno, mapping)
                if entry_revid not in self.revid_seen:
                    self.cache.insert_revid(entry_revid, branch, revno, revno, mapping.name)
                    self.revid_seen.add(entry_revid)
            for entry_revid, branch, revno, mapping in self.actual.discover_fileprop_revids(layout, last_checked, fileprops_to_revnum, project):
                if entry_revid == revid:
                    found = (branch, last_checked, revno, mapping)
                if entry_revid not in self.revid_seen:
                    self.cache.insert_revid(entry_revid, branch, last_checked, revno, mapping.name)
                    self.revid_seen.add(entry_revid)
                
            # We've added all the revision ids for this layout in the
            # repository, so no need to check again unless new revisions got 
            # added
            self.cache.set_last_revnum_checked(repr((layout, project)), last_revnum)
            if found is None:
                raise e
            (branch_path, min_revnum, max_revnum, mapping) = found
            assert min_revnum <= max_revnum
            assert isinstance(branch_path, str)

        (branch_path, revnum, mapping) = self.actual.bisect_revid_revnum(revid, 
            branch_path, min_revnum, max_revnum)
        self.cache.insert_revid(revid, branch_path, revnum, revnum, mapping.name)
        return (branch_path, revnum, mapping)



class RevisionIdMapCache(CacheTable):
    """Revision id mapping store. 

    Stores mapping from revid -> (path, revnum, mapping)
    """
    def _create_table(self):
        self.cachedb.executescript("""
        create table if not exists revmap (revid text, path text, min_revnum integer, max_revnum integer, mapping text);
        create index if not exists revid on revmap (revid);
        create unique index if not exists revid_path_mapping on revmap (revid, path, mapping);
        drop index if exists lookup_branch_revnum;
        create index if not exists lookup_branch_revnum_non_unique on revmap (max_revnum, min_revnum, path, mapping);
        create table if not exists revids_seen (layout text, max_revnum int);
        create unique index if not exists layout on revids_seen (layout);
        """)
        # Revisions ids are quite expensive
        self._commit_interval = 1000

    def set_last_revnum_checked(self, layout, revnum):
        """Remember the latest revision number that has been checked
        for a particular layout.

        :param layout: Repository layout.
        :param revnum: Revision number.
        """
        self.cachedb.execute("replace into revids_seen (layout, max_revnum) VALUES (?, ?)", (layout, revnum))
        self.commit_conditionally()

    def last_revnum_checked(self, layout):
        """Retrieve the latest revision number that has been checked 
        for revision ids for a particular layout.

        :param layout: Repository layout.
        :return: Last revision number checked or 0.
        """
        self.mutter("last revnum checked %r", layout)
        ret = self.cachedb.execute(
            "select max_revnum from revids_seen where layout = ?", (layout,)).fetchone()
        if ret is None:
            return 0
        return int(ret[0])
    
    def lookup_revid(self, revid):
        """Lookup the details for a particular revision id.

        :param revid: Revision id.
        :return: Tuple with path inside repository, minimum revision number, maximum revision number and 
            mapping.
        """
        assert isinstance(revid, str)
        self.mutter("lookup revid %r", revid)
        ret = self.cachedb.execute(
            "select path, min_revnum, max_revnum, mapping from revmap where revid=? order by abs(min_revnum-max_revnum) asc", (revid,)).fetchone()
        if ret is None:
            raise NoSuchRevision(self, revid)
        (path, min_revnum, max_revnum, mapping) = (ret[0].encode("utf-8"), int(ret[1]), int(ret[2]), ret[3].encode("utf-8"))
        if min_revnum > max_revnum:
            return (path, max_revnum, min_revnum, mapping)
        else:
            return (path, min_revnum, max_revnum, mapping)

    def lookup_branch_revnum(self, revnum, path, mapping):
        """Lookup a revision by revision number, branch path and mapping.

        :param revnum: Subversion revision number.
        :param path: Subversion branch path.
        :param mapping: Mapping
        """
        assert isinstance(revnum, int)
        assert isinstance(path, str)
        assert isinstance(mapping, str)
        row = self.cachedb.execute(
                "select revid from revmap where max_revnum=? and min_revnum=? and path=? and mapping=?", (revnum, revnum, path, mapping)).fetchone()
        if row is not None:
            ret = str(row[0])
        else:
            ret = None
        self.mutter("lookup branch,revnum %r:%r -> %r", path, revnum, ret)
        return ret

    def insert_revid(self, revid, branch, min_revnum, max_revnum, mapping):
        """Insert a revision id into the revision id cache.

        :param revid: Revision id for which to insert metadata.
        :param branch: Branch path at which the revision was seen
        :param min_revnum: Minimum Subversion revision number in which the 
                           revid was found
        :param max_revnum: Maximum Subversion revision number in which the 
                           revid was found
        :param mapping: Name of the mapping with which the revision 
                       was found
        """
        assert revid is not None and revid != ""
        assert isinstance(mapping, str)
        assert isinstance(branch, str)
        assert isinstance(min_revnum, int) and isinstance(max_revnum, int)
        assert min_revnum <= max_revnum
        self.mutter("insert revid %r:%r-%r -> %r", branch, min_revnum, max_revnum, revid)
        if min_revnum == max_revnum:
            cursor = self.cachedb.execute(
                "update revmap set min_revnum = ?, max_revnum = ? WHERE revid=? AND path=? AND mapping=?",
                (min_revnum, max_revnum, revid, branch, mapping))
        else:
            cursor = self.cachedb.execute(
                "update revmap set min_revnum = MAX(min_revnum,?), max_revnum = MIN(max_revnum, ?) WHERE revid=? AND path=? AND mapping=?",
                (min_revnum, max_revnum, revid, branch, mapping))
        if cursor.rowcount == 0:
            self.cachedb.execute(
                "insert into revmap (revid,path,min_revnum,max_revnum,mapping) VALUES (?,?,?,?,?)",
                (revid, branch, min_revnum, max_revnum, mapping))
