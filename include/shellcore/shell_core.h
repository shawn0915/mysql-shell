/*
 * Copyright (c) 2014, Oracle and/or its affiliates. All rights reserved.
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

#ifndef _SHELLCORE_H_
#define _SHELLCORE_H_

#include "shellcore/types.h"

namespace shcore {


class Registry
{
public:

private:
  Value _connections;
};


enum Interactive_input_state
{
  Input_ok,
  Input_continued
};


class Shell_core;

class Shell_language
{
public:
  Shell_language(Shell_core *owner) : _owner(owner) {}

  virtual Interactive_input_state handle_interactive_input_line(std::string &line) = 0;

private:
  Shell_core *_owner;
};


class Shell_core
{
public:
  enum Mode
  {
    Mode_SQL,
    Mode_JScript,
    Mode_Python
  };

  Shell_core(Mode default_mode);

  Mode interactive_mode() const { return _mode; }
  bool switch_mode(Mode mode);

public:
  Interactive_input_state handle_interactive_input_line(std::string &line);

private:
  Value get_global(const std::string &name);
  bool set_global(const std::string &name, Value value);


private:
  Registry _registry;
  std::map<Mode, Shell_language*> _langs;

  Mode _mode;
};

};

#endif // _SHELLCORE_H_

