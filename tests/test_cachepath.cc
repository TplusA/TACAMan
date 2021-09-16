/*
 * Copyright (C) 2017, 2020, 2021  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of TACAMan.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <doctest.h>

#include "cachepath.hh"

#include "mock_messages.hh"
#include "mock_os.hh"

/*!
 * \addtogroup cache_path_tests Unit tests
 * \ingroup cache
 *
 * Cache path unit tests.
 */
/*!@{*/

TEST_SUITE_BEGIN("Cache Paths");

class Fixture
{
  protected:
    std::unique_ptr<MockMessages::Mock> mock_messages;
    std::unique_ptr<MockOS::Mock> mock_os;

  public:
    explicit Fixture():
        mock_messages(std::make_unique<MockMessages::Mock>()),
        mock_os(std::make_unique<MockOS::Mock>())
    {
        MockMessages::singleton = mock_messages.get();
        MockOS::singleton = mock_os.get();
    }

    ~Fixture()
    {
        try
        {
            mock_messages->done();
            mock_os->done();
        }
        catch(...)
        {
            /* no throwing from dtors */
        }

        MockMessages::singleton = nullptr;
        MockOS::singleton = nullptr;
    }
};

TEST_CASE_FIXTURE(Fixture, "Path constructors")
{
    static const char root_as_cstring[] = "/root/from/c/string";
    static const std::string root_as_cxxstring("/root/from/cxx/string");

    ArtCache::Path a(root_as_cstring);
    ArtCache::Path acopy(a);
    ArtCache::Path b(root_as_cxxstring);
    ArtCache::Path bcopy(b);

    static const std::string expected_c_root("/root/from/c/string/");
    static const std::string expected_cxx_root("/root/from/cxx/string/");

    CHECK(a.str() == expected_c_root);
    CHECK(a.dirstr() == expected_c_root);
    CHECK(acopy.str() == expected_c_root);
    CHECK(acopy.dirstr() == expected_c_root);

    CHECK(b.str() == expected_cxx_root);
    CHECK(b.dirstr() == expected_cxx_root);
    CHECK(bcopy.str() == expected_cxx_root);
    CHECK(bcopy.dirstr() == expected_cxx_root);
}

TEST_CASE_FIXTURE(Fixture, "Constructor with empty string refers to root path")
{
    const ArtCache::Path p("");
    CHECK(p.str() == "/");
    CHECK(p.dirstr() == "/");
}

TEST_CASE_FIXTURE(Fixture, "Append hash to path as directory")
{
    ArtCache::Path p("/cache");

    p.append_hash("64ef367018099de4d4183ffa3bc0848a", false);
    CHECK(p.str() == "/cache/64/ef367018099de4d4183ffa3bc0848a/");
}

TEST_CASE_FIXTURE(Fixture, "Append hash to path as file")
{
    ArtCache::Path p("/cache");

    p.append_hash("64ef367018099de4d4183ffa3bc0848a", true);
    CHECK(p.str() == "/cache/64/ef367018099de4d4183ffa3bc0848a");
}

TEST_CASE_FIXTURE(Fixture, "Trying to append empty hash as directory is a bug")
{
    ArtCache::Path p("/cache");

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Cannot append empty hash to path", false);
    p.append_hash("", false);
    CHECK(p.str() == "/cache/");
}

TEST_CASE_FIXTURE(Fixture, "Trying to append empty hash as file is a bug")
{
    ArtCache::Path p("/cache");

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Cannot append empty hash to path", false);
    p.append_hash("", true);
    CHECK(p.str() == "/cache/");
}

TEST_CASE_FIXTURE(Fixture, "Trying to append short hash as directory is a bug")
{
    ArtCache::Path p("/cache");

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Hash too short", false);
    p.append_hash("a", false);
    CHECK(p.str() == "/cache/");

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Hash too short", false);
    p.append_hash("ab", false);
    CHECK(p.str() == "/cache/");

    p.append_hash("abc", false);
    CHECK(p.str() == "/cache/ab/c/");
}

TEST_CASE_FIXTURE(Fixture, "Trying to append short hash as file is a bug")
{
    ArtCache::Path p("/cache");

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Hash too short", false);
    p.append_hash("a", true);
    CHECK(p.str() == "/cache/");

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Hash too short", false);
    p.append_hash("ab", true);
    CHECK(p.str() == "/cache/");

    p.append_hash("abc", true);
    CHECK(p.str() == "/cache/ab/c");
}

TEST_CASE_FIXTURE(Fixture, "Trying to append empty directory part to path is a bug")
{
    ArtCache::Path p("/cache");

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Cannot append empty part to path", false);
    p.append_part("", false);
    CHECK(p.str() == "/cache/");
}

TEST_CASE_FIXTURE(Fixture, "Trying to append empty file part to path is a bug")
{
    ArtCache::Path p("/cache");

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Cannot append empty part to path", false);
    p.append_part("", true);
    CHECK(p.str() == "/cache/");
}

TEST_CASE_FIXTURE(Fixture, "Append multiple components to path")
{
    ArtCache::Path p("/cache");

    p.append_hash("64ef367018099de4d4183ffa3bc0848a")
     .append_part("050")
     .append_part("some_file", true);
    CHECK(p.str() == "/cache/64/ef367018099de4d4183ffa3bc0848a/050/some_file");
    CHECK(p.dirstr() == "/cache/64/ef367018099de4d4183ffa3bc0848a/050/");
}

TEST_CASE_FIXTURE(Fixture, "Trying to append to a path to file is a bug")
{
    ArtCache::Path p("/cache");

    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Cannot append part to file name", false);
    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT,
                                   "BUG: Cannot append part to file name", false);
    p.append_hash("64ef367018099de4d4183ffa3bc0848a", true)
     .append_part("050")
     .append_part("some_file", true);
    CHECK(p.str() == "/cache/64/ef367018099de4d4183ffa3bc0848a");
    CHECK(p.dirstr() == "/cache/64/");
}

TEST_CASE_FIXTURE(Fixture, "Intermediate paths may be used to construct more paths")
{
    ArtCache::Path root("/root");
    root.append_part("sub").append_hash("123456");

    ArtCache::Path a(root);
    a.append_hash("abcdef").append_part("file", true);

    ArtCache::Path b(root);
    b.append_part("hello", true);

    ArtCache::Path c(root);
    c.append_part("another_sub");

    CHECK(root.str() == "/root/sub/12/3456/");
    CHECK(root.dirstr() == "/root/sub/12/3456/");
    CHECK(a.str() == "/root/sub/12/3456/ab/cdef/file");
    CHECK(a.dirstr() == "/root/sub/12/3456/ab/cdef/");
    CHECK(b.str() == "/root/sub/12/3456/hello");
    CHECK(b.dirstr() == "/root/sub/12/3456/");
    CHECK(c.str() == "/root/sub/12/3456/another_sub/");
    CHECK(c.dirstr() == "/root/sub/12/3456/another_sub/");
}

TEST_CASE_FIXTURE(Fixture, "Can check if directory exists")
{
    ArtCache::Path path("/dir");

    expect<MockOS::PathGetType>(mock_os, OS_PATH_TYPE_DIRECTORY, 0, "/dir/");
    CHECK(path.exists());
}

TEST_CASE_FIXTURE(Fixture, "Check if directory exists for a file of the same name fails")
{
    ArtCache::Path path("");
    path.append_part("dir");

    expect<MockOS::PathGetType>(mock_os, OS_PATH_TYPE_FILE, 0, "/dir/");
    CHECK_FALSE(path.exists());
}

TEST_CASE_FIXTURE(Fixture, "Check if directory exists for a special file of the same name fails")
{
    ArtCache::Path path("");
    path.append_part("dir");

    expect<MockOS::PathGetType>(mock_os, OS_PATH_TYPE_OTHER, 0, "/dir/");
    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT, "BUG: Unexpected type of path /dir/", false);
    CHECK_FALSE(path.exists());
}

TEST_CASE_FIXTURE(Fixture, "Check for non-existent directory fails")
{
    ArtCache::Path path("/dir");

    expect<MockOS::PathGetType>(mock_os, OS_PATH_TYPE_IO_ERROR, 0, "/dir/");
    CHECK_FALSE(path.exists());
}

TEST_CASE_FIXTURE(Fixture, "Check if file exists")
{
    ArtCache::Path path("/dir");
    path.append_part("file", true);

    expect<MockOS::PathGetType>(mock_os, OS_PATH_TYPE_FILE, 0, "/dir/file");
    CHECK(path.exists());
}

TEST_CASE_FIXTURE(Fixture, "Check if file for a directory of the same name fails")
{
    ArtCache::Path path("/dir");
    path.append_part("file", true);

    expect<MockOS::PathGetType>(mock_os, OS_PATH_TYPE_DIRECTORY, 0, "/dir/file");
    CHECK_FALSE(path.exists());
}

TEST_CASE_FIXTURE(Fixture, "Check if file for a special file of the same name fails")
{
    ArtCache::Path path("/dir");
    path.append_part("file", true);

    expect<MockOS::PathGetType>(mock_os, OS_PATH_TYPE_OTHER, 0, "/dir/file");
    expect<MockMessages::MsgError>(mock_messages, 0, LOG_CRIT, "BUG: Unexpected type of path /dir/file", false);
    CHECK_FALSE(path.exists());
}

TEST_CASE_FIXTURE(Fixture, "Check for non-existent file fails")
{
    ArtCache::Path path("/dir");
    path.append_part("file", true);

    expect<MockOS::PathGetType>(mock_os, OS_PATH_TYPE_IO_ERROR, 0, "/dir/file");
    CHECK_FALSE(path.exists());
}

TEST_SUITE_END();

/*!@}*/
