# Copyright (C) 2005-2008 Jelmer Vernooij <jelmer@samba.org>
 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

SVN_PROP_SVK_MERGE = 'svk:merge'

def parse_svk_feature(feature):
    """Parse a svk feature identifier.

    :param feature: The feature identifier as string.
    :return: tuple with uuid, branch path and revnum
    """
    try:
        (uuid, branch, revnum) = feature.split(":", 3)
    except ValueError:
        raise errors.InvalidPropertyValue(SVN_PROP_SVK_MERGE, 
                "not enough colons")
    return (uuid, branch.strip("/"), int(revnum))
