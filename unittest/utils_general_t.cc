/*
 * Copyright (c) 2015, 2018, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is also distributed with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have included with MySQL.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stack>
#include <string>

#include "gtest_clean.h"
#include "mysh_config.h"
#include "unittest/test_utils/mocks/gmock_clean.h"
#include "utils/utils_general.h"

#include <mysql_version.h>

namespace shcore {
TEST(utils_general, split_account) {
  std::string a, b;

  // test no crash
  split_account("a@b", nullptr, nullptr);

  // test no crash
  split_account("a@b", &a, nullptr);

  struct Case {
    const char *account;
    const char *user;
    const char *host;
  };
  static std::vector<Case> good_cases_no_auto_quote{
      {"foo@bar", "foo", "bar"},
      {"'foo'@bar", "foo", "bar"},
      {"'foo'@'bar'", "foo", "bar"},
      {"\"foo\"@\"bar\"", "foo", "bar"},
      {"'fo\"o'@bar", "fo\"o", "bar"},
      {"'fo\\'o'@bar", "fo'o", "bar"},
      {"'fo''o'@bar", "fo'o", "bar"},
      {"\"foo\"\"bar\"@'bar'", "foo\"bar", "bar"},
      {"\"foo\\\"bar\"@'bar'", "foo\"bar", "bar"},
      {"'fo%o'@'bar%'", "fo%o", "bar%"},
      {"_foo@'bar'", "_foo", "bar"},
      {"$foo@'bar'", "$foo", "bar"},
      {"foo_@'bar'", "foo_", "bar"},
      {"foo123@'bar'", "foo123", "bar"},
      {"'foo@123'@'bar'", "foo@123", "bar"},
      {"'foo@123'", "foo@123", ""},
      {"'foo'@", "foo", ""},
      {"'fo\\no'@", "fo\no", ""},
      {"'foo'", "foo", ""},
      {"'foo'''", "foo'", ""},
      {"''''''''", "'''", ""},
      {"foo@", "foo", ""},
      {"foo@''", "foo", ""},
      {"foo@'::1'", "foo", "::1"},
      {"foo@'ho\\'\\'st'", "foo", "ho\'\'st"},
      {"foo@'\\'host'", "foo", "\'host"},
      {"foo@'\\'ho\\'st'", "foo", "\'ho\'st"},
      {"root@'ho\\'st'", "root", "ho\'st"},
      {"foo@'host\\''", "foo", "host\'"},
      {"foo@'192.167.1.___'", "foo", "192.167.1.___"},
      {"foo@'192.168.1.%'", "foo", "192.168.1.%"},
      {"foo@'192.58.197.0/255.255.255.0'", "foo", "192.58.197.0/255.255.255.0"},
      {"foo@' .::1lol\\t\\n\\r\\b\\'\"&$%'", "foo", " .::1lol\t\n\r\b'\"&$%"},
      {"`foo@`@`nope`", "foo@", "nope"},
      {"foo@`'ho'st`", "foo", "'ho'st"},
      {"foo@`1234`", "foo", "1234"},
      {"foo@```1234`", "foo", "`1234"},
      {"`foo`@```1234`", "foo", "`1234"},
      {"```foo`@```1234`", "`foo", "`1234"},
      {"foo@` .::1lol\\t\\n\\r\\b\\0'\"&$%`", "foo",
       " .::1lol\\t\\n\\r\\b\\0'\"&$%"},
      {"root@foo.bar.warblegarble.com", "root", "foo.bar.warblegarble.com"},
      {"root@192.168.0.3", "root", "192.168.0.3"},
      {"root@%", "root", "%"},
      {"test_user", "test_user", ""},
      {"123@bar", "123", "bar"},
      {"@@@", "@@", ""},
      {"@foo@@localhost", "@foo@", "localhost"},
      {"@\"''\"foo@localhost", "@\"''\"foo", "localhost"},
      {"skip-grants user@skip-grants host", "skip-grants user",
       "skip-grants host"},
      {"foo@@'stuf'", "foo@", "stuf"},
      {"foo bar@localhost", "foo bar", "localhost"}};
  for (auto &t : good_cases_no_auto_quote) {
    a.clear();
    b.clear();
    SCOPED_TRACE(t.account);
    EXPECT_NO_THROW(split_account(t.account, &a, &b));
    EXPECT_EQ(t.user, a);
    EXPECT_EQ(t.host, b);
  }
  static std::vector<Case> good_cases_auto_quote{
      {"foo@%", "foo", "%"},
      {"ic@192.168.%", "ic", "192.168.%"},  // Regression test for BUG#25528695
      {"foo@192.168.1.%", "foo", "192.168.1.%"},
      {"foo@192.58.197.0/255.255.255.0", "foo", "192.58.197.0/255.255.255.0"},
      {"root@foo-bar.com", "root", "foo-bar.com"}};
  for (auto &t : good_cases_auto_quote) {
    a.clear();
    b.clear();
    SCOPED_TRACE(t.account);
    EXPECT_NO_THROW(split_account(t.account, &a, &b, true));
    EXPECT_EQ(t.user, a);
    EXPECT_EQ(t.host, b);
  }
  static std::vector<std::string> bad_cases{
      "'foo",
      "'foo'bar",
      "'foo'@@bar",
      "'foo@bar",
      "''foo",
      "\"foo'@bar",
      "'foo'x",
      "'foo'bar",
      "'foo'@'bar'z",
      "foo@\"bar\".",
      "\"foo\"-",
      "''foo''@",
      "@",
      "@localhost",
      "''foo''@",
      "foo@'",
      "foo@''nope",
      "foo@''host''",
      "foo@'ho''st",
      "foo@'host",
      "foo@'ho'st",
      "foo@ho'st",
      "foo@host'",
      "foo@ .::1lol\\t\\n\\'\"&$%",
      "`foo@bar",
      "foo@`bar",
  };
  for (auto &t : bad_cases) {
    SCOPED_TRACE(t);
    EXPECT_THROW(split_account(t, nullptr, nullptr), std::runtime_error);
    EXPECT_THROW(split_account(t, nullptr, nullptr, true), std::runtime_error);
  }
}

TEST(utils_general, make_account) {
  struct Case {
    const char *account;
    const char *user;
    const char *host;
  };
  static std::vector<Case> good_cases{
      {"'foo'@'bar'", "foo", "bar"},
      {"'\\'foo\\''@'bar'", "'foo'", "bar"},
      {"'\\\"foo\\\"'@'bar%'", "\"foo\"", "bar%"},
      {"'a@b'@'bar'", "a@b", "bar"},
      {"'a@b'@'bar'", "a@b", "bar"},
      {"''@''", "", ""}};
  for (auto &t : good_cases) {
    SCOPED_TRACE(t.account);
    EXPECT_EQ(t.account, make_account(t.user, t.host));
  }
}

TEST(utils_general, split_string_chars) {
  std::vector<std::string> strs =
      split_string_chars("aaaa-vvvvv>bbbbb", "->", true);
  EXPECT_EQ("aaaa", strs[0]);
  EXPECT_EQ("vvvvv", strs[1]);
  EXPECT_EQ("bbbbb", strs[2]);

  std::vector<std::string> strs1 =
      split_string_chars("aaaa->bbbbb", "->", true);
  EXPECT_EQ("aaaa", strs1[0]);
  EXPECT_EQ("bbbbb", strs1[1]);

  std::vector<std::string> strs2 =
      split_string_chars("aaaa->bbbbb", "->", false);
  EXPECT_EQ("aaaa", strs2[0]);
  EXPECT_EQ("", strs2[1]);
  EXPECT_EQ("bbbbb", strs2[2]);

  std::vector<std::string> strs3 =
      split_string_chars(",aaaa-bbb-bb,", "-,", true);
  EXPECT_EQ("", strs3[0]);
  EXPECT_EQ("aaaa", strs3[1]);
  EXPECT_EQ("bbb", strs3[2]);
  EXPECT_EQ("bb", strs3[3]);
  EXPECT_EQ("", strs3[4]);

  std::vector<std::string> strs4 =
      split_string_chars("aa;a\\a-bbb-bb,", "\\-;", true);
  EXPECT_EQ("aa", strs4[0]);
  EXPECT_EQ("a", strs4[1]);
  EXPECT_EQ("a", strs4[2]);
  EXPECT_EQ("bbb", strs4[3]);
  EXPECT_EQ("bb,", strs4[4]);

  std::vector<std::string> strs5 = split_string_chars("!.", "!.", false);
  EXPECT_EQ("", strs5[0]);
  EXPECT_EQ("", strs5[1]);

  std::vector<std::string> strs6 = split_string_chars("aa.a.aaa.a", ".", true);
  EXPECT_EQ("aa", strs6[0]);
  EXPECT_EQ("a", strs6[1]);
  EXPECT_EQ("aaa", strs6[2]);
  EXPECT_EQ("a", strs6[3]);

  std::vector<std::string> strs7 = split_string_chars("", ".", false);
  EXPECT_EQ("", strs7[0]);

  std::vector<std::string> strs8 = split_string_chars(".", ".", false);
  EXPECT_EQ("", strs8[0]);
  EXPECT_EQ("", strs8[1]);

  std::vector<std::string> strs9 = split_string_chars("....", ".", false);
  EXPECT_EQ("", strs9[0]);
  EXPECT_EQ("", strs9[1]);
  EXPECT_EQ("", strs9[2]);
  EXPECT_EQ("", strs9[3]);
}

TEST(utils_general, match_glob) {
  EXPECT_TRUE(match_glob("*", ""));
  EXPECT_TRUE(match_glob("**", ""));
  EXPECT_TRUE(match_glob("*", "aaaa"));
  EXPECT_TRUE(match_glob("**", "aaaa"));
  EXPECT_FALSE(match_glob("*?", ""));
  EXPECT_TRUE(match_glob("*?", "a"));
  EXPECT_TRUE(match_glob("*?", "aaaa"));
  EXPECT_TRUE(match_glob("?*?", "aaaa"));
  EXPECT_TRUE(match_glob("?*?", "aa"));
  EXPECT_FALSE(match_glob("?*?", "a"));
  EXPECT_TRUE(match_glob("a", "a"));
  EXPECT_TRUE(match_glob("a", "A"));
  EXPECT_FALSE(match_glob("a", ""));
  EXPECT_FALSE(match_glob("a", "b"));
  EXPECT_FALSE(match_glob("a", "A", true));
  EXPECT_TRUE(match_glob("A", "A", true));
  EXPECT_TRUE(match_glob("a?b", "axb"));
  EXPECT_FALSE(match_glob("a?b", "ab"));
  EXPECT_TRUE(match_glob("*.*", "a.b"));
  EXPECT_TRUE(match_glob("*.*", "a.b.c"));
  EXPECT_TRUE(match_glob("*.*", "."));
  EXPECT_TRUE(match_glob("\\*", "*"));
  EXPECT_FALSE(match_glob("\\*", "\\"));
  EXPECT_FALSE(match_glob("\\*", "x"));
  EXPECT_TRUE(match_glob("\\?", "?"));
  EXPECT_TRUE(match_glob("?\\?", "??"));
  EXPECT_TRUE(match_glob("?\\?", "x?"));
  EXPECT_FALSE(match_glob("?\\?", "xx"));
  EXPECT_FALSE(match_glob("\\?", "\\"));
  EXPECT_FALSE(match_glob("\\?", "x"));
  EXPECT_TRUE(match_glob("\\", "\\"));
  EXPECT_FALSE(match_glob("\\", "x"));
  EXPECT_TRUE(match_glob("\\opti*", "\\option"));
  EXPECT_FALSE(match_glob("\\opti\\*", "\\option"));
  EXPECT_TRUE(match_glob("*\\*", "abcdefgh*"));

  EXPECT_TRUE(match_glob("a*b", "ab"));
  EXPECT_TRUE(match_glob("a*b", "abb"));
  EXPECT_TRUE(match_glob("a*b", "abcb"));
  EXPECT_TRUE(match_glob("*a*b", "abcb"));
  EXPECT_TRUE(match_glob("*a*b", "babcb"));
  EXPECT_TRUE(match_glob("*a*b", "babacb"));
  EXPECT_TRUE(match_glob("*a*b", "ababacb"));
  EXPECT_TRUE(match_glob("*a*b*c", "xcaqbcqbxaxc"));
  EXPECT_TRUE(match_glob("*a*b*c", "xccqbcqbxabc"));
  EXPECT_TRUE(match_glob("a*b*c*", "abcbabac"));
  EXPECT_TRUE(match_glob("a*b*c*", "abcbabacdx"));
  EXPECT_TRUE(match_glob("a*b*c", "abcbabac"));
  EXPECT_TRUE(match_glob("a*b*c", "abxc"));
  EXPECT_TRUE(match_glob("a*b*c*", "abc"));
  EXPECT_TRUE(match_glob("a*b*c*", "abcd"));
  EXPECT_TRUE(match_glob("a*b*c*", "abdcd"));
  EXPECT_TRUE(match_glob("a**b*c*", "abdcd"));
  EXPECT_TRUE(match_glob("a**b**c*", "abdcd"));
  EXPECT_TRUE(match_glob("a**b**c**", "abdcd"));

  EXPECT_TRUE(match_glob("", ""));
  EXPECT_FALSE(match_glob("x", ""));
  EXPECT_FALSE(match_glob("", "x"));
  EXPECT_TRUE(match_glob("abc", "abc"));
  EXPECT_TRUE(match_glob("*", "abc"));
  EXPECT_TRUE(match_glob("*c", "abc"));
  EXPECT_FALSE(match_glob("*b", "abc"));
  EXPECT_TRUE(match_glob("a*", "abc"));
  EXPECT_FALSE(match_glob("b*", "abc"));
  EXPECT_TRUE(match_glob("a*", "a"));
  EXPECT_TRUE(match_glob("*a", "a"));
  EXPECT_TRUE(match_glob("a*b*c*d*e*", "axbxcxdxe"));
  EXPECT_TRUE(match_glob("a*b*c*d*e*", "axbxcxdxexxx"));
  EXPECT_TRUE(match_glob("a*b?c*x", "abxbbxdbxebxczzx"));
  EXPECT_FALSE(match_glob("a*b?c*x", "abxbbxdbxebxczzy"));
  EXPECT_FALSE(match_glob("a*a*a*a*b", std::string(100, 'a')));
  EXPECT_TRUE(match_glob("a*a*a*a*a", std::string(100, 'a')));
  EXPECT_TRUE(match_glob("*x", "xxx"));
}

TEST(utils_general, get_long_version) {
  if (*MYSH_BUILD_ID) {
    std::cout << "Verifying values for the build server" << std::endl;

    const std::string suffix = EXTRA_NAME_SUFFIX;
    const std::string comment = MYSQL_COMPILATION_COMMENT;

    if (suffix == "") {
      ASSERT_EQ("MySQL Community Server (GPL)", comment);
    } else if (suffix == "-commercial") {
      ASSERT_EQ("MySQL Enterprise Server - Commercial", comment);
    } else {
      FAIL() << "EXTRA_NAME_SUFFIX should be either \"\" or \"-commercial\", "
                " got: "
             << suffix;
    }
  }

  const auto version = get_long_version();
  EXPECT_THAT(version, ::testing::HasSubstr(MYSH_FULL_VERSION));
  EXPECT_THAT(version, ::testing::HasSubstr(SYSTEM_TYPE));
  EXPECT_THAT(version, ::testing::HasSubstr(MACHINE_TYPE));
  EXPECT_THAT(version, ::testing::HasSubstr(LIBMYSQL_VERSION));
  EXPECT_THAT(version, ::testing::HasSubstr(MYSQL_COMPILATION_COMMENT));
}

TEST(utils_general, lexical_cast) {
  EXPECT_EQ(12345, lexical_cast<int>("12345"));
  EXPECT_EQ(12345, lexical_cast<unsigned>("12345"));
  EXPECT_TRUE(lexical_cast<bool>("true"));
  EXPECT_TRUE(lexical_cast<bool>("1"));
  EXPECT_FALSE(lexical_cast<bool>("false"));
  EXPECT_FALSE(lexical_cast<bool>(0));
  EXPECT_EQ(123.123, lexical_cast<double>("123.123"));
  EXPECT_EQ(std::string("12345"), lexical_cast<std::string>(12345));
  EXPECT_EQ(std::string("-12345"), lexical_cast<std::string>(-12345));

  EXPECT_THROW(lexical_cast<bool>(12345), std::invalid_argument);
  EXPECT_THROW(lexical_cast<bool>("tru"), std::invalid_argument);
  EXPECT_THROW(lexical_cast<int>(123.45), std::invalid_argument);
  EXPECT_THROW(lexical_cast<double>("12345f"), std::invalid_argument);
  EXPECT_THROW(lexical_cast<unsigned>("-12345"), std::invalid_argument);
  EXPECT_THROW(lexical_cast<unsigned>(-12345), std::invalid_argument);
}

}  // namespace shcore
