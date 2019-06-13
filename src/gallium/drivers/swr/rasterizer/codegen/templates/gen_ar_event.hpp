/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * @file ${filename}
 *
 * @brief Definitions for events.  auto-generated file
 *
 * DO NOT EDIT
 *
 * Generation Command Line:
 *  ${'\n *    '.join(cmdline)}
 *
 ******************************************************************************/
// clang-format off
#pragma once

#include "common/os.h"
#include "core/state.h"

namespace ArchRast
{
% for name in protos['enum_names']:
    enum ${name}
    {<% names = protos['enums'][name]['names'] %>
        % for i in range(len(names)):
        ${names[i].lstrip()}
        % endfor
    };
% endfor

    // Forward decl
    class EventHandler;

    //////////////////////////////////////////////////////////////////////////
    /// Event - interface for handling events.
    //////////////////////////////////////////////////////////////////////////
    struct Event
    {
        Event() {}
        virtual ~Event() {}

        virtual void Accept(EventHandler* pHandler) const = 0;
    };
% for name in protos['event_names']:

    //////////////////////////////////////////////////////////////////////////
    /// ${name}Data
    //////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
    struct ${name}Data
    {<%
        fields = protos['events'][name]['fields'] %>
        // Fields
        % for i in range(len(fields)):
            % if fields[i]['size'] > 1:
        ${fields[i]['type']} ${fields[i]['name']}[${fields[i]['size']}];
            % else:
        ${fields[i]['type']} ${fields[i]['name']};
            % endif
        % endfor
    };
#pragma pack(pop)

    //////////////////////////////////////////////////////////////////////////
    /// ${name}
    //////////////////////////////////////////////////////////////////////////
    struct ${name} : Event
    {<%
        fields = protos['events'][name]['fields'] %>
        ${name}Data data;

        // Constructor
        ${name}(
        % for i in range(len(fields)):
            % if i < len(fields)-1:
                % if fields[i]['size'] > 1:
            ${fields[i]['type']}* ${fields[i]['name']},
            uint32_t ${fields[i]['name']}_size,
                % else:
            ${fields[i]['type']} ${fields[i]['name']},
                % endif
            % endif
            % if i == len(fields)-1:
                % if fields[i]['size'] > 1:
            ${fields[i]['type']}* ${fields[i]['name']},
            uint32_t ${fields[i]['name']}_size
                % else:
            ${fields[i]['type']} ${fields[i]['name']}
                % endif
            % endif
        % endfor
        )
        {
        % for i in range(len(fields)):
            % if fields[i]['size'] > 1:
                % if fields[i]['type'] == 'char':
            // Copy size of string (null-terminated) followed by string into entire buffer
            SWR_ASSERT(${fields[i]['name']}_size + 1 < ${fields[i]['size']} - sizeof(uint32_t), "String length must be less than size of char buffer - size(uint32_t)!");
            memcpy(data.${fields[i]['name']}, &${fields[i]['name']}_size, sizeof(uint32_t));
            strcpy_s(data.${fields[i]['name']} + sizeof(uint32_t), ${fields[i]['name']}_size + 1, ${fields[i]['name']});
                % else:
            memcpy(data.${fields[i]['name']}, ${fields[i]['name']}, ${fields[i]['name']}_size);
                % endif
            % else:
            data.${fields[i]['name']} = ${fields[i]['name']};
            % endif
        % endfor
        }

        virtual void Accept(EventHandler* pHandler) const;
    };
    % endfor
} // namespace ArchRast
// clang-format on
