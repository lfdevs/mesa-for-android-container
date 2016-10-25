/******************************************************************************
*
* Copyright 2015-2016
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
* @file gen_knobs.cpp
*
* @brief Dynamic Knobs for Core.
*
* ======================= AUTO GENERATED: DO NOT EDIT !!! ====================
*
******************************************************************************/
#include <core/knobs_init.h>
#include <common/os.h>
#include <sstream>
#include <iomanip>

//========================================================
// Static Data Members
//========================================================
GlobalKnobs g_GlobalKnobs;

//========================================================
// Knob Initialization
//========================================================
GlobalKnobs::GlobalKnobs()
{
    InitKnob(ENABLE_ASSERT_DIALOGS);
    InitKnob(SINGLE_THREADED);
    InitKnob(DUMP_SHADER_IR);
    InitKnob(USE_GENERIC_STORETILE);
    InitKnob(FAST_CLEAR);
    InitKnob(MAX_NUMA_NODES);
    InitKnob(MAX_CORES_PER_NUMA_NODE);
    InitKnob(MAX_THREADS_PER_CORE);
    InitKnob(MAX_WORKER_THREADS);
    InitKnob(BUCKETS_START_FRAME);
    InitKnob(BUCKETS_END_FRAME);
    InitKnob(WORKER_SPIN_LOOP_COUNT);
    InitKnob(MAX_DRAWS_IN_FLIGHT);
    InitKnob(MAX_PRIMS_PER_DRAW);
    InitKnob(MAX_TESS_PRIMS_PER_DRAW);
    InitKnob(DEBUG_OUTPUT_DIR);
    InitKnob(TOSS_DRAW);
    InitKnob(TOSS_QUEUE_FE);
    InitKnob(TOSS_FETCH);
    InitKnob(TOSS_IA);
    InitKnob(TOSS_VS);
    InitKnob(TOSS_SETUP_TRIS);
    InitKnob(TOSS_BIN_TRIS);
    InitKnob(TOSS_RS);
}

//========================================================
// Knob Display (Convert to String)
//========================================================
std::string GlobalKnobs::ToString(const char* optPerLinePrefix)
{
    std::basic_stringstream<char> str;
    str << std::showbase << std::setprecision(1) << std::fixed;

    if (optPerLinePrefix == nullptr) { optPerLinePrefix = ""; }

    str << optPerLinePrefix << "KNOB_ENABLE_ASSERT_DIALOGS:      ";
    str << (KNOB_ENABLE_ASSERT_DIALOGS ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_SINGLE_THREADED:            ";
    str << (KNOB_SINGLE_THREADED ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_DUMP_SHADER_IR:             ";
    str << (KNOB_DUMP_SHADER_IR ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_USE_GENERIC_STORETILE:      ";
    str << (KNOB_USE_GENERIC_STORETILE ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_FAST_CLEAR:                 ";
    str << (KNOB_FAST_CLEAR ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_MAX_NUMA_NODES:             ";
    str << std::hex << std::setw(11) << std::left << KNOB_MAX_NUMA_NODES;
    str << std::dec << KNOB_MAX_NUMA_NODES << "\n";
    str << optPerLinePrefix << "KNOB_MAX_CORES_PER_NUMA_NODE:    ";
    str << std::hex << std::setw(11) << std::left << KNOB_MAX_CORES_PER_NUMA_NODE;
    str << std::dec << KNOB_MAX_CORES_PER_NUMA_NODE << "\n";
    str << optPerLinePrefix << "KNOB_MAX_THREADS_PER_CORE:       ";
    str << std::hex << std::setw(11) << std::left << KNOB_MAX_THREADS_PER_CORE;
    str << std::dec << KNOB_MAX_THREADS_PER_CORE << "\n";
    str << optPerLinePrefix << "KNOB_MAX_WORKER_THREADS:         ";
    str << std::hex << std::setw(11) << std::left << KNOB_MAX_WORKER_THREADS;
    str << std::dec << KNOB_MAX_WORKER_THREADS << "\n";
    str << optPerLinePrefix << "KNOB_BUCKETS_START_FRAME:        ";
    str << std::hex << std::setw(11) << std::left << KNOB_BUCKETS_START_FRAME;
    str << std::dec << KNOB_BUCKETS_START_FRAME << "\n";
    str << optPerLinePrefix << "KNOB_BUCKETS_END_FRAME:          ";
    str << std::hex << std::setw(11) << std::left << KNOB_BUCKETS_END_FRAME;
    str << std::dec << KNOB_BUCKETS_END_FRAME << "\n";
    str << optPerLinePrefix << "KNOB_WORKER_SPIN_LOOP_COUNT:     ";
    str << std::hex << std::setw(11) << std::left << KNOB_WORKER_SPIN_LOOP_COUNT;
    str << std::dec << KNOB_WORKER_SPIN_LOOP_COUNT << "\n";
    str << optPerLinePrefix << "KNOB_MAX_DRAWS_IN_FLIGHT:        ";
    str << std::hex << std::setw(11) << std::left << KNOB_MAX_DRAWS_IN_FLIGHT;
    str << std::dec << KNOB_MAX_DRAWS_IN_FLIGHT << "\n";
    str << optPerLinePrefix << "KNOB_MAX_PRIMS_PER_DRAW:         ";
    str << std::hex << std::setw(11) << std::left << KNOB_MAX_PRIMS_PER_DRAW;
    str << std::dec << KNOB_MAX_PRIMS_PER_DRAW << "\n";
    str << optPerLinePrefix << "KNOB_MAX_TESS_PRIMS_PER_DRAW:    ";
    str << std::hex << std::setw(11) << std::left << KNOB_MAX_TESS_PRIMS_PER_DRAW;
    str << std::dec << KNOB_MAX_TESS_PRIMS_PER_DRAW << "\n";
    str << optPerLinePrefix << "KNOB_DEBUG_OUTPUT_DIR:           ";
    str << KNOB_DEBUG_OUTPUT_DIR << "\n";
    str << optPerLinePrefix << "KNOB_TOSS_DRAW:                  ";
    str << (KNOB_TOSS_DRAW ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_TOSS_QUEUE_FE:              ";
    str << (KNOB_TOSS_QUEUE_FE ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_TOSS_FETCH:                 ";
    str << (KNOB_TOSS_FETCH ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_TOSS_IA:                    ";
    str << (KNOB_TOSS_IA ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_TOSS_VS:                    ";
    str << (KNOB_TOSS_VS ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_TOSS_SETUP_TRIS:            ";
    str << (KNOB_TOSS_SETUP_TRIS ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_TOSS_BIN_TRIS:              ";
    str << (KNOB_TOSS_BIN_TRIS ? "+\n" : "-\n");
    str << optPerLinePrefix << "KNOB_TOSS_RS:                    ";
    str << (KNOB_TOSS_RS ? "+\n" : "-\n");
    str << std::ends;

    return str.str();
}

