/*
 *
 * Copyright (c) 2006 The Apache Software Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "qpid/Options.h"
#include "qpid/Exception.h"

#include <boost/bind.hpp>

#include <fstream>
#include <algorithm>
#include <iostream>

namespace qpid {

using namespace std;


namespace {

struct EnvOptMapper {
    static bool matchChar(char env, char opt) {
        return (env==toupper(opt)) || (strchr("-.", opt) && env=='_');
    }

    static bool matchStr(const string& env, boost::shared_ptr<po::option_description> desc) {
        return desc->long_name().size() == env.size() &&
            std::equal(env.begin(), env.end(), desc->long_name().begin(), &matchChar);
    }

    static bool matchCase(const string& env, boost::shared_ptr<po::option_description> desc) {
        return env == desc->long_name();
    }

    EnvOptMapper(const Options& o) : opts(o) {}

    string operator()(const string& envVar) {
        static const std::string prefix("QPID_");
        if (envVar.substr(0, prefix.size()) == prefix) {
            string env = envVar.substr(prefix.size());
            typedef const std::vector< boost::shared_ptr<po::option_description> > OptDescs;
            OptDescs::const_iterator i =
                find_if(opts.options().begin(), opts.options().end(), boost::bind(matchStr, env, _1));
            if (i != opts.options().end())
                return (*i)->long_name();
        }
        return string();
    }


    bool
    isComment ( string const & str )
    {
      size_t i = str.find_first_not_of ( " \t" );

      if ( i == string::npos )
        return true;

      return str[i] == '#';
    }


    void badArg ( string& line ) {
        ostringstream msg;
        msg << "Bad argument: |" << line << "|\n";
        throw Exception(msg.str());
    }


    string configFileLine (string& line, bool allowUnknowns=true) {

        if ( isComment ( line ) ) {
            return string();
        }

        size_t pos = line.find ('=');
        if (pos == string::npos) {
            if ( allowUnknowns ) {
                return string();
            }
            else {
                badArg ( line );
            }
        }
        string key = line.substr (0, pos);
#if (BOOST_VERSION >= 103300)
        typedef const std::vector< boost::shared_ptr<po::option_description> > OptDescs;
        OptDescs::const_iterator i =
            find_if(opts.options().begin(), opts.options().end(), boost::bind(matchCase, key, _1));
        if (i != opts.options().end())
            return string (line) + "\n";
        else {
            if ( allowUnknowns ) {
                return string();
            }
            else {
                badArg ( line );
            }
        }
#else
        // Use 'count' to see if this option exists.  Using 'find' will 
        // SEGV or hang if the option has not been defined yet.
        if ( opts.count(key.c_str()) > 0 )
          return string ( line ) + "\n";
        else {
            if ( allowUnknowns ) {
                return string ( );
            }
            else {
                badArg ( line );
            }
        }
#endif
      // Control will not arrive here, but the compiler things it could.  
      // Calls to badArg(), that I used above, throw.
      return string();  
    }

    const Options& opts;
};

}
std::string prettyArg(const std::string& name, const std::string& value) {
    return value.empty() ? name+" " : name+" ("+value+") ";
}

Options::Options(const string& name) :
  po::options_description(name)
{
}





void Options::parse(int argc, char const* const* argv, const std::string& configFile, bool allowUnknown)
{
    string defaultConfigFile = configFile; // May be changed by env/cmdline
    string parsing;
    try {
        po::variables_map vm;
        parsing="command line options";
        if (argc > 0 && argv != 0) {
            if (allowUnknown) {
                // This hideous workaround is required because boost 1.33 has a bug
                // that causes 'allow_unregistered' to not work.
                po::command_line_parser clp = po::command_line_parser(argc, const_cast<char**>(argv)).
                    options(*this).allow_unregistered();
                po::parsed_options opts     = clp.run();
                po::parsed_options filtopts = clp.run();
                filtopts.options.clear ();
                for (std::vector< po::basic_option<char> >::iterator i = opts.options.begin();
                     i != opts.options.end(); i++)
                    if (!i->unregistered)
                        filtopts.options.push_back (*i);
                po::store(filtopts, vm);

            }
            else
                po::store(po::parse_command_line(argc, const_cast<char**>(argv), *this), vm);
        }
        parsing="environment variables";
        po::store(po::parse_environment(*this, EnvOptMapper(*this)), vm);
        po::notify(vm); // configFile may be updated from arg/env options.
        if (!configFile.empty()) {
            parsing="configuration file "+configFile;
            ifstream conf(configFile.c_str());
            conf.peek();
            if (conf.good()) {
                // Remove this hack when we get a stable version of boost that
                // can allow unregistered options in config files.
                EnvOptMapper mapper(*this);
                stringstream filtered;

                while (!conf.eof()) {
                    string line;
                    getline (conf, line);
                    filtered << mapper.configFileLine (line, allowUnknown);
                }

                po::store(po::parse_config_file(filtered, *this), vm);
                // End of hack
            }
            else {
                // No error if default configfile is missing/unreadable
                // but complain for non-default config file.
                if (configFile != defaultConfigFile)
                    throw Exception("cannot read configuration file "
                                    +configFile);
            }
        }
        po::notify(vm);
    }
    catch (const std::exception& e) {
        ostringstream msg;
        msg << "Error in " << parsing << ": " << e.what() << endl;
#if (BOOST_VERSION >= 103300)
        if (find_nothrow("help", false))
            msg << "Use --help to see valid options" << endl;
#endif
        throw Exception(msg.str());
    }
}

CommonOptions::CommonOptions(const string& name, const string& configfile, const string& clientfile)
    : Options(name), config(configfile), clientConfig(clientfile)
{
    addOptions()
        ("help,h", optValue(help), "Displays the help message")
        ("version,v", optValue(version), "Displays version information")
        ("config", optValue(config, "FILE"), "Reads configuration from FILE")
        ("client-config", optValue(clientConfig, "FILE"), "Reads client configuration from FILE (for cluster interconnect)");
}



bool Options::findArg(int argc, char const* const* argv, const std::string& theArg)
{
    const string parsing("command line options");
    bool result(false);
    try {
        if (argc > 0 && argv != 0) {
            po::command_line_parser clp = po::command_line_parser(argc, const_cast<char**>(argv)).
            options(*this).allow_unregistered();
            po::parsed_options opts     = clp.run();

            for (std::vector< po::basic_option<char> >::iterator
                i = opts.options.begin(); i != opts.options.end(); i++) {
                if (theArg.compare(i->string_key) == 0) {
                    result = true;
                    break;
                }
            }
        }
        return result;
    }
    catch (const std::exception& e) {
        ostringstream msg;
        msg << "Error in " << parsing << ": " << e.what() << endl;
        throw Exception(msg.str());
    }
}

} // namespace qpid

