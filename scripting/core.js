/*
 * Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
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
function ModuleHandler()
{
  this._module_cache = {}
  
  this._find_module = function(name)
  {
    var index = 0;
    var module_path = '';
    while(index < shell.js.module_paths.length && module_path == '')
    {
      var tmp_path = shell.js.module_paths[index];
      var last_char = tmp_path.slice(-1);
      if (last_char != '/' && last_char != '\\')
        tmp_path += '/';
      
      tmp_path += name + '.js';
      
      if (os.file_exists(tmp_path))
        module_path = tmp_path;
      else
        index++;
    }
    
    return module_path;
  }
  
  this._load_module = function(name)
  {
    var path = this._find_module(name);
    
    if (path)
    {
      var source = os.load_text_file(path);
      if (source)
      {
        var wrapped_source = '(function (exports){' + source + '});';
        var module_definition = __build_module(path, wrapped_source);
        
        var module = {}
        module_definition(module)
        
        this._module_cache[name] = module;
      }
      else
        throw ('Module ' + name + ' is empty.');
    }
  }
  
  this.get_module = function(name, reload)
  {
    if (typeof reload !== 'undefined' && reload)
      delete this._module_cache[name];
    
    // Loads the module if not done already.
    if (typeof this._module_cache[name] === 'undefined')
    {
      // Retrieves the module path and content
      this._load_module(name);
    }

    // If the module is on the cache then returns it
    if (typeof this._module_cache[name] !== 'undefined')
      return this._module_cache[name]
    else
      throw ('Module ' + name + ' was not found.');
  }
}

this.shell.js = {}

this.shell.js.module_paths = [];
  
this.shell.js.module_paths[0] = './modules/js';

var path = os.get_user_config_path();
if (path)
  this.shell.js.module_paths[1] = path + '/modules/js';

path = os.getenv('MYSQLX_JS_MODULE_PATH');
if (path)
  this.shell.js.module_paths[2] = path;

// An instance of the module handler and the require function will
// be appended to the received object, i.e. to the context globals.
this.shell.js.module_handler = new ModuleHandler();

this.require = function(module_name, reload)
{
  return this.shell.js.module_handler.get_module(module_name, reload);
}


