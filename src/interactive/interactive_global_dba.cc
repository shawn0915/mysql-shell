/*
 * Copyright (c) 2016, 2018, Oracle and/or its affiliates. All rights reserved.
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

#include "interactive/interactive_global_dba.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "interactive/interactive_dba_cluster.h"
#include "modules/adminapi/dba/check_instance.h"
#include "modules/adminapi/mod_dba.h"
#include "modules/adminapi/mod_dba_common.h"
#include "modules/adminapi/mod_dba_sql.h"
#include "modules/mod_utils.h"
#include "modules/mysqlxtest_utils.h"
#include "mysqlshdk/include/shellcore/base_shell.h"
#include "mysqlshdk/include/shellcore/console.h"
#include "shellcore/utils_help.h"
#include "utils/utils_file.h"
#include "utils/utils_general.h"
#include "utils/utils_net.h"
#include "utils/utils_path.h"
#include "utils/utils_string.h"

using std::placeholders::_1;
using namespace shcore;
using mysqlshdk::db::uri::formats::only_transport;

void Global_dba::init() {
  add_varargs_method("deploySandboxInstance",
                     std::bind(&Global_dba::deploy_sandbox_instance, this, _1,
                               "deploySandboxInstance"));
  add_varargs_method("startSandboxInstance",
                     std::bind(&Global_dba::start_sandbox_instance, this, _1));
  add_varargs_method("deleteSandboxInstance",
                     std::bind(&Global_dba::delete_sandbox_instance, this, _1));
  add_varargs_method("killSandboxInstance",
                     std::bind(&Global_dba::kill_sandbox_instance, this, _1));
  add_varargs_method("stopSandboxInstance",
                     std::bind(&Global_dba::stop_sandbox_instance, this, _1));
  add_varargs_method("getCluster",
                     std::bind(&Global_dba::get_cluster, this, _1));
  add_varargs_method(
      "rebootClusterFromCompleteOutage",
      std::bind(&Global_dba::reboot_cluster_from_complete_outage, this, _1));

  add_method("createCluster", std::bind(&Global_dba::create_cluster, this, _1),
             "clusterName", shcore::String);
  add_method("dropMetadataSchema",
             std::bind(&Global_dba::drop_metadata_schema, this, _1), "data",
             shcore::Map);
}

mysqlsh::dba::Cluster_check_info Global_dba::check_preconditions(
    std::shared_ptr<mysqlshdk::db::ISession> group_session,
    const std::string &function_name) const {
  ScopedStyle ss(_target.get(), naming_style);
  auto dba = std::dynamic_pointer_cast<mysqlsh::dba::Dba>(_target);
  return dba->check_preconditions(group_session, function_name);
}

std::vector<std::pair<std::string, std::string>>
Global_dba::get_replicaset_instances_status(
    std::shared_ptr<mysqlsh::dba::Cluster> cluster,
    const shcore::Value::Map_type_ref &options) const {
  ScopedStyle ss(_target.get(), naming_style);
  auto dba = std::dynamic_pointer_cast<mysqlsh::dba::Dba>(_target);
  return dba->get_replicaset_instances_status(cluster, options);
}

void Global_dba::validate_instances_status_reboot_cluster(
    std::shared_ptr<mysqlsh::dba::Cluster> cluster,
    std::shared_ptr<mysqlshdk::db::ISession> member_session,
    shcore::Value::Map_type_ref options) const {
  ScopedStyle ss(_target.get(), naming_style);
  auto dba = std::dynamic_pointer_cast<mysqlsh::dba::Dba>(_target);
  return dba->validate_instances_status_reboot_cluster(cluster, member_session,
                                                       options);
}

shcore::Argument_list Global_dba::check_instance_op_params(
    const shcore::Argument_list &args, const std::string &function_name) {
  shcore::Value ret_val;
  shcore::Argument_list new_args;

  shcore::Value::Map_type_ref options;  // Map with the connection data

  // Initialize sandboxDir with the default sandboxValue
  std::string sandboxDir =
      mysqlsh::current_shell_options()->get().sandbox_directory;

  new_args.push_back(args[0]);

  if (args.size() == 2) {
    new_args.push_back(args[1]);
    options = args.map_at(1);

    shcore::Argument_map opt_map(*options);

    if (function_name == "deploySandboxInstance") {
      opt_map.ensure_keys({}, mysqlsh::dba::Dba::_deploy_instance_opts,
                          "the instance definition");
    } else if (function_name == "stopSandboxInstance") {
      opt_map.ensure_keys({}, mysqlsh::dba::Dba::_stop_instance_opts,
                          "the instance definition");
    } else {
      opt_map.ensure_keys({}, mysqlsh::dba::Dba::_default_local_instance_opts,
                          "the instance definition");
    }

    if (opt_map.has_key("sandboxDir")) {
      sandboxDir = opt_map.string_at("sandboxDir");
      // When the user specifies the sandbox dir we validate it
      if (!shcore::is_folder(sandboxDir))
        throw shcore::Exception::argument_error("The sandboxDir path '" +
                                                sandboxDir + "' is not valid");
    }
    // Store sandboxDir value
    (*options)["sandboxDir"] = shcore::Value(sandboxDir);
  } else {
    options.reset(new shcore::Value::Map_type());
    (*options)["sandboxDir"] = shcore::Value(sandboxDir);
    new_args.push_back(shcore::Value(options));
  }
  return new_args;
}

shcore::Value Global_dba::deploy_sandbox_instance(
    const shcore::Argument_list &args, const std::string &fname) {
  args.ensure_count(1, 2, get_function_name(fname).c_str());

  shcore::Argument_list valid_args;
  int port;
  bool deploying = (fname == "deploySandboxInstance");
  bool cancelled = false;
  try {
    // Verifies and sets default args
    // After this there is port and options
    // options at least contains sandboxDir
    valid_args = check_instance_op_params(args, fname);
    port = valid_args.int_at(0);
    auto options = valid_args.map_at(1);

    std::string sandbox_dir{options->get_string("sandboxDir")};

    bool prompt_password = !options->has_key("password");

    std::string message;
    if (prompt_password) {
      std::vector<std::string> paths{sandbox_dir, std::to_string(port)};
      std::string path = shcore::path::join_path(paths);
      if (deploying) {
        message =
            "A new MySQL sandbox instance will be created on this host "
            "in \n" +
            path +
            "\n\n"
            "Warning: Sandbox instances are only suitable for deploying and \n"
            "running on your local machine for testing purposes and are not \n"
            "accessible from external networks.\n\n"
            "Please enter a MySQL root password for the new instance: ";
      } else {
        message =
            "The MySQL sandbox instance on this host in \n"
            "" +
            path +
            " will be started\n\n"
            "Warning: Sandbox instances are only suitable for deploying and \n"
            "running on your local machine for testing purposes and are not \n"
            "accessible from external networks.\n\n"
            "Please enter the MySQL root password of the instance: ";
      }

      std::string answer;
      if (password(message, answer))
        (*options)["password"] = shcore::Value(answer);
      else
        cancelled = true;
    }
    if (!options->has_key("allowRootFrom")) {
      // If the user didn't specify the option allowRootFrom we
      // automatically use '%'
      std::string pattern = "%";
      (*options)["allowRootFrom"] = shcore::Value(pattern);
    }
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name(fname));

  shcore::Value ret_val;

  if (!cancelled) {
    if (deploying)
      println("Deploying new MySQL instance...");
    else
      println("Starting MySQL instance...");

    ret_val = call_target(fname, valid_args);

    println();
    if (deploying)
      println("Instance localhost:" + std::to_string(port) +
              " successfully deployed and started.");
    else
      println("Instance localhost:" + std::to_string(port) +
              " successfully started.");

    println("Use shell.connect('root@localhost:" + std::to_string(port) +
            "'); to connect to the instance.");
    println();
  }

  return ret_val;
}

shcore::Value Global_dba::perform_instance_operation(
    const shcore::Argument_list &args, const std::string &fname,
    const std::string &progressive, const std::string &past) {
  shcore::Argument_list valid_args;
  args.ensure_count(1, 2, get_function_name(fname).c_str());

  try {
    valid_args = check_instance_op_params(args, fname);
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name(fname));

  int port = valid_args.int_at(0);
  auto options = valid_args.map_at(1);

  std::string sandboxDir{options->get_string("sandboxDir")};
  std::vector<std::string> paths{sandboxDir, std::to_string(port)};
  std::string path = shcore::path::join_path(paths);
  std::string message =
      "The MySQL sandbox instance on this host in \n"
      "" +
      path + " will be " + past + "\n";

  println(message);

  if (fname == "stopSandboxInstance") {
    bool prompt_password = !options->has_key("password");

    if (prompt_password) {
      std::string message =
          "Please enter the MySQL root password for the "
          "instance 'localhost:" +
          std::to_string(port) + "': ";

      std::string answer;
      if (password(message, answer))
        (*options)["password"] = shcore::Value(answer);
    }
  }

  println();
  println(progressive + " MySQL instance...");
  {
    shcore::Value ret_val = call_target(fname, valid_args);

    println();
    println("Instance localhost:" + std::to_string(port) + " successfully " +
            past + ".");
    println();

    return ret_val;
  }
}

shcore::Value Global_dba::delete_sandbox_instance(
    const shcore::Argument_list &args) {
  return perform_instance_operation(args, "deleteSandboxInstance", "Deleting",
                                    "deleted");
}

shcore::Value Global_dba::kill_sandbox_instance(
    const shcore::Argument_list &args) {
  return perform_instance_operation(args, "killSandboxInstance", "Killing",
                                    "killed");
}

shcore::Value Global_dba::stop_sandbox_instance(
    const shcore::Argument_list &args) {
  return perform_instance_operation(args, "stopSandboxInstance", "Stopping",
                                    "stopped");
}

shcore::Value Global_dba::start_sandbox_instance(
    const shcore::Argument_list &args) {
  return perform_instance_operation(args, "startSandboxInstance", "Starting",
                                    "started");
}

shcore::Value Global_dba::create_cluster(const shcore::Argument_list &args) {
  args.ensure_count(1, 2, get_function_name("createCluster").c_str());

  mysqlsh::dba::Cluster_check_info state;
  auto dba = std::dynamic_pointer_cast<mysqlsh::dba::Dba>(_target);
  std::shared_ptr<mysqlshdk::db::ISession> member_session;

  shcore::Value ret_val;
  shcore::Value::Map_type_ref options;
  std::string cluster_name;

  bool adopt_from_gr = false;
  bool clear_read_only = false;

  try {
    cluster_name = args.string_at(0);
    bool multi_primary = false;
    bool force = false;
    bool prompt_read_only = true;
    // Validate the cluster_name
    mysqlsh::dba::validate_cluster_name(cluster_name);

    if (args.size() > 1) {
      // Handle the deprecation of multiMaster first

      // Map with the options
      options = args.map_at(1);

      // Verification of invalid attributes on the instance creation options
      shcore::Argument_map opt_map(*options);

      if (opt_map.has_key("multiPrimary") && opt_map.has_key("multiMaster"))
        throw shcore::Exception::argument_error(
            "Cannot use the multiMaster and multiPrimary options "
            "simultaneously. The multiMaster option is deprecated, please use "
            "the multiPrimary option instead.");

      std::string ssl_mode, group_name, local_address, group_seeds,
          exit_state_action, ip_whitelist, failover_consistency;

      mysqlshdk::utils::nullable<int64_t> member_weight, expel_timeout;

      // Retrieves optional options if exists
      mysqlsh::Unpack_options(options)
          .optional("multiPrimary", &multi_primary)
          .optional("multiMaster", &multi_primary)
          .optional("force", &force)
          .optional("adoptFromGR", &adopt_from_gr)
          .optional("memberSslMode", &ssl_mode)
          .optional("clearReadOnly", &clear_read_only)
          .optional("ipWhitelist", &ip_whitelist)
          .optional("groupName", &group_name)
          .optional("localAddress", &local_address)
          .optional("groupSeeds", &group_seeds)
          .optional("exitStateAction", &exit_state_action)
          .optional("memberWeight", &member_weight)
          .optional("failoverConsistency", &failover_consistency)
          .optional_exact("expelTimeout", &expel_timeout)
          .end();

      // Validate SSL options for the cluster instance
      mysqlsh::dba::validate_ssl_instance_options(options);

      if (opt_map.has_key("clearReadOnly")) {
        prompt_read_only = false;
      }

      if (adopt_from_gr && opt_map.has_key("multiPrimary")) {
        throw shcore::Exception::argument_error(
            "Cannot use multiPrimary option if adoptFromGR is set to true."
            " Using adoptFromGR mode will adopt the primary mode in use by the "
            "Cluster.");
      }

      if (adopt_from_gr && opt_map.has_key("multiMaster")) {
        throw shcore::Exception::argument_error(
            "Cannot use multiMaster option if adoptFromGR is set to true."
            " Using adoptFromGR mode will adopt the primary mode in use by the "
            "Cluster.");
      }
    } else {
      options.reset(new shcore::Value::Map_type());
    }

    member_session = dba->connect_to_target_member();
    try {
      state = check_preconditions(member_session, "createCluster");
    } catch (shcore::Exception &e) {
      std::string error(e.what());
      if (error.find("already in an InnoDB cluster") != std::string::npos) {
        /*
         * For V1.0 we only support one single Cluster.
         * That one shall be the default Cluster.
         * We must check if there's already a Default Cluster assigned,
         * and if so thrown an exception.
         * And we must check if there's already one Cluster on the MD and if so
         * assign it to Default
         */

        std::string nice_error = get_function_name("createCluster") +
                                 ": Unable "
                                 "to create cluster. The instance '" +
                                 member_session->uri(only_transport()) +
                                 ""
                                 "' already belongs to an InnoDB cluster. Use "
                                 "<Dba>." +
                                 get_function_name("getCluster", false) +
                                 "() to access it.";

        throw Exception::runtime_error(nice_error);
      } else {
        throw;
      }
    }

    if (state.source_type == mysqlsh::dba::GRInstanceType::GroupReplication &&
        !adopt_from_gr) {
      if (confirm(
              "You are connected to an instance that belongs to an unmanaged "
              "replication group.\nDo you want to setup an InnoDB cluster "
              "based on this replication group?") ==
          mysqlsh::Prompt_answer::YES) {
        (*options)["adoptFromGR"] = shcore::Value(true);
        adopt_from_gr = true;
      } else {
        throw Exception::argument_error(
            "Creating a cluster on an unmanaged "
            "replication group requires "
            "adoptFromGR option to be true");
      }
    }

    if (adopt_from_gr &&
        state.source_type != mysqlsh::dba::GRInstanceType::GroupReplication)
      throw Exception::argument_error(
          "The adoptFromGR option is set to true, but there is no replication "
          "group to adopt");

    println(std::string{"A new InnoDB cluster will be created"} +
            (adopt_from_gr ? " based on the existing replication group" : "") +
            " on instance '" +
            member_session->uri(
                mysqlshdk::db::uri::formats::no_scheme_no_password()) +
            "'.\n");

    if (multi_primary && !force) {
      println(
          "The MySQL InnoDB cluster is going to be setup in advanced "
          "Multi-Primary Mode.\n"
          "Before continuing you have to confirm that you understand the "
          "requirements and\n"
          "limitations of Multi-Primary Mode. For more information see\n"
          "https://dev.mysql.com/doc/refman/en/"
          "group-replication-limitations.html\n"
          "before proceeding.\n"
          "\n");

      println(
          "I have read the MySQL InnoDB cluster manual and I understand the "
          "requirements\n"
          "and limitations of advanced Multi-Primary Mode.");
      if (confirm("Confirm", mysqlsh::Prompt_answer::NO) ==
          mysqlsh::Prompt_answer::NO) {
        println();
        println("Cancelled");
        return shcore::Value();
      } else {
        println();
        (*options)["force"] = shcore::Value(true);
      }
    }

    // Verify the status of super_read_only and ask the user if wants
    // to disable it
    // NOTE: this is left for last to avoid setting super_read_only to true
    // and right before some execution failure of the command leaving the
    // instance in an incorrect state
    if (prompt_read_only) {
      if (!prompt_super_read_only(member_session, options))
        return shcore::Value();
    }
  }
  CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(get_function_name("createCluster"));

  assert(member_session);

  shcore::Argument_list new_args;
  new_args.push_back(shcore::Value(cluster_name));

  if (options != NULL) new_args.push_back(shcore::Value(options));

  // This is an instance of the API cluster
  auto raw_cluster = call_target("createCluster", new_args);

  auto dba_cluster =
      std::dynamic_pointer_cast<mysqlsh::dba::Cluster>(raw_cluster.as_object());
  auto default_replicaset = dba_cluster->get_default_replicaset();

  assert(default_replicaset);

  bool single_primary_mode = default_replicaset->get_topology_type() ==
                             mysqlsh::dba::ReplicaSet::kTopologySinglePrimary;

  std::string master_uuid;

  if (single_primary_mode) {
    println("Adding Seed Instance...");

    mysqlsh::dba::get_status_variable(dba_cluster->get_group_session(),
                                      "group_replication_primary_member",
                                      master_uuid, false);
  }

  if (adopt_from_gr) {
    for (auto &instance : default_replicaset->get_instances_from_metadata()) {
      if (instance.uuid != master_uuid) {
        println("Adding Instance '" + instance.label + "'...");
      }
    }
  }

  println();

  std::string message =
      adopt_from_gr
          ? "Cluster successfully created based on existing "
            "replication group."
          : "Cluster successfully created. Use Cluster." +
                get_member_name("addInstance", naming_style) +
                "() to add MySQL instances.\n"
                "At least 3 instances are needed for the cluster to be "
                "able to withstand up to\n"
                "one server failure.";

  println(message);
  println();

  // Returns an interactive wrapper of this instance
  Interactive_dba_cluster *cluster =
      new Interactive_dba_cluster(this->_shell_core);
  cluster->set_target(dba_cluster);
  ret_val = shcore::Value::wrap<Interactive_dba_cluster>(cluster);

  member_session->close();

  return ret_val;
}

shcore::Value Global_dba::drop_metadata_schema(
    const shcore::Argument_list &args) {
  args.ensure_count(0, 1, get_function_name("dropMetadataSchema").c_str());
  auto dba = std::dynamic_pointer_cast<mysqlsh::dba::Dba>(_target);

  check_preconditions(dba->get_active_shell_session(), "dropMetadataSchema");

  shcore::Value ret_val;
  shcore::Argument_list new_args;
  bool force = false;
  bool prompt_read_only = true;
  bool prompt_drop_confirmation = true;
  Value::Map_type_ref options;

  if (args.size() < 1) {
    options.reset(new shcore::Value::Map_type);
  } else {
    try {
      options = args.map_at(0);
      shcore::Argument_map opt_map(*options);
      opt_map.ensure_keys({}, {"force", "clearReadOnly"}, "drop options");

      if (opt_map.has_key("force")) {
        force = opt_map.bool_at("force");
        prompt_drop_confirmation = false;
      }

      if (opt_map.has_key("clearReadOnly")) {
        // This call is done only to validate the passed data
        opt_map.bool_at("clearReadOnly");
        prompt_read_only = false;
      }
    }
    CATCH_AND_TRANSLATE_FUNCTION_EXCEPTION(
        get_function_name("dropMetadataSchema"))
  }

  if (prompt_drop_confirmation &&
      confirm("Are you sure you want to remove the Metadata?",
              mysqlsh::Prompt_answer::NO) == mysqlsh::Prompt_answer::YES) {
    (*options)["force"] = shcore::Value(true);
    force = true;
  }

  if (force) {
    // Verify the status of super_read_only and ask the user if wants
    // to disable it
    if (prompt_read_only) {
      auto session = dba->get_active_shell_session();
      if (!prompt_super_read_only(session, options)) return shcore::Value();

      if (args.size() < 1) new_args.push_back(shcore::Value(options));
    }

    ret_val =
        call_target("dropMetadataSchema", (args.size() < 1) ? new_args : args);
    println("Metadata Schema successfully removed.");
  } else {
    println("No changes made to the Metadata Schema.");
  }
  println();

  return ret_val;
}

shcore::Value Global_dba::get_cluster(const shcore::Argument_list &args) {
  args.ensure_count(0, 2, get_function_name("getCluster").c_str());
  auto dba = std::dynamic_pointer_cast<mysqlsh::dba::Dba>(_target);

  // We get the cluster object 1st, so that low-level validations happen 1st
  Value raw_cluster = call_target("getCluster", args);
  std::shared_ptr<mysqlsh::dba::Cluster> cluster_obj(
      raw_cluster.as_object<mysqlsh::dba::Cluster>());

  // TODO(.) These checks should be moved to mod_dba.cc
  auto state =
      dba->check_preconditions(cluster_obj->get_group_session(), "getCluster");
  if (state.source_state == mysqlsh::dba::ManagedInstance::OnlineRO) {
    println("WARNING: You are connected to an instance in state '" +
            mysqlsh::dba::ManagedInstance::describe(
                static_cast<mysqlsh::dba::ManagedInstance::State>(
                    state.source_state)) +
            "'\n"
            "Write operations on the InnoDB cluster will not be allowed.\n");
  } else if (state.source_state != mysqlsh::dba::ManagedInstance::OnlineRW) {
    println("WARNING: You are connected to an instance in state '" +
            mysqlsh::dba::ManagedInstance::describe(
                static_cast<mysqlsh::dba::ManagedInstance::State>(
                    state.source_state)) +
            "'\n"
            "Write operations on the InnoDB cluster will not be allowed.\n"
            "Output from describe() and status() may be outdated.\n");
  }

  Interactive_dba_cluster *cluster =
      new Interactive_dba_cluster(this->_shell_core);
  cluster->set_target(
      std::dynamic_pointer_cast<Cpp_object_bridge>(cluster_obj));
  return shcore::Value::wrap<Interactive_dba_cluster>(cluster);
}

shcore::Value Global_dba::reboot_cluster_from_complete_outage(
    const shcore::Argument_list &args) {
  args.ensure_count(
      0, 2, get_function_name("rebootClusterFromCompleteOutage").c_str());

  shcore::Value ret_val;
  std::string cluster_name, password, user;
  shcore::Value::Map_type_ref options;
  shcore::Value::Array_type_ref confirmed_rescan_removes(
      new shcore::Value::Array_type());
  shcore::Value::Array_type_ref confirmed_rescan_rejoins(
      new shcore::Value::Array_type());
  Value::Array_type_ref remove_instances_ref, rejoin_instances_ref;
  shcore::Argument_list new_args;
  shcore::Argument_map opt_map;
  bool prompt_read_only = true;
  bool confirm_rescan_rejoins = true, confirm_rescan_removes = true;
  auto dba = std::dynamic_pointer_cast<mysqlsh::dba::Dba>(_target);

  check_preconditions(dba->get_active_shell_session(),
                      "rebootClusterFromCompleteOutage");

  try {
    bool default_cluster = false;

    if (args.size() == 0) {
      default_cluster = true;
    } else if (args.size() == 1) {
      cluster_name = args.string_at(0);
    } else {
      cluster_name = args.string_at(0);
      options = args.map_at(1);
    }

    if (options) {
      mysqlshdk::db::Connection_options connection_options;
      mysqlsh::set_user_from_map(&connection_options, options);
      mysqlsh::set_password_from_map(&connection_options, options);

      // Check if the password is specified on the options
      if (connection_options.has_user()) user = connection_options.get_user();

      if (connection_options.has_password())
        password = connection_options.get_password();

      opt_map = *options;

      // Case sensitive validation of the rest of the options, at this point
      // the user and password should have been already removed
      opt_map.ensure_keys({}, mysqlsh::dba::Dba::_reboot_cluster_opts,
                          "the options");

      if (opt_map.has_key("removeInstances")) confirm_rescan_removes = false;

      if (opt_map.has_key("rejoinInstances")) confirm_rescan_rejoins = false;

      if (opt_map.has_key("clearReadOnly")) {
        // This call is done only to validate the passed data
        opt_map.bool_at("clearReadOnly");
        prompt_read_only = false;
      }
    } else {
      options.reset(new shcore::Value::Map_type());
    }

    std::shared_ptr<mysqlsh::dba::MetadataStorage> metadata;
    std::shared_ptr<mysqlshdk::db::ISession> group_session;
    dba->connect_to_target_group({}, &metadata, &group_session, false);

    std::shared_ptr<mysqlsh::dba::Cluster> cluster;
    if (default_cluster) {
      println("Reconfiguring the default cluster from complete outage...");
      cluster = dba->get_cluster(nullptr, metadata, group_session);

      // BUG#28207565: DBA.REBOOTCLUSTERFROMCOMPLETEOUTAGE DOES NOT USE DEFAULT
      // CLUSTER.
      // The interactive layer will add a map of options to the function
      // call so the detection of default cluster won't happen because args
      // won't be zero:
      // if (args.size() == 0) {
      //    default_cluster = true;
      // To avoid that bug, we must obtain the default cluster name here
      // and use it for the function call.
      cluster_name = cluster->get_name();
    } else {
      println("Reconfiguring the cluster '" + cluster_name +
              "' from complete outage...");
      cluster = dba->get_cluster(cluster_name.c_str(), metadata, group_session);
    }

    // Verify the status of the instances
    validate_instances_status_reboot_cluster(
        cluster, cluster->get_group_session(), options);

    // Get the all the instances and their status
    std::vector<std::pair<std::string, std::string>> instances_status =
        get_replicaset_instances_status(cluster, options);

    // Validate the rejoinInstances list if provided
    if (!confirm_rescan_rejoins) {
      rejoin_instances_ref = opt_map.array_at("rejoinInstances");

      for (auto value : *rejoin_instances_ref.get()) {
        std::string instance = value.get_string();

        shcore::Argument_list args;
        args.push_back(shcore::Value(instance));

        try {
          auto instance_def = mysqlsh::get_connection_options(
              args, mysqlsh::PasswordFormat::NONE);
        } catch (std::exception &e) {
          std::string error(e.what());
          throw shcore::Exception::argument_error(
              "Invalid value '" + instance +
              "' for 'rejoinInstances': " + error);
        }

        auto it = std::find_if(
            instances_status.begin(), instances_status.end(),
            [&instance](const std::pair<std::string, std::string> &p) {
              return p.first == instance;
            });

        if (it == instances_status.end()) {
          throw Exception::runtime_error("The instance '" + instance +
                                         "' does not "
                                         "belong to the cluster or is the seed "
                                         "instance.");
        }
      }
    }

    // Validate the removeInstances list if provided
    if (!confirm_rescan_removes) {
      remove_instances_ref = opt_map.array_at("removeInstances");

      for (auto value : *remove_instances_ref.get()) {
        std::string instance = value.get_string();

        shcore::Argument_list args;
        args.push_back(shcore::Value(instance));

        try {
          auto instance_def = mysqlsh::get_connection_options(
              args, mysqlsh::PasswordFormat::NONE);
        } catch (std::exception &e) {
          std::string error(e.what());
          throw shcore::Exception::argument_error(
              "Invalid value '" + instance +
              "' for 'removeInstances': " + error);
        }

        auto it = std::find_if(
            instances_status.begin(), instances_status.end(),
            [&instance](const std::pair<std::string, std::string> &p) {
              return p.first == instance;
            });

        if (it == instances_status.end()) {
          throw Exception::runtime_error("The instance '" + instance +
                                         "' does not "
                                         "belong to the cluster or is the seed "
                                         "instance.");
        }
      }
    }

    // Only after a validation of the list (if provided)
    // we can move forward to the interaction
    if (confirm_rescan_rejoins) {
      for (const auto &value : instances_status) {
        std::string instance_address = value.first;
        std::string instance_status = value.second;

        // if the status is not empty it means the connection failed
        // so we skip this instance
        if (!instance_status.empty()) {
          std::string msg = "The instance '" + instance_address +
                            "' is not "
                            "reachable: '" +
                            instance_status +
                            "'. Skipping rejoin to the Cluster.";
          log_warning("%s", msg.c_str());
          continue;
        }
        // If the instance is part of the remove_instances list we skip this
        // instance
        if (!confirm_rescan_removes) {
          auto it = std::find_if(remove_instances_ref.get()->begin(),
                                 remove_instances_ref.get()->end(),
                                 [&instance_address](shcore::Value val) {
                                   return val.get_string() == instance_address;
                                 });
          if (it != remove_instances_ref.get()->end()) continue;
        }

        println();
        println("The instance '" + instance_address +
                "' was part of the cluster configuration.");

        if (confirm("Would you like to rejoin it to the cluster?",
                    mysqlsh::Prompt_answer::NO) ==
            mysqlsh::Prompt_answer::YES) {
          confirmed_rescan_rejoins->push_back(shcore::Value(instance_address));
        }
      }
    }

    if (confirm_rescan_removes) {
      for (const auto &value : instances_status) {
        std::string instance_address = value.first;
        std::string instance_status = value.second;

        // if the status is empty it means the connection succeeded
        // so we skip this instance
        if (instance_status.empty()) continue;

        // If the instance is part of the rejoin_instances list we skip this
        // instance
        if (!confirm_rescan_rejoins) {
          auto it = std::find_if(rejoin_instances_ref.get()->begin(),
                                 rejoin_instances_ref.get()->end(),
                                 [&instance_address](shcore::Value val) {
                                   return val.get_string() == instance_address;
                                 });
          if (it != rejoin_instances_ref.get()->end()) continue;
        }
        println();
        println("Could not open a connection to '" + instance_address + "': '" +
                instance_status + "'");

        if (confirm("Would you like to remove it from the cluster's metadata?",
                    mysqlsh::Prompt_answer::NO) == mysqlsh::Prompt_answer::YES)
          confirmed_rescan_removes->push_back(shcore::Value(instance_address));
      }
    }

    println();

    // Verify the status of super_read_only and ask the user if wants
    // to disable it
    // NOTE: this is left for last to avoid setting super_read_only to true
    // and right before some execution failure of the command leaving the
    // instance in an incorrect state
    if (prompt_read_only) {
      if (!prompt_super_read_only(cluster->get_group_session(), options))
        return shcore::Value();
    }
  }
  CATCH_AND_TRANSLATE_CLUSTER_EXCEPTION(
      get_function_name("rebootClusterFromCompleteOutage"));

  if (!confirmed_rescan_rejoins->empty() ||
      !confirmed_rescan_removes->empty() ||
      (prompt_read_only && options->has_key("clearReadOnly"))) {
    shcore::Argument_list new_args;

    if (!confirmed_rescan_rejoins->empty())
      (*options)["rejoinInstances"] = shcore::Value(confirmed_rescan_rejoins);

    if (!confirmed_rescan_removes->empty())
      (*options)["removeInstances"] = shcore::Value(confirmed_rescan_removes);

    // Check if the user provided any option
    if (!confirm_rescan_removes)
      (*options)["removeInstances"] =
          shcore::Value(opt_map.array_at("removeInstances"));

    if (!confirm_rescan_rejoins)
      (*options)["rejoinInstances"] =
          shcore::Value(opt_map.array_at("rejoinInstances"));

    if (!user.empty()) (*options)["user"] = shcore::Value(user);

    if (!password.empty()) (*options)["password"] = shcore::Value(password);

    new_args.push_back(shcore::Value(cluster_name));
    new_args.push_back(shcore::Value(options));
    ret_val = call_target("rebootClusterFromCompleteOutage", new_args);
  } else {
    ret_val = call_target("rebootClusterFromCompleteOutage", args);
  }

  println();
  println("The cluster was successfully rebooted.");
  println();

  Interactive_dba_cluster *cluster =
      new Interactive_dba_cluster(this->_shell_core);
  cluster->set_target(
      std::dynamic_pointer_cast<Cpp_object_bridge>(ret_val.as_object()));
  return shcore::Value::wrap<Interactive_dba_cluster>(cluster);
}

bool Global_dba::resolve_cnf_path(
    const mysqlshdk::db::Connection_options &connection_args,
    const shcore::Value::Map_type_ref &extra_options) {
  // Path is not given, let's try to autodetect itg
  int port = 0;
  std::string datadir;

  auto session = mysqlsh::dba::Dba::get_session(connection_args);

  enum class OperatingSystem { DEBIAN, REDHAT, LINUX, WINDOWS, MACOS, SOLARIS };

  // If the instance is a sandbox, we can obtain directly the path from
  // the datadir
  mysqlsh::dba::get_port_and_datadir(session, port, datadir);
  std::string path_separator = datadir.substr(datadir.size() - 1);
  auto path_elements = shcore::split_string(datadir, path_separator);

  // Removes the empty element at the end
  path_elements.pop_back();

  std::string tmpPath;
  std::string cnfPath;

  // Sandbox deployment structure would be:
  // - <root_path>/<port>/sandboxdata
  // - <root_path>/<port>/my.cnf
  // So we validate such structure to determine it is a sandbox
  if (path_elements[path_elements.size() - 2] == std::to_string(port)) {
    path_elements[path_elements.size() - 1] = "my.cnf";

    tmpPath = shcore::str_join(path_elements, path_separator);
    if (shcore::file_exists(tmpPath)) {
      println();
      println("Detected as sandbox instance.");
      println();
      println("Validating MySQL configuration file at: " + tmpPath);
      cnfPath = tmpPath;
    } else {
      log_warning(
          "Sandbox configuration file not found at expected location: %s",
          tmpPath.c_str());
    }
  } else {
    // It's not a sandbox, so let's try to locate the .cnf file path
    // based on the OS and the default my.cnf path as configured
    // by our official MySQL packages

    // Detect the OS
    OperatingSystem os;

#ifdef WIN32
    os = OperatingSystem::WINDOWS;
#elif __APPLE__
    os = OperatingSystem::MACOS;
#elif __sun
    os = OperatingSystem::SOLARIS;
#else
    os = OperatingSystem::LINUX;

    // Detect the distribution
    std::string distro_buffer, proc_version = "/proc/version";

    if (shcore::file_exists(proc_version)) {
      // Read the proc_version file
      std::ifstream s(proc_version.c_str());

      if (!s.fail())
        std::getline(s, distro_buffer);
      else
        log_warning("Failed to read file: %s", proc_version.c_str());

      // Transform all to lowercase
      std::transform(distro_buffer.begin(), distro_buffer.end(),
                     distro_buffer.begin(), ::tolower);

      const std::vector<std::string> distros = {"ubuntu", "debian", "red hat"};

      for (const auto &value : distros) {
        if (distro_buffer.find(value) != std::string::npos) {
          if (value == "ubuntu" || value == "debian") {
            os = OperatingSystem::DEBIAN;
            break;
          } else if (value == "red hat") {
            os = OperatingSystem::REDHAT;
            break;
          } else {
            continue;
          }
        }
      }
    } else {
      log_warning(
          "Failed to detect the Linux distribution. '%s' "
          "does not exist.",
          proc_version.c_str());
    }
#endif

    println();
    println("Detecting the configuration file...");

    std::vector<std::string> default_paths;

    switch (os) {
      case OperatingSystem::DEBIAN:
        default_paths.push_back("/etc/mysql/mysql.conf.d/mysqld.cnf");
        break;
      case OperatingSystem::REDHAT:
      case OperatingSystem::SOLARIS:
        default_paths.push_back("/etc/my.cnf");
        break;
      case OperatingSystem::LINUX:
        default_paths.push_back("/etc/my.cnf");
        default_paths.push_back("/etc/mysql/my.cnf");
        break;
      case OperatingSystem::WINDOWS:
        default_paths.push_back(
            "C:\\ProgramData\\MySQL\\MySQL Server 5.7\\my.ini");
        default_paths.push_back(
            "C:\\ProgramData\\MySQL\\MySQL Server 8.0\\my.ini");
        break;
      case OperatingSystem::MACOS:
        default_paths.push_back("/etc/my.cnf");
        default_paths.push_back("/etc/mysql/my.cnf");
        default_paths.push_back("/usr/local/mysql/etc/my.cnf");
        break;
      default:
        // The non-handled OS case will keep default_paths and cnfPath empty
        break;
    }

    // Iterate the default_paths to check if the files exist and if so,
    // set cnfPath
    for (const auto &value : default_paths) {
      if (shcore::file_exists(value)) {
        // Prompt the user to validate if shall use it or not
        println("Found configuration file at standard location: " + value);

        if (confirm("Do you want to modify this file?") ==
            mysqlsh::Prompt_answer::YES) {
          cnfPath = value;
          break;
        }
      }
    }

    // macOS does not create a default file so there might not be any
    // configuration file on the default locations. We must create the file
    if (cnfPath.empty() && (os == OperatingSystem::MACOS)) {
      println("Default file not found at the standard locations.");

      for (const auto &value : default_paths) {
        if (confirm("Do you want to create a file at: '" + value + "'?") ==
            mysqlsh::Prompt_answer::YES) {
          std::ofstream cnf(value.c_str());

          if (!cnf.fail()) {
            cnf << "[mysqld]\n";
            cnf.close();
            cnfPath = value;
            break;
          } else {
            println("Failed to create file at: '" + value + "'");
          }
        }
      }
    }
  }

  if (cnfPath.empty()) {
    println("Default file not found at the standard locations.");

    bool done = false;
    while (!done && prompt("Please specify the path to the MySQL "
                           "configuration file: ",
                           tmpPath)) {
      if (tmpPath.empty()) {
        done = true;
      } else {
        if (shcore::file_exists(tmpPath)) {
          cnfPath = tmpPath;
          done = true;
        } else {
          println("The given path to the MySQL configuration file is invalid.");
          println();
        }
      }
    }
  }

  // if the path was finally resolved
  if (!cnfPath.empty()) (*extra_options)["mycnfPath"] = shcore::Value(cnfPath);

  return !cnfPath.empty();
}

std::string Global_dba::prompt_confirmed_password() {
  std::string password1;
  std::string password2;
  auto console = mysqlsh::current_console();

  for (;;) {
    if (shcore::Prompt_result::Ok ==
        console->prompt_password("Password for new account: ", &password1)) {
      if (shcore::Prompt_result::Ok ==
          console->prompt_password("Confirm password: ", &password2)) {
        if (password1 != password2) {
          println("Passwords don't match, please try again.");
          continue;
        }
      }
    }
    break;
  }
  return password1;
}

int Global_dba::prompt_menu(const std::vector<std::string> &options,
                            int defopt) {
  int i = 0;
  for (const auto &opt : options) {
    println(std::to_string(++i) + ") " + opt);
  }
  for (;;) {
    std::string result;
    if (defopt > 0) {
      if (!prompt("Please select an option [" + std::to_string(defopt) + "]: ",
                  result))
        return 0;
    } else {
      if (!prompt("Please select an option: ", result)) return 0;
    }
    // Note that menu options start at 1, not 0 since that's what users will
    // input
    if (result.empty() && defopt > 0) return defopt;
    std::stringstream ss(result);
    ss >> i;
    if (i <= 0 || i > static_cast<int>(options.size())) continue;
    return i;
  }
}

bool Global_dba::ensure_admin_account_usable(
    std::shared_ptr<mysqlshdk::db::ISession> session, const std::string &user,
    const std::string &host, std::string *out_create_account) {
  int n_wildcard_accounts, n_non_wildcard_accounts;
  std::vector<std::string> hosts;
  std::tie(n_wildcard_accounts, n_non_wildcard_accounts) =
      mysqlsh::dba::find_cluster_admin_accounts(session, user, &hosts);

  // there are multiple accounts defined with the same username
  // we assume that the user knows what they're doing and skip the rest
  if (n_wildcard_accounts + n_non_wildcard_accounts > 1) {
    log_info("%i accounts named %s found. Skipping remaining checks",
             n_wildcard_accounts + n_non_wildcard_accounts, user.c_str());
    return true;
  } else {
    auto hiter = std::find_if(
        hosts.begin(), hosts.end(),
        [](const std::string &a) { return a.find('%') != std::string::npos; });
    // there is only one account with a wildcard, so validate it
    if (n_wildcard_accounts == 1 && hiter != hosts.end()) {
      std::string whost = *hiter;
      std::string validation_error;

      if (mysqlsh::dba::validate_cluster_admin_user_privileges(
              session, user, whost, &validation_error)) {
        log_info("Account %s@%s has required privileges for cluster management",
                 user.c_str(), whost.c_str());
        // account accepted
        return true;
      } else {
        log_info(
            "Account %s@%s is missing privileges needed for cluster management"
            ": %s",
            user.c_str(), whost.c_str(), validation_error.c_str());
        std::string msg;
        msg = "Account " + user + "@" + whost + " is missing privileges\n";
        msg += "that may be needed for managing an InnoDB cluster.\n";
        msg += validation_error + "\n";
        println(msg.c_str());
      }
    } else {
      log_info("Account %s may not be accessible from all hosts of the cluster",
               user.c_str());
      std::string msg;
      msg = "MySQL user '" + user +
            "' cannot be verified to have access to other hosts in the "
            "network.\n";
      println(msg);
      int result;
      result = prompt_menu({"Create " + user + "@% with necessary grants",
                            "Create account with different name",
                            "Continue without creating account", "Cancel"},
                           1);
      switch (result) {
        case 1:
          if (out_create_account) *out_create_account = user + "@'%'";
          return true;
        case 2:
          break;
        case 3:
          if (out_create_account) *out_create_account = "";
          return true;
        case 4:
        default:
          println("Cancelling...");
          return false;
      }
    }

    std::string msg =
        "Please provide an account name (e.g: icroot@%) "
        "to have it created with the necessary\n";
    msg += "privileges or leave empty and press Enter to cancel.";
    println(msg.c_str());

    std::string create_user;
    bool cancelled = false;
    for (;;) {
      if (prompt("Account Name: ", create_user) && !create_user.empty()) {
        try {
          // normalize the account name
          if (std::count(create_user.begin(), create_user.end(), '@') <= 1 &&
              create_user.find(' ') == std::string::npos &&
              create_user.find('\'') == std::string::npos &&
              create_user.find('"') == std::string::npos &&
              create_user.find('\\') == std::string::npos) {
            auto p = create_user.find('@');
            if (p == std::string::npos)
              create_user = make_account(create_user, "%");
            else
              create_user = make_account(create_user.substr(0, p),
                                         create_user.substr(p + 1));
          }
          // validate
          shcore::split_account(create_user, nullptr, nullptr, false);

          if (out_create_account) *out_create_account = create_user;
          break;
        } catch (std::runtime_error &) {
          println("`" + create_user +
                  "` is not a valid account name. Must be user[@host] or "
                  "'user'[@'host']");
        }
      } else {
        cancelled = true;
        break;
      }
    }
    if (cancelled) {
      println("Cancelling...");
      return false;
    }
    return true;
  }
}

bool Global_dba::prompt_super_read_only(
    std::shared_ptr<mysqlshdk::db::ISession> session,
    const shcore::Value::Map_type_ref &options) {
  auto options_session = session->get_connection_options();
  auto active_session_address =
      options_session.as_uri(mysqlshdk::db::uri::formats::only_transport());

  // Get the status of super_read_only in order to verify if we need to
  // prompt the user to disable it
  int super_read_only = 0;
  mysqlsh::dba::get_server_variable(session, "super_read_only", super_read_only,
                                    false);

  if (super_read_only) {
    println("The MySQL instance at '" + active_session_address +
            "' "
            "currently has the super_read_only \nsystem variable set to "
            "protect it from inadvertent updates from applications. \n"
            "You must first unset it to be able to perform any changes "
            "to this instance. \n"
            "For more information see: https://dev.mysql.com/doc/refman/"
            "en/server-system-variables.html#sysvar_super_read_only.");
    println();

    // Get the list of open session to the instance
    std::vector<std::pair<std::string, int>> open_sessions;
    open_sessions = mysqlsh::dba::get_open_sessions(session);

    if (!open_sessions.empty()) {
      println("Note: there are open sessions to '" + active_session_address +
              "'.\n"
              "You may want to kill these sessions to prevent them from "
              "performing unexpected updates: \n");

      for (auto &value : open_sessions) {
        std::string account = value.first;
        int open_sessions = value.second;

        println("" + std::to_string(open_sessions) +
                " open session(s) of "
                "'" +
                account + "'. \n");
      }
    }

    if (confirm("Do you want to disable super_read_only and continue?",
                mysqlsh::Prompt_answer::NO) == mysqlsh::Prompt_answer::NO) {
      println();
      println("Cancelled");
      return false;
    } else {
      (*options)["clearReadOnly"] = shcore::Value(true);
      println();
      return true;
    }
  }
  return true;
}
