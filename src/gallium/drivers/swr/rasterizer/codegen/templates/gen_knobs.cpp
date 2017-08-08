/******************************************************************************
*
* Copyright 2015-2017
* Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http ://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
% if gen_header:
* @file ${filename}.h
% else:
* @file ${filename}.cpp
% endif 
*
* @brief Dynamic Knobs for Core.
*
* ======================= AUTO GENERATED: DO NOT EDIT !!! ====================
*
* Generation Command Line:
*  ${'\n*    '.join(cmdline)}
*
******************************************************************************/
<% calc_max_knob_len(knobs) %>
%if gen_header:
#pragma once
#include <string>

struct KnobBase
{
private:
    // Update the input string.
    static void autoExpandEnvironmentVariables(std::string &text);

protected:
    // Leave input alone and return new string.
    static std::string expandEnvironmentVariables(std::string const &input)
    {
        std::string text = input;
        autoExpandEnvironmentVariables(text);
        return text;
    }

    template <typename T>
    static T expandEnvironmentVariables(T const &input)
    {
        return input;
    }
};

template <typename T>
struct Knob : KnobBase
{
public:
    const   T&  Value() const               { return m_Value; }
    const   T&  Value(T const &newValue)
    {
        m_Value = expandEnvironmentVariables(newValue);
        return Value();
    }

protected:
    Knob(T const &defaultValue) :
        m_Value(expandEnvironmentVariables(defaultValue))
    {
    }

private:
    T m_Value;
};

#define DEFINE_KNOB(_name, _type, _default)                     \\

    struct Knob_##_name : Knob<_type>                           \\

    {                                                           \\

        Knob_##_name() : Knob<_type>(_default) { }              \\

        static const char* Name() { return "KNOB_" #_name; }    \\

    } _name;

#define GET_KNOB(_name)             g_GlobalKnobs._name.Value()
#define SET_KNOB(_name, _newValue)  g_GlobalKnobs._name.Value(_newValue)

struct GlobalKnobs
{
    % for knob in knobs:
    //-----------------------------------------------------------
    // KNOB_${knob[0]}
    //
    % for line in knob[1]['desc']:
    // ${line}
    % endfor
    % if knob[1].get('choices'):
    <%
    choices = knob[1].get('choices')
    _max_len = calc_max_name_len(choices) %>//
    % for i in range(len(choices)):
    //     ${choices[i]['name']}${space_name(choices[i]['name'], _max_len)} = ${format(choices[i]['value'], '#010x')}
    % endfor
    % endif
    //
    % if knob[1]['type'] == 'std::string':
    DEFINE_KNOB(${knob[0]}, ${knob[1]['type']}, "${repr(knob[1]['default'])[1:-1]}");
    % else:
    DEFINE_KNOB(${knob[0]}, ${knob[1]['type']}, ${knob[1]['default']});
    % endif

    % endfor
    GlobalKnobs();
    std::string ToString(const char* optPerLinePrefix="");
};
extern GlobalKnobs g_GlobalKnobs;

#undef DEFINE_KNOB

% for knob in knobs:
#define KNOB_${knob[0]}${space_knob(knob[0])} GET_KNOB(${knob[0]})
% endfor

% else:
% for inc in includes:
#include <${inc}>
% endfor
#include <regex>
#include <core/utils.h>

//========================================================
// Implementation
//========================================================
void KnobBase::autoExpandEnvironmentVariables(std::string &text)
{
#if (__GNUC__) && (GCC_VERSION < 409000)
    // <regex> isn't implemented prior to gcc-4.9.0
    // unix style variable replacement
    size_t start;
    while ((start = text.find("${'${'}")) != std::string::npos) {
        size_t end = text.find("}");
        if (end == std::string::npos)
            break;
        const std::string var = GetEnv(text.substr(start + 2, end - start - 2));
        text.replace(start, end - start + 1, var);
    }
    // win32 style variable replacement
    while ((start = text.find("%")) != std::string::npos) {
        size_t end = text.find("%", start + 1);
        if (end == std::string::npos)
            break;
        const std::string var = GetEnv(text.substr(start + 1, end - start - 1));
        text.replace(start, end - start + 1, var);
    }
#else
    {
        // unix style variable replacement
        static std::regex env("\\$\\{([^}]+)\\}");
        std::smatch match;
        while (std::regex_search(text, match, env))
        {
            const std::string var = GetEnv(match[1].str());
            // certain combinations of gcc/libstd++ have problems with this
            // text.replace(match[0].first, match[0].second, var);
            text.replace(match.prefix().length(), match[0].length(), var);
        }
    }
    {
        // win32 style variable replacement
        static std::regex env("\\%([^}]+)\\%");
        std::smatch match;
        while (std::regex_search(text, match, env))
        {
            const std::string var = GetEnv(match[1].str());
            // certain combinations of gcc/libstd++ have problems with this
            // text.replace(match[0].first, match[0].second, var);
            text.replace(match.prefix().length(), match[0].length(), var);
        }
    }
#endif
}


//========================================================
// Static Data Members
//========================================================
GlobalKnobs g_GlobalKnobs;

//========================================================
// Knob Initialization
//========================================================
GlobalKnobs::GlobalKnobs()
{
    % for knob in knobs:
    InitKnob(${knob[0]});
    % endfor
}

//========================================================
// Knob Display (Convert to String)
//========================================================
std::string GlobalKnobs::ToString(const char* optPerLinePrefix)
{
    std::basic_stringstream<char> str;
    str << std::showbase << std::setprecision(1) << std::fixed;

    if (optPerLinePrefix == nullptr) { optPerLinePrefix = ""; }

    % for knob in knobs:
    str << optPerLinePrefix << "KNOB_${knob[0]}:${space_knob(knob[0])}";
    % if knob[1]['type'] == 'bool':
    str << (KNOB_${knob[0]} ? "+\n" : "-\n");
    % elif knob[1]['type'] != 'float' and knob[1]['type'] != 'std::string':
    str << std::hex << std::setw(11) << std::left << KNOB_${knob[0]};
    str << std::dec << KNOB_${knob[0]} << "\n";
    % else:
    str << KNOB_${knob[0]} << "\n";
    % endif
    % endfor
    str << std::ends;

    return str.str();
}

% endif

<%!
    # Globally available python 
    max_len = 0
    def calc_max_knob_len(knobs):
        global max_len
        max_len = 0
        for knob in knobs:
            if len(knob[0]) > max_len: max_len = len(knob[0])
        max_len += len('KNOB_ ')
        if max_len % 4: max_len += 4 - (max_len % 4)

    def space_knob(knob):
        knob_len = len('KNOB_' + knob)
        return ' '*(max_len - knob_len)

    def calc_max_name_len(choices_array):
        _max_len = 0
        for choice in choices_array:
            if len(choice['name']) > _max_len: _max_len = len(choice['name'])

        if _max_len % 4: _max_len += 4 - (_max_len % 4)
        return _max_len

    def space_name(name, max_len):
        name_len = len(name)
        return ' '*(max_len - name_len)


%>
