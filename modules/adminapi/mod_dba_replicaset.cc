/*
 * Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#include "mod_dba_replicaset.h"
#include "mod_dba_metadata_storage.h"

#include "common/uuid/include/uuid_gen.h"
#include "utils/utils_general.h"
#include "../mysqlxtest_utils.h"
#include "xerrmsg.h"
#include "mysqlx_connection.h"
#include "shellcore/shell_core_options.h"
#include "common/process_launcher/process_launcher.h"
#include "../mod_mysql_session.h"
#include "../mod_mysql_resultset.h"

#include <sstream>
#include <iostream>
#include <iomanip>
#include <string>

#include <boost/lexical_cast.hpp>

using namespace std::placeholders;
using namespace mysh;
using namespace mysh::dba;
using namespace shcore;

std::set<std::string> ReplicaSet::_add_instance_opts = {"name", "host", "port", "user", "dbUser", "password", "dbPassword", "socket", "ssl_ca", "ssl_cert", "ssl_key", "ssl_key"};
std::set<std::string> ReplicaSet::_remove_instance_opts = {"name", "host", "port", "socket", "ssl_ca", "ssl_cert", "ssl_key", "ssl_key"};

char const *ReplicaSet::kTopologyPrimaryMaster = "pm";
char const *ReplicaSet::kTopologyMultiMaster = "mm";

ReplicaSet::ReplicaSet(const std::string &name, const std::string &topology_type,
                       std::shared_ptr<MetadataStorage> metadata_storage) :
_name(name), _topology_type(topology_type), _json_mode(JSON_STANDARD_OUTPUT),
_metadata_storage(metadata_storage) {
  init();
}

ReplicaSet::~ReplicaSet() {}

std::string &ReplicaSet::append_descr(std::string &s_out, int UNUSED(indent), int UNUSED(quote_strings)) const {
  s_out.append("<" + class_name() + ":" + _name + ">");
  return s_out;
}

static void append_member_status(shcore::JSON_dumper& dumper,
                                std::shared_ptr<mysh::Row> member_row,
                                bool read_write) {
  //dumper.append_string("name", member_row->get_member(1).as_string());
  auto status = member_row->get_member(3);
  dumper.append_string("address", member_row->get_member(4).as_string());
  dumper.append_string("status", status ? status.as_string() : "OFFLINE");
  dumper.append_string("role", member_row->get_member(2).as_string());
  dumper.append_string("mode", read_write ? "R/W" : "R/O");
}

void ReplicaSet::append_json_status(shcore::JSON_dumper& dumper) const {
  bool single_primary_mode = _topology_type == kTopologyPrimaryMaster;

  // Identifies the master node
  std::string master_uuid;
  if (single_primary_mode) {
    auto uuid_result = _metadata_storage->execute_sql("show status like 'group_replication_primary_member'");
    auto uuid_row = uuid_result->call("fetchOne", shcore::Argument_list());
    if (uuid_row)
      master_uuid = uuid_row.as_object<Row>()->get_member(1).as_string();
  }

  std::string query = "select mysql_server_uuid, instance_name, role, MEMBER_STATE, JSON_UNQUOTE(JSON_EXTRACT(addresses, \"$.mysqlClassic\")) as host"
                      " from mysql_innodb_cluster_metadata.instances left join performance_schema.replication_group_members on `mysql_server_uuid`=`MEMBER_ID`"
                      " where replicaset_id = " + std::to_string(_id);
  auto result = _metadata_storage->execute_sql(query);

  auto raw_instances = result->call("fetchAll", shcore::Argument_list());

  // First we identify the master instance
  auto instances = raw_instances.as_array();

  std::shared_ptr<mysh::Row> master;
  int online_count = 0;
  for (auto value : *instances.get()) {
    auto row = value.as_object<mysh::Row>();
    if (row->get_member(0).as_string() == master_uuid)
      master = row;

    auto status = row->get_member(3);
    if (status && status.as_string() == "ONLINE")
      online_count++;
  }

  std::string rset_status;
  switch (online_count) {
    case 0:
    case 1:
    case 2: rset_status = "Cluster is NOT tolerant to any failures.";
      break;
    case 3: rset_status = "Cluster tolerant to up to ONE failure.";
      break;
      // Add logic on the default for the even/uneven count
    default:rset_status = "Cluster tolerant to up to " + std::to_string(online_count - 2) + " failures.";
      break;
  }

  dumper.start_object();
  dumper.append_string("status", rset_status);
  if (!single_primary_mode) {
    //  dumper.append_string("topology", _topology_type);
  }
  dumper.append_string("topology");
  if (single_primary_mode) {
    dumper.start_object();
    if (master) {
      dumper.append_string(master->get_member(1).as_string());
      dumper.start_object();
      append_member_status(dumper, master, true);
      dumper.append_string("leaves");
      dumper.start_object();
    }
    for (auto value : *instances.get()) {
      auto row = value.as_object<mysh::Row>();
      if (row != master) {
        dumper.append_string(row->get_member(1).as_string());
        dumper.start_object();
        append_member_status(dumper, row, single_primary_mode ? false : true);
        dumper.append_string("leaves");
        dumper.start_object();
        dumper.end_object();
        dumper.end_object();
      }
    }
    if (master) {
      dumper.end_object();
      dumper.end_object();
    }
    dumper.end_object();
  } else {
    dumper.start_object();
    for (auto value : *instances.get()) {
      auto row = value.as_object<mysh::Row>();
      dumper.append_string(row->get_member(1).as_string());
      dumper.start_object();
      append_member_status(dumper, row, true);
      dumper.append_string("leaves");
      dumper.start_object();
      dumper.end_object();
      dumper.end_object();
    }
    dumper.end_object();
  }
  dumper.end_object();
}

void ReplicaSet::append_json_description(shcore::JSON_dumper& dumper) const {
  std::string query = "select mysql_server_uuid, instance_name, role,"
                        " JSON_UNQUOTE(JSON_EXTRACT(addresses, \"$.mysqlClassic\")) as host"
                      " from mysql_innodb_cluster_metadata.instances"
                      " where replicaset_id = " + std::to_string(_id);
  auto result = _metadata_storage->execute_sql(query);

  auto raw_instances = result->call("fetchAll", shcore::Argument_list());

  // First we identify the master instance
  auto instances = raw_instances.as_array();

  dumper.start_object();
  dumper.append_string("name", _name);

  // Creates the instances element
  dumper.append_string("instances");
  dumper.start_array();

  for (auto value : *instances.get()) {
    auto row = value.as_object<mysh::Row>();
    dumper.start_object();
    dumper.append_string("name", row->get_member(1).as_string());
    dumper.append_string("host", row->get_member(3).as_string());
    dumper.append_string("role", row->get_member(2).as_string());
    dumper.end_object();
  }
  dumper.end_array();
  dumper.end_object();
}

void ReplicaSet::append_json(shcore::JSON_dumper& dumper) const {
  if (_json_mode == JSON_STANDARD_OUTPUT)
    shcore::Cpp_object_bridge::append_json(dumper);
  else {
    if (_json_mode == JSON_STATUS_OUTPUT)
      append_json_status(dumper);
    else if (_json_mode == JSON_TOPOLOGY_OUTPUT)
      append_json_description(dumper);
  }
}

bool ReplicaSet::operator == (const Object_bridge &other) const {
  return class_name() == other.class_name() && this == &other;
}

#if DOXYGEN_CPP
/**
 * Use this function to retrieve an valid member of this class exposed to the scripting languages.
 * \param prop : A string containing the name of the member to be returned
 *
 * This function returns a Value that wraps the object returned by this function.
 * The content of the returned value depends on the property being requested.
 * The next list shows the valid properties as well as the returned value for each of them:
 *
 * \li name: returns a String object with the name of this ReplicaSet object.
 */
#else
/**
* Returns the name of this ReplicaSet object.
* \return the name as an String object.
*/
#if DOXYGEN_JS
String ReplicaSet::getName() {}
#elif DOXYGEN_PY
str ReplicaSet::get_name() {}
#endif
#endif
shcore::Value ReplicaSet::get_member(const std::string &prop) const {
  shcore::Value ret_val;
  if (prop == "name")
    ret_val = shcore::Value(_name);
  else
    ret_val = shcore::Cpp_object_bridge::get_member(prop);

  return ret_val;
}

void ReplicaSet::init() {
  add_property("name", "getName");
  add_method("addInstance", std::bind(&ReplicaSet::add_instance_, this, _1), "data");
  add_method("rejoinInstance", std::bind(&ReplicaSet::rejoin_instance, this, _1), "data");
  add_method("removeInstance", std::bind(&ReplicaSet::remove_instance_, this, _1), "data");
  add_varargs_method("disable", std::bind(&ReplicaSet::disable, this, _1));
  add_varargs_method("dissolve", std::bind(&ReplicaSet::dissolve, this, _1));
}

#if DOXYGEN_CPP
/**
 * Use this function to add a Instance to the ReplicaSet object
 * \param args : A list of values to be used to add a Instance to the ReplicaSet.
 *
 * This function returns an empty Value.
 */
#else
/**
* Adds a Instance to the ReplicaSet
* \param conn The Connection String or URI of the Instance to be added
*/
#if DOXYGEN_JS
Undefined ReplicaSet::addInstance(String conn) {}
#elif DOXYGEN_PY
None ReplicaSet::add_instance(str conn) {}
#endif
/**
* Adds a Instance to the ReplicaSet
* \param doc The Document representing the Instance to be added
*/
#if DOXYGEN_JS
Undefined ReplicaSet::addInstance(Document doc) {}
#elif DOXYGEN_PY
None ReplicaSet::add_instance(Document doc) {}
#endif
#endif
shcore::Value ReplicaSet::add_instance_(const shcore::Argument_list &args) {
  shcore::Value ret_val;
  args.ensure_count(1, 2, get_function_name("addInstance").c_str());

  // Check if the ReplicaSet is empty
  if (_metadata_storage->is_replicaset_empty(get_id()))
    throw shcore::Exception::logic_error("ReplicaSet not initialized. Please add the Seed Instance using: addSeedInstance().");

  // Add the Instance to the Default ReplicaSet
  try {
    ret_val = add_instance(args);
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("addInstance"));

  return ret_val;
}

static void run_queries(mysh::mysql::ClassicSession *session, const std::vector<std::string> &queries) {
  for (auto &q : queries) {
    shcore::Argument_list args;
    args.push_back(shcore::Value(q));
    log_info("DBA: run_sql(%s)", q.c_str());
    session->run_sql(args);
  }
}

shcore::Value ReplicaSet::add_instance(const shcore::Argument_list &args) {
  shcore::Value ret_val;

  bool seed_instance = false;
  std::string uri;
  shcore::Value::Map_type_ref options; // Map with the connection data

  std::string user;
  std::string super_user_password;
  std::string host;
  int port = 0;

  // NOTE: This function is called from either the add_instance_ on this class
  //       or the add_instance in Cluster class, hence this just throws exceptions
  //       and the proper handling is done on the caller functions (to append the called function name)

  // Check if we're on a addSeedInstance or not
  if (_metadata_storage->is_replicaset_empty(_id)) seed_instance = true;

  // Identify the type of connection data (String or Document)
  if (args[0].type == String) {
    uri = args.string_at(0);
    options = get_connection_data(uri, false);
  }

  // Connection data comes in a dictionary
  else if (args[0].type == Map)
    options = args.map_at(0);

  else
    throw shcore::Exception::argument_error("Invalid connection options, expected either a URI or a Dictionary.");

  // Verification of invalid attributes on the connection data
  auto invalids = shcore::get_additional_keys(options, _add_instance_opts);
  if (invalids.size()) {
    std::string error = "Unexpected instance options: ";
    error += shcore::join_strings(invalids, ", ");
    throw shcore::Exception::argument_error(error);
  }

  // Verification of required attributes on the connection data
  auto missing = shcore::get_missing_keys(options, {"host", "password|dbPassword"});
  if (missing.find("password") != missing.end() && args.size() == 2)
    missing.erase("password");

  if (missing.size()) {
    std::string error = "Missing instance options: ";
    error += shcore::join_strings(missing, ", ");
    throw shcore::Exception::argument_error(error);
  }

  if (options->has_key("port"))
    port = options->get_int("port");
  else
    port = get_default_port();

  // Sets a default user if not specified
  if (options->has_key("user"))
    user = options->get_string("user");
  else if (options->has_key("dbUser"))
      user = options->get_string("dbUser");
  else {
    user = "root";
    (*options)["dbUser"] = shcore::Value(user);
  }

  host = options->get_string("host");

  std::string password;
  if (options->has_key("password"))
    super_user_password = options->get_string("password");
  else if (options->has_key("dbPassword"))
    super_user_password = options->get_string("dbPassword");
  else if (args.size() == 2 && args[1].type == shcore::String) {
    super_user_password = args.string_at(1);
    (*options)["dbPassword"] = shcore::Value(super_user_password);
  } else
    throw shcore::Exception::argument_error("Missing password for " + build_connection_string(options, false));

  // Check if the instance was already added
  std::string instance_address = options->get_string("host") + ":" + std::to_string(options->get_int("port"));

  if (_metadata_storage->is_instance_on_replicaset(get_id(), instance_address))
    throw shcore::Exception::logic_error("The instance '" + instance_address + "' already belongs to the ReplicaSet: '" + get_member("name").as_string() + "'.");

  std::string instance_admin_user = _cluster->get_account_user(ACC_INSTANCE_ADMIN);
  std::string instance_admin_user_password = _cluster->get_account_password(ACC_INSTANCE_ADMIN);
  std::string cluster_admin_user = _cluster->get_account_user(ACC_CLUSTER_ADMIN);
  std::string cluster_admin_user_password = _cluster->get_account_password(ACC_CLUSTER_ADMIN);
  std::string replication_user = _cluster->get_account_user(ACC_REPLICATION_USER);
  std::string replication_user_password = _cluster->get_account_password(ACC_REPLICATION_USER);
  std::string cluster_reader_user = _cluster->get_account_user(ACC_CLUSTER_READER);
  std::string cluster_reader_user_password = _cluster->get_account_password(ACC_CLUSTER_READER);

  // Drop and create users
  shcore::Argument_list temp_args;

  shcore::Argument_list new_args;
  new_args.push_back(shcore::Value(options));
  auto session = mysh::connect_session(new_args, mysh::Classic);
  mysh::mysql::ClassicSession *classic = dynamic_cast<mysh::mysql::ClassicSession*>(session.get());

  MetadataStorage::Transaction tx(_metadata_storage);

  temp_args.clear();
  temp_args.push_back(shcore::Value("SET sql_log_bin = 0"));
  classic->run_sql(temp_args);

  run_queries(classic, {
    "DROP USER IF EXISTS '" + instance_admin_user + "'@'" + host + "'",
    "CREATE USER '" + instance_admin_user + "'@'" + host + "' IDENTIFIED BY '" + instance_admin_user_password + "'",
    "GRANT PROCESS, RELOAD, REPLICATION CLIENT, REPLICATION SLAVE, CREATE USER, SUPER ON *.* TO '" + instance_admin_user + "'@'" + host + "'",
    "GRANT ALL ON mysql_innodb_cluster_metadata.* TO '" + instance_admin_user + "'@'" + host + "'",
    "GRANT SELECT ON performance_schema.* TO '" + instance_admin_user + "'@'" + host + "'"
  });

  run_queries(classic, {
    "DROP USER IF EXISTS '" + cluster_reader_user + "'@'%'",
    "CREATE USER IF NOT EXISTS '" + cluster_reader_user + "'@'%' IDENTIFIED BY '" + cluster_reader_user_password + "'",
    "GRANT SELECT ON mysql_innodb_cluster_metadata.* to '" + cluster_reader_user + "'@'%'",
    "GRANT SELECT ON performance_schema.replication_group_members to '" + cluster_reader_user + "'@'%'"
  });

  run_queries(classic, {
    "DROP USER IF EXISTS '" + cluster_admin_user + "'@'" + host + "'",
    "CREATE USER '" + cluster_admin_user + "'@'" + host + "' IDENTIFIED BY '" + cluster_admin_user_password + "'",
    "GRANT ALL ON mysql_innodb_cluster_metadata.* TO '" + cluster_admin_user + "'@'" + host + "'",
    "GRANT SELECT ON performance_schema.replication_group_members to '" + cluster_admin_user + "'@'" + host + "'"
  });

  run_queries(classic, {
    "DROP USER IF EXISTS '" + replication_user + "'@'%'",
    "CREATE USER IF NOT EXISTS '" + replication_user + "'@'%' IDENTIFIED BY '" + replication_user_password + "'",
    "GRANT REPLICATION SLAVE ON *.* to '" + replication_user + "'@'%'"
  });

  temp_args.clear();
  temp_args.push_back(shcore::Value("SET sql_log_bin = 1"));
  classic->run_sql(temp_args);

  temp_args.clear();
  temp_args.push_back(shcore::Value("SELECT @@server_uuid"));
  auto uuid_raw_result = classic->run_sql(temp_args);
  auto uuid_result = uuid_raw_result.as_object<mysh::mysql::ClassicResult>();

  auto uuid_row = uuid_result->fetch_one(shcore::Argument_list());

  std::string mysql_server_uuid;
  if (uuid_row)
    mysql_server_uuid = uuid_row.as_object<mysh::Row>()->get_member(0).as_string();

  // Call the gadget to bootstrap the group with this instance
  if (seed_instance) {
    // Call mysqlprovision to bootstrap the group using "start"
    do_join_replicaset(user + "@" + host + ":" + std::to_string(port),
        "",
        super_user_password,
        replication_user, replication_user_password);
  } else {
    // We need to retrieve a peer instance, so let's use the Seed one
    auto instances = _metadata_storage->get_replicaset_instances(get_id());

    if (!instances || instances->empty())
      throw Exception::logic_error("Cannot add Instance: ReplicaSet empty.");

    // The seed is always the first
    auto value = instances->at(0);
    auto row = value.as_object<mysh::Row>();
    std::string peer_instance = row->get_member("instance_name").as_string();
    // TODO: if it fails re-try with the other instances of the ReplicaSet

    // Call mysqlprovision to do the work
    do_join_replicaset(user + "@" + host + ":" + std::to_string(port),
        user + "@" + peer_instance,
        super_user_password,
        replication_user, replication_user_password);
  }

  // OK, if we reached here without errors we can update the metadata with the host
  auto result = _metadata_storage->insert_host(args);

  // And the instance
  uint64_t host_id = result->get_member("autoIncrementValue").as_int();

  shcore::Argument_list args_instance;
  Value::Map_type_ref options_instance(new shcore::Value::Map_type);
  args_instance.push_back(shcore::Value(options_instance));

  (*options_instance)["role"] = shcore::Value("HA");

  std::string address = host + ":" + std::to_string(port);
  shcore::Value val_address = shcore::Value(address);
  (*options_instance)["addresses"] = val_address;

  (*options_instance)["mysql_server_uuid"] = shcore::Value(mysql_server_uuid);

  if (options->has_key("name"))
    (*options_instance)["instance_name"] = (*options)["name"];
  else
    (*options_instance)["instance_name"] = val_address;

  _metadata_storage->insert_instance(args_instance, host_id, get_id());

  tx.commit();

  return ret_val;
}

bool ReplicaSet::do_join_replicaset(const std::string &instance_url,
    const std::string &peer_instance_url,
    const std::string &super_user_password,
    const std::string &repl_user, const std::string &repl_user_password) {
  shcore::Value ret_val;
  int exit_code = -1;

  bool is_seed_instance = peer_instance_url.empty() ? true : false;

  std::string command, errors;

  if (is_seed_instance) {
    exit_code = _cluster->get_provisioning_interface()->start_replicaset(instance_url,
                      repl_user, super_user_password,
                      repl_user_password,
                      _topology_type == kTopologyMultiMaster,
                      errors);
  } else {
    exit_code = _cluster->get_provisioning_interface()->join_replicaset(instance_url,
                      repl_user, peer_instance_url,
                      super_user_password, repl_user_password,
                      _topology_type == kTopologyMultiMaster,
                      errors);
  }

  if (exit_code == 0) {
    if (is_seed_instance)
      ret_val = shcore::Value("The instance '" + instance_url + "' was successfully added as seeding instance to the MySQL Cluster.");
    else
      ret_val = shcore::Value("The instance '" + instance_url + "' was successfully added to the MySQL Cluster.");
  } else
    throw shcore::Exception::logic_error(errors);

  return exit_code == 0;
}

#if DOXYGEN_CPP
/**
 * Use this function to rejoin an Instance to the ReplicaSet
 * \param args : A list of values to be used to add a Instance to the ReplicaSet.
 *
 * This function returns an empty Value.
 */
#else
/**
* Rejoin a Instance to the ReplicaSet
* \param name The name of the Instance to be rejoined
*/
#if DOXYGEN_JS
Undefined ReplicaSet::rejoinInstance(String name, Dictionary options) {}
#elif DOXYGEN_PY
None ReplicaSet::rejoin_instance(str name, Dictionary options) {}
#endif
#endif // DOXYGEN_CPP
shcore::Value ReplicaSet::rejoin_instance(const shcore::Argument_list &args) {
  shcore::Value ret_val;
  args.ensure_count(1, 2, get_function_name("rejoinInstance").c_str());
  // Check if the ReplicaSet is empty
  if (_metadata_storage->is_replicaset_empty(get_id()))
    throw shcore::Exception::logic_error(
        "ReplicaSet not initialized. Please add the Seed Instance using: addSeedInstance().");

  // Add the Instance to the Default ReplicaSet
  try {
    shcore::Value::Map_type_ref options; // Map with the connection data
    std::string super_user_password;
    std::string host;
    int port = 0;

    // Identify the type of connection data (String or Document)
    if (args[0].type == String) {
      std::string uri = args.string_at(0);
      options = get_connection_data(uri, false);
    } else
     throw shcore::Exception::argument_error(
            "Invalid connection options, expected either a URI.");
    // Verification of required attributes on the connection data
    auto missing = shcore::get_missing_keys(options, {"host"});
    if (missing.size()) {
      std::string error = "Missing instance options: ";
      error += shcore::join_strings(missing, ", ");
      throw shcore::Exception::argument_error(error);
    }
    if (options->has_key("port"))
      port = options->get_int("port");
    else
      port = get_default_port();
    std::string peer_instance = _metadata_storage->get_seed_instance(get_id());
    if (peer_instance.empty()) {
      throw shcore::Exception::runtime_error("Cannot rejoin instance. There are no remaining available instances in the replicaset.");
    }
    std::string user;
    // Sets a default user if not specified
    if (options->has_key("user"))
      user = options->get_string("user");
    else if (options->has_key("dbUser"))
      user = options->get_string("dbUser");
    else {
      user = "root";
      (*options)["dbUser"] = shcore::Value(user);
    }
    host = options->get_string("host");

    std::string password;
    if (options->has_key("password"))
      super_user_password = options->get_string("password");
    else if (options->has_key("dbPassword"))
      super_user_password = options->get_string("dbPassword");
    else if (args.size() == 2 && args[1].type == shcore::String) {
      super_user_password = args.string_at(1);
      (*options)["dbPassword"] = shcore::Value(super_user_password);
    } else
      throw shcore::Exception::argument_error("Missing password for " + build_connection_string(options, false));
    std::string cluster_admin_user = _cluster->get_account_user(ACC_INSTANCE_ADMIN);
    std::string cluster_admin_user_password = _cluster->get_account_password(ACC_INSTANCE_ADMIN);
    std::string replication_user = _cluster->get_account_user(ACC_REPLICATION_USER);
    std::string replication_user_password = _cluster->get_account_password(ACC_REPLICATION_USER);

    do_join_replicaset(user + "@" + host + ":" + std::to_string(port),
        user + "@" + peer_instance,
        super_user_password,
        replication_user, replication_user_password);
  } CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("addInstance"));

  return ret_val;
}

#if DOXYGEN_CPP
/**
 * Use this function to remove a Instance from the ReplicaSet object
 * \param args : A list of values to be used to remove a Instance to the Cluster.
 *
 * This function returns an empty Value.
 */
#else
/**
* Removes a Instance from the ReplicaSet
* \param name The name of the Instance to be removed
*/
#if DOXYGEN_JS
Undefined ReplicaSet::removeInstance(String name) {}
#elif DOXYGEN_PY
None ReplicaSet::remove_instance(str name) {}
#endif

/**
* Removes a Instance from the ReplicaSet
* \param doc The Document representing the Instance to be removed
*/
#if DOXYGEN_JS
Undefined ReplicaSet::removeInstance(Document doc) {}
#elif DOXYGEN_PY
None ReplicaSet::remove_instance(Document doc) {}
#endif
#endif

shcore::Value ReplicaSet::remove_instance_(const shcore::Argument_list &args) {
  shcore::Value ret_val;
  args.ensure_count(1, get_function_name("removeInstance").c_str());

  // Remove the Instance from the Default ReplicaSet
  try {
    ret_val = remove_instance(args);
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("removeInstance"));

  return ret_val;
}

shcore::Value ReplicaSet::remove_instance(const shcore::Argument_list &args) {
  args.ensure_count(1, get_function_name("removeInstance").c_str());

  std::string uri;
  shcore::Value::Map_type_ref options; // Map with the connection data

  std::string host;
  std::string name;
  int port = 0;

  // Identify the type of connection data (String or Document)
  if (args[0].type == String) {
    uri = args.string_at(0);
    options = get_connection_data(uri, false);
  }

  // TODO: what if args[0] is a String containing the name of the instance?

  // Connection data comes in a dictionary
  else if (args[0].type == Map)
    options = args.map_at(0);

  else
    throw shcore::Exception::argument_error("Invalid connection options, expected either a URI or a Dictionary.");

  // Verification of invalid attributes on the connection data
  auto invalids = shcore::get_additional_keys(options, _remove_instance_opts);
  if (invalids.size()) {
    std::string error = "Unexpected instance options: ";
    error += shcore::join_strings(invalids, ", ");
    throw shcore::Exception::argument_error(error);
  }

  if (options->has_key("port"))
    port = options->get_int("port");
  else
    port = get_default_port();

  std::string instance_admin_user = _cluster->get_account_user(ACC_INSTANCE_ADMIN);
  std::string instance_admin_user_password = _cluster->get_account_password(ACC_INSTANCE_ADMIN);
  std::string cluster_reader_user = _cluster->get_account_user(ACC_CLUSTER_READER);

  host = options->get_string("host");

  // Check if the instance exists on the ReplicaSet
  std::string instance_address = options->get_string("host") + ":" + std::to_string(options->get_int("port"));

  if (!_metadata_storage->is_instance_on_replicaset(get_id(), instance_address))
    throw shcore::Exception::logic_error("The instance '" + instance_address + "' does not belong to the ReplicaSet: '" + get_member("name").as_string() + "'.");

  MetadataStorage::Transaction tx(_metadata_storage);

  // Update the Metadata

  // TODO: do we remove the host? we check if is the last instance of that host and them remove?
  // auto result = _metadata_storage->remove_host(args);

  // TODO: the instance_name can be actually a name, check TODO above
  std::string instance_name = host + ":" + std::to_string(port);

  _metadata_storage->remove_instance(instance_name);

  tx.commit();

  // call provisioning to remove the instance from the replicaset
  int exit_code = -1;
  std::string errors, instance_url;

  instance_url = instance_admin_user + "@" + host + ":" + std::to_string(port);

  exit_code = _cluster->get_provisioning_interface()->leave_replicaset(instance_url, instance_admin_user_password, errors);

  if (exit_code != 0)
    throw shcore::Exception::logic_error(errors);

  // Drop users
  shcore::Argument_list temp_args, new_args;

  (*options)["dbUser"] = shcore::Value(instance_admin_user);
  (*options)["dbPassword"] = shcore::Value(instance_admin_user_password);
  new_args.push_back(shcore::Value(options));

  auto session = mysh::connect_session(new_args, mysh::Classic);
  mysh::mysql::ClassicSession *classic = dynamic_cast<mysh::mysql::ClassicSession*>(session.get());

  temp_args.clear();
  temp_args.push_back(shcore::Value("SET sql_log_bin = 0"));
  classic->run_sql(temp_args);

  run_queries(classic, {
    "DROP USER IF EXISTS '" + instance_admin_user + "'@'" + host + "'",
    "DROP USER IF EXISTS '" + cluster_reader_user + "'@'" + host + "'",
  });

  temp_args.clear();
  temp_args.push_back(shcore::Value("SET sql_log_bin = 1"));
  classic->run_sql(temp_args);

  return shcore::Value();
}

shcore::Value ReplicaSet::dissolve(const shcore::Argument_list &args) {
  shcore::Value ret_val;
  args.ensure_count(0, 1, get_function_name("dissolve").c_str());

  try {
    bool force = false;
    shcore::Value::Map_type_ref options;

    if (args.size() == 1)
      options = args.map_at(0);

    if (options) {
      // Verification of invalid attributes on the instance creation options
      auto invalids = shcore::get_additional_keys(options, {"force"});
      if (invalids.size()) {
        std::string error = "The options contain the following invalid attributes: ";
        error += shcore::join_strings(invalids, ", ");
        throw shcore::Exception::argument_error(error);
      }

      if (options->has_key("force")) {
        if ((*options)["force"].type != shcore::Bool)
          throw shcore::Exception::type_error("Invalid data type for 'force' field, should be a boolean");
        force = options->get_bool("force");
      }
    }

    if (force) {
      disable(shcore::Argument_list());
    } else if (_metadata_storage->is_replicaset_active(get_id()))
      throw shcore::Exception::logic_error("Cannot dissolve the ReplicaSet: the ReplicaSet is active.");

    MetadataStorage::Transaction tx(_metadata_storage);

    // remove all the instances from the ReplicaSet
    auto instances = _metadata_storage->get_replicaset_instances(get_id());

    for (auto value : *instances.get()) {
      auto row = value.as_object<mysh::Row>();

      std::string instance_name = row->get_member(1).as_string();
      shcore::Argument_list args_instance;
      args_instance.push_back(shcore::Value(instance_name));

      remove_instance(args_instance);
    }

    // Remove the replicaSet
    _metadata_storage->drop_replicaset(get_id());

    tx.commit();
  } CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("dissolve"));

  return ret_val;
}

shcore::Value ReplicaSet::disable(const shcore::Argument_list &args) {
  shcore::Value ret_val;

  args.ensure_count(0, get_function_name("disable").c_str());

  try {
    MetadataStorage::Transaction tx(_metadata_storage);

    std::string instance_admin_user = _cluster->get_account_user(ACC_INSTANCE_ADMIN);
    std::string instance_admin_user_password = _cluster->get_account_password(ACC_INSTANCE_ADMIN);

    // Get all instances of the replicaset
    auto instances = _metadata_storage->get_replicaset_instances(get_id());

    for (auto value : *instances.get()) {
      int exit_code;
      auto row = value.as_object<mysh::Row>();

      std::string instance_name = row->get_member(1).as_string();
      std::string instance_url = instance_admin_user + "@" + instance_name;
      std::string errors;

      // Leave the replicaset
      exit_code = _cluster->get_provisioning_interface()->leave_replicaset(instance_url,
                                            instance_admin_user_password, errors);

      if (exit_code != 0)
        throw shcore::Exception::logic_error(errors);
    }

    // Update the metadata to turn 'active' off
    _metadata_storage->disable_replicaset(get_id());
    tx.commit();
  } CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("disable"));

  return ret_val;
}