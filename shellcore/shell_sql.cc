/*
 * Copyright (c) 2014, 2015, Oracle and/or its affiliates. All rights reserved.
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

#include "shellcore/shell_sql.h"
#include "../modules/mod_session.h"
#include "../modules/mod_connection.h"
#include "../utils/utils_mysql_parsing.h"
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <fstream>

using namespace shcore;
using namespace boost::system;

Shell_sql::Shell_sql(Shell_core *owner)
: Shell_language(owner)
{
  _delimiter = ";";

  std::string cmd_help;
  SET_SHELL_COMMAND("warnings|\\W", "Show warnings after every statement.", "", Shell_sql::cmd_enable_auto_warnings);
  SET_SHELL_COMMAND("nowarnings|\\w", "Don't show warnings after every statement.", "", Shell_sql::cmd_disable_auto_warnings);
}

Value Shell_sql::handle_input(std::string &code, Interactive_input_state &state, bool interactive)
{
  state = Input_ok;
  Value session_wrapper = _owner->get_global("_S");
  MySQL_splitter splitter;

  _last_handled.clear();
  
  if (session_wrapper)
  {
    boost::shared_ptr<mysh::Session> session = session_wrapper.as_object<mysh::Session>();
    // Parses the input string to identify individual statements in it.
    // Will return a range for every statement that ends with the delimiter, if there
    // is additional code after the last delimiter, a range for it will be included too.
    std::vector<std::pair<size_t, size_t> > ranges;
    size_t statement_count = splitter.determineStatementRanges(code.c_str(), code.length(), _delimiter, ranges, "\n", _parsing_context_stack);

    // statement_count is > 0 if the splitter determined a statement was completed
    // ranges: contains the char ranges having statements.
    // special case: statement_count is 1 but there are no ranges: the delimiter was sent on the last call to the splitter
    //               and it determined the continued statement is now complete, but there's no additional data for it
    
    size_t index = 0;

    // Gets the total number of ranges
    size_t range_count = ranges.size();
    std::vector<std::string> statements;
    if (statement_count)
    {
      // If cache has data it is part of the found statement so it has to 
      // be flushed at this point into the statements list for execution
      if (!_sql_cache.empty())
      {
        if (statement_count > range_count)
          statements.push_back(_sql_cache);
        else
        {
          statements.push_back(_sql_cache.append("\n").append(code.substr(ranges[0].first, ranges[0].second)));
          index++;
        }

        _sql_cache.clear();
      }


      if (range_count)
      {
        // Now also adds the rest of the statements for execution
        for(; index < statement_count; index++)
          statements.push_back(code.substr(ranges[index].first, ranges[index].second));

        // If there's still data, itis a partial statement: gets cached
        if (index < range_count)
          _sql_cache = code.substr(ranges[index].first, ranges[index].second);
      }

      code = _sql_cache;

      // Executes every found statement
      for (index = 0; index < statements.size(); index++)
      {
        shcore::Argument_list query;
        query.push_back(Value(statements[index]));
        Value result_wrapper = session->sql(query);

        if (_last_handled.empty())
          _last_handled = statements[index];
        else
          _last_handled.append("\n").append(statements[index]);


        // TODO: the --force option should skip this validation
        //           to let it continue processing the rest of the statements
        if (result_wrapper.type == shcore::Null)
          return result_wrapper;

        if (result_wrapper)
        {
          boost::shared_ptr<mysh::Base_resultset> result = result_wrapper.as_object<mysh::Base_resultset>();

          if (result)
          {
            result->print(Argument_list());

            // Prints the warnings if required
            if (result->warning_count())
              print_warnings(session);
          }
        }
      }
    }
    else if (range_count)
    {
      if (_sql_cache.empty())
        _sql_cache = code.substr(ranges[0].first, ranges[0].second);
      else
        _sql_cache.append("\n").append(code.substr(ranges[0].first, ranges[0].second));
    }
    else // Multiline code, all is "processed"
      code = "";
    
    if (_parsing_context_stack.empty())
      state = Input_ok;
    else
      state = Input_continued;
  }

  // TODO: previous to file processing the caller was caching unprocessed code and sending it again on next
  //       call. On file processing an internal handling of this cache was required. 
  //       Clearing the code here prevents it being sent again.
  //       We need to decide if the caching logic we introduced on the caller is still required or not.
  code = "";

  return Value();
}

void Shell_sql::print_warnings(boost::shared_ptr<mysh::Session> session)
{
  Argument_list warnings_query;
  warnings_query.push_back(Value("show warnings"));

  Value::Map_type_ref options(new shcore::Value::Map_type);
  (*options)["key_by_index"] = Value::True();
  warnings_query.push_back(Value(options));

  Value result_wrapper = session->sql(warnings_query);

  if (result_wrapper)
  {
    boost::shared_ptr<mysh::Base_resultset> result = result_wrapper.as_object<mysh::Base_resultset>();

    if (result)
    {
      Value record;

      while ((record = result->next(Argument_list())))
      {
        boost::shared_ptr<Value::Map_type> row = record.as_map();

        
        unsigned long error = ((*row)["1"].as_int());

        std::string type = (*row)["0"].as_string();
        std::string msg = (*row)["2"].as_string();
        _owner->print((boost::format("%s (Code %ld): %s\n") % type % error % msg).str());
      }
    }
  }
}


std::string Shell_sql::prompt()
{
  if (!_parsing_context_stack.empty())
    return (boost::format("%5s> ") % _parsing_context_stack.top().c_str()).str();
  else
    return "mysql> ";
}

bool Shell_sql::print_help(const std::string& topic)
{
  bool ret_val = true;
  if (topic.empty())
    _shell_command_handler.print_commands("===== SQL Mode Commands =====");
  else
    ret_val = _shell_command_handler.print_command_help(topic);

  return ret_val;
}


//------------------ SQL COMMAND HANDLERS ------------------//

void Shell_sql::cmd_enable_auto_warnings(const std::vector<std::string>& params)
{
  // To be done once the global options are in place...
}

void Shell_sql::cmd_disable_auto_warnings(const std::vector<std::string>& params)
{
  // To be done once the global options are in place...
}
