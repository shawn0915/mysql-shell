/*
 * Copyright (c) 2014, 2017, Oracle and/or its affiliates. All rights reserved.
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

#ifndef MYSQLSHDK_LIBS_DB_UTILS_CONNECTION_H_
#define MYSQLSHDK_LIBS_DB_UTILS_CONNECTION_H_

#include <set>
#include <string>
#include <vector>

#include "mysqlshdk/include/mysqlshdk_export.h"

#ifdef _WIN32
#include <string.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

namespace mysqlshdk {
namespace db {

/**
 * Bidirectional map of modes values:
 * "DISABLED" <-> 1
 * "PREFERRED" <-> 2
 * "REQUIRED" <-> 3
 * "VERIFY_CA" <-> 4
 * "VERIFY_IDENTITY" <-> 5
 */
class SHCORE_PUBLIC MapSslModeNameToValue {
 private:
  MapSslModeNameToValue() {}

 public:
  static int get_value(const std::string& value);
  static const std::string& get_value(int value);
};

// common keys for dictionary connection data
const char kHost[] = "host";
const char kPort[] = "port";
const char kSocket[] = "socket";
const char kScheme[] = "scheme";
const char kSchema[] = "schema";
const char kUser[] = "user";
const char kDbUser[] = "dbUser";
const char kPassword[] = "password";
const char kDbPassword[] = "dbPassword";
const char kSslCa[] = "ssl-ca";
const char kSslCaPath[] = "ssl-capath";
const char kSslCert[] = "ssl-cert";
const char kSslKey[] = "ssl-key";
const char kSslCrl[] = "ssl-crl";
const char kSslCrlPath[] = "ssl-crlpath";
const char kSslCipher[] = "ssl-cipher";
const char kSslTlsVersion[] = "tls-version";
const char kSslMode[] = "ssl-mode";
const char kAuthMethod[] = "auth-method";

const char kSslModeDisabled[] = "disabled";
const char kSslModePreferred[] = "preferred";
const char kSslModeRequired[] = "required";
const char kSslModeVerifyCA[] = "verify_ca";
const char kSslModeVerifyIdentity[] = "verify_identity";

const std::set<std::string> connection_attributes = {
    kHost,       kPort,          kSocket,   kScheme,     kSchema,
    kUser,       kDbUser,        kPassword, kDbPassword, kSslCa,
    kSslCaPath,  kSslCert,       kSslKey,   kSslCrl,     kSslCrlPath,
    kSslCipher, kSslTlsVersion, kSslMode,  kAuthMethod};

const std::set<std::string> uri_connection_attributes = {
    kSslCa,      kSslCaPath,  kSslCert,       kSslKey,  kSslCrl,
    kSslCrlPath, kSslCipher, kSslTlsVersion, kSslMode, kAuthMethod};

const std::vector<std::string> ssl_modes = {"",
                                            kSslModeDisabled,
                                            kSslModePreferred,
                                            kSslModeRequired,
                                            kSslModeVerifyCA,
                                            kSslModeVerifyIdentity};

}  // namespace db
}  // namespace mysqlshdk

#endif  // MYSQLSHDK_LIBS_DB_UTILS_CONNECTION_H_