/*
 * Copyright (C) 2017  T+A elektroakustik GmbH & Co. KG
 *
 * This file is part of TACAMan.
 *
 * TACAMan is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3 as
 * published by the Free Software Foundation.
 *
 * TACAMan is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with TACAMan.  If not, see <http://www.gnu.org/licenses/>.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cppcutter.h>

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

namespace cache_path_tests
{

static MockMessages *mock_messages;
static MockOs *mock_os;

void cut_setup(void)
{
    mock_os = new MockOs;
    cppcut_assert_not_null(mock_os);
    mock_os->init();
    mock_os_singleton = mock_os;

    mock_messages = new MockMessages;
    cppcut_assert_not_null(mock_messages);
    mock_messages->init();
    mock_messages_singleton = mock_messages;
}

void cut_teardown(void)
{
    mock_os->check();
    mock_messages->check();

    mock_os_singleton = nullptr;
    mock_messages_singleton = nullptr;

    delete mock_os;
    delete mock_messages;

    mock_os = nullptr;
    mock_messages = nullptr;
}

void test_ctors()
{
    static const char root_as_cstring[] = "/root/from/c/string";
    static const std::string root_as_cxxstring("/root/from/cxx/string");

    ArtCache::Path a(root_as_cstring);
    ArtCache::Path acopy(a);
    ArtCache::Path b(root_as_cxxstring);
    ArtCache::Path bcopy(b);

    static const std::string expected_c_root("/root/from/c/string/");
    static const std::string expected_cxx_root("/root/from/cxx/string/");

    cppcut_assert_equal(expected_c_root, a.str());
    cppcut_assert_equal(expected_c_root, a.dirstr());
    cppcut_assert_equal(expected_c_root, acopy.str());
    cppcut_assert_equal(expected_c_root, acopy.dirstr());

    cppcut_assert_equal(expected_cxx_root, b.str());
    cppcut_assert_equal(expected_cxx_root, b.dirstr());
    cppcut_assert_equal(expected_cxx_root, bcopy.str());
    cppcut_assert_equal(expected_cxx_root, bcopy.dirstr());
}

void test_ctor_with_empty_string_refers_to_root()
{
    const ArtCache::Path p("");
    cppcut_assert_equal("/", p.str().c_str());
    cppcut_assert_equal("/", p.dirstr().c_str());
}

void test_append_hash_as_directory()
{
    ArtCache::Path p("/cache");

    p.append_hash("64ef367018099de4d4183ffa3bc0848a", false);
    cppcut_assert_equal("/cache/64/ef367018099de4d4183ffa3bc0848a/", p.str().c_str());
}

void test_append_hash_as_file()
{
    ArtCache::Path p("/cache");

    p.append_hash("64ef367018099de4d4183ffa3bc0848a", true);
    cppcut_assert_equal("/cache/64/ef367018099de4d4183ffa3bc0848a", p.str().c_str());
}

void test_append_empty_hash_dir_is_a_bug()
{
    ArtCache::Path p("/cache");

    mock_messages->expect_msg_error(0, LOG_CRIT,
                                    "BUG: Cannot append empty hash to path");
    p.append_hash("", false);
    cppcut_assert_equal("/cache/", p.str().c_str());
}

void test_append_empty_hash_file_is_a_bug()
{
    ArtCache::Path p("/cache");

    mock_messages->expect_msg_error(0, LOG_CRIT,
                                    "BUG: Cannot append empty hash to path");
    p.append_hash("", true);
    cppcut_assert_equal("/cache/", p.str().c_str());
}

void test_append_short_hash_dir_is_a_bug()
{
    ArtCache::Path p("/cache");

    mock_messages->expect_msg_error(0, LOG_CRIT, "BUG: Hash too short");
    p.append_hash("a", false);
    cppcut_assert_equal("/cache/", p.str().c_str());

    mock_messages->expect_msg_error(0, LOG_CRIT, "BUG: Hash too short");
    p.append_hash("ab", false);
    cppcut_assert_equal("/cache/", p.str().c_str());

    p.append_hash("abc", false);
    cppcut_assert_equal("/cache/ab/c/", p.str().c_str());
}

void test_append_short_hash_file_is_a_bug()
{
    ArtCache::Path p("/cache");

    mock_messages->expect_msg_error(0, LOG_CRIT, "BUG: Hash too short");
    p.append_hash("a", true);
    cppcut_assert_equal("/cache/", p.str().c_str());

    mock_messages->expect_msg_error(0, LOG_CRIT, "BUG: Hash too short");
    p.append_hash("ab", true);
    cppcut_assert_equal("/cache/", p.str().c_str());

    p.append_hash("abc", true);
    cppcut_assert_equal("/cache/ab/c", p.str().c_str());
}

void test_append_empty_dir_part_is_a_bug()
{
    ArtCache::Path p("/cache");

    mock_messages->expect_msg_error(0, LOG_CRIT,
                                    "BUG: Cannot append empty part to path");
    p.append_part("", false);
    cppcut_assert_equal("/cache/", p.str().c_str());
}

void test_append_empty_file_part_is_a_bug()
{
    ArtCache::Path p("/cache");

    mock_messages->expect_msg_error(0, LOG_CRIT,
                                    "BUG: Cannot append empty part to path");
    p.append_part("", true);
    cppcut_assert_equal("/cache/", p.str().c_str());
}

void test_append_multiple_components()
{
    ArtCache::Path p("/cache");

    p.append_hash("64ef367018099de4d4183ffa3bc0848a")
     .append_part("050")
     .append_part("some_file", true);
    cppcut_assert_equal("/cache/64/ef367018099de4d4183ffa3bc0848a/050/some_file", p.str().c_str());
    cppcut_assert_equal("/cache/64/ef367018099de4d4183ffa3bc0848a/050/", p.dirstr().c_str());
}

void test_append_to_file_is_a_bug()
{
    ArtCache::Path p("/cache");

    mock_messages->expect_msg_error(0, LOG_CRIT,
                                    "BUG: Cannot append part to file name");
    mock_messages->expect_msg_error(0, LOG_CRIT,
                                    "BUG: Cannot append part to file name");
    p.append_hash("64ef367018099de4d4183ffa3bc0848a", true)
     .append_part("050")
     .append_part("some_file", true);
    cppcut_assert_equal("/cache/64/ef367018099de4d4183ffa3bc0848a", p.str().c_str());
    cppcut_assert_equal("/cache/64/", p.dirstr().c_str());
}

void test_intermediate_paths_may_be_used_to_construct_more_paths()
{
    ArtCache::Path root("/root");
    root.append_part("sub").append_hash("123456");

    ArtCache::Path a(root);
    a.append_hash("abcdef").append_part("file", true);

    ArtCache::Path b(root);
    b.append_part("hello", true);

    ArtCache::Path c(root);
    c.append_part("another_sub");

    cppcut_assert_equal("/root/sub/12/3456/", root.str().c_str());
    cppcut_assert_equal("/root/sub/12/3456/", root.dirstr().c_str());
    cppcut_assert_equal("/root/sub/12/3456/ab/cdef/file", a.str().c_str());
    cppcut_assert_equal("/root/sub/12/3456/ab/cdef/", a.dirstr().c_str());
    cppcut_assert_equal("/root/sub/12/3456/hello", b.str().c_str());
    cppcut_assert_equal("/root/sub/12/3456/", b.dirstr().c_str());
    cppcut_assert_equal("/root/sub/12/3456/another_sub/", c.str().c_str());
    cppcut_assert_equal("/root/sub/12/3456/another_sub/", c.dirstr().c_str());
}

void test_check_directory_exists()
{
    ArtCache::Path path("/dir");

    mock_os->expect_os_path_get_type(OS_PATH_TYPE_DIRECTORY, "/dir/");
    cut_assert_true(path.exists());
}

void test_check_directory_but_is_file()
{
    ArtCache::Path path("");
    path.append_part("dir");

    mock_os->expect_os_path_get_type(OS_PATH_TYPE_FILE, "/dir/");
    cut_assert_false(path.exists());
}

void test_check_directory_but_is_special()
{
    ArtCache::Path path("");
    path.append_part("dir");

    mock_os->expect_os_path_get_type(OS_PATH_TYPE_OTHER, "/dir/");
    mock_messages->expect_msg_error_formatted(0, LOG_CRIT,
                                              "BUG: Unexpected type of path /dir/");
    cut_assert_false(path.exists());
}

void test_check_directory_does_not_exist()
{
    ArtCache::Path path("/dir");

    mock_os->expect_os_path_get_type(OS_PATH_TYPE_IO_ERROR, "/dir/");
    cut_assert_false(path.exists());
}

void test_check_file_exists()
{
    ArtCache::Path path("/dir");
    path.append_part("file", true);

    mock_os->expect_os_path_get_type(OS_PATH_TYPE_FILE, "/dir/file");
    cut_assert_true(path.exists());
}

void test_check_file_but_is_directory()
{
    ArtCache::Path path("/dir");
    path.append_part("file", true);

    mock_os->expect_os_path_get_type(OS_PATH_TYPE_DIRECTORY, "/dir/file");
    cut_assert_false(path.exists());
}

void test_check_file_but_is_special()
{
    ArtCache::Path path("/dir");
    path.append_part("file", true);

    mock_os->expect_os_path_get_type(OS_PATH_TYPE_OTHER, "/dir/file");
    mock_messages->expect_msg_error_formatted(0, LOG_CRIT,
                                              "BUG: Unexpected type of path /dir/file");
    cut_assert_false(path.exists());
}

void test_check_file_does_not_exist()
{
    ArtCache::Path path("/dir");
    path.append_part("file", true);

    mock_os->expect_os_path_get_type(OS_PATH_TYPE_IO_ERROR, "/dir/file");
    cut_assert_false(path.exists());
}

}

/*!@}*/
