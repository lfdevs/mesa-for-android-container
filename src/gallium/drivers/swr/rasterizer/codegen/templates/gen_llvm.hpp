/****************************************************************************
* Copyright (C) 2014-2017 Intel Corporation.   All Rights Reserved.
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
* @brief auto-generated file
*
* DO NOT EDIT
*
* Generation Command Line:
*   ${'\n*     '.join(cmdline)}
*
******************************************************************************/
#pragma once

namespace SwrJit
{
    using namespace llvm;

%for type in types:
    INLINE static StructType *Gen_${type['name']}(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        <%
            (max_type_len, max_name_len) = calc_max_len(type['members'])
        %>
        %for member in type['members']:
        /* ${member['name']} ${pad(len(member['name']), max_name_len)}*/ members.push_back( ${member['type']} );
        %endfor

        return StructType::get(ctx, members, false);
    }

    %for member in type['members']:
    static const uint32_t ${type['name']}_${member['name']} ${pad(len(member['name']), max_name_len)}= ${loop.index};
    %endfor

%endfor
} // ns SwrJit

<%! # Global function definitions
    def calc_max_len(fields):
        max_type_len = 0
        max_name_len = 0
        for f in fields:
            if len(f['type']) > max_type_len: max_type_len = len(f['type'])
            if len(f['name']) > max_name_len: max_name_len = len(f['name'])
        return (max_type_len, max_name_len)

    def pad(cur_len, max_len):
        pad_amt = max_len - cur_len
        return ' '*pad_amt
%>
