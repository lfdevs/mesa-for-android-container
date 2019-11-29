/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "pan_context.h"
#include "pan_job.h"
#include "pan_allocate.h"
#include "util/bitset.h"

/*
 * Within a batch (panfrost_job), there are various types of Mali jobs:
 *
 *  - SET_VALUE: initializes tiler
 *  - VERTEX: runs a vertex shader
 *  - TILER: runs tiling and sets up a fragment shader
 *  - FRAGMENT: runs fragment shaders and writes out
 *  - COMPUTE: runs a compute shader
 *  - FUSED: vertex+tiler fused together, implicit intradependency (Bifrost)
 *  - GEOMETRY: runs a geometry shader (unimplemented)
 *  - CACHE_FLUSH: unseen in the wild, theoretically cache flush
 *
 * In between a full batch and a single Mali job is the "job chain", a series
 * of Mali jobs together forming a linked list. Within the job chain, each Mali
 * job can set (up to) two dependencies on other earlier jobs in the chain.
 * This dependency graph forms a scoreboard. The general idea of a scoreboard
 * applies: when there is a data dependency of job B on job A, job B sets one
 * of its dependency indices to job A, ensuring that job B won't start until
 * job A finishes.
 *
 * More specifically, here are a set of rules:
 *
 * - A set value job must appear if and only if there is at least one tiler job.
 *
 * - Vertex jobs and tiler jobs are independent.
 *
 * - A tiler job must have a dependency on its data source. If it's getting
 *   data from a vertex job, it depends on the vertex job. If it's getting data
 *   from software, this is null.
 *
 * - The first vertex job used as the input to tiling must depend on the set
 *   value job, if it is present.
 *
 * - Tiler jobs must be strictly ordered. So each tiler job must depend on the
 *   previous job in the chain.
 *
 * - Jobs linking via next_job has no bearing on order of execution, rather it
 *   just establishes the linked list of jobs, EXCEPT:
 *
 * - A job's dependencies must appear earlier in the linked list (job chain).
 *
 * Justification for each rule:
 *
 * - Set value jobs set up tiling, essentially. If tiling occurs, they are
 *   needed; if it does not, we cannot emit them since then tiling partially
 *   occurs and it's bad.
 *
 * - The hardware has no notion of a "vertex/tiler job" (at least not our
 *   hardware -- other revs have fused jobs, but --- crap, this just got even
 *   more complicated). They are independent units that take in data, process
 *   it, and spit out data.
 *
 * - Any job must depend on its data source, in fact, or risk a
 *   read-before-write hazard. Tiler jobs get their data from vertex jobs, ergo
 *   tiler jobs depend on the corresponding vertex job (if it's there).
 *
 * - In fact, tiling depends on the set value job, but tiler jobs depend on the
 *   corresponding vertex jobs and each other, so this rule ensures each tiler
 *   job automatically depends on the set value job.
 *
 * - The tiler is not thread-safe; this dependency prevents race conditions
 *   between two different jobs trying to write to the tiler outputs at the
 *   same time.
 *
 * - Internally, jobs are scoreboarded; the next job fields just form a linked
 *   list to allow the jobs to be read in; the execution order is from
 *   resolving the dependency fields instead.
 *
 * - The hardware cannot set a dependency on a job it doesn't know about yet,
 *   and dependencies are processed in-order of the next job fields.
 *
 */

/* Accessor to set the next job field */

static void
panfrost_set_job_next(struct mali_job_descriptor_header *first, mali_ptr next)
{
        if (first->job_descriptor_size)
                first->next_job_64 = (u64) (uintptr_t) next;
        else
                first->next_job_32 = (u32) (uintptr_t) next;
}

/* Coerce a panfrost_transfer to a header */

static inline struct mali_job_descriptor_header *
job_descriptor_header(struct panfrost_transfer t)
{
        return (struct mali_job_descriptor_header *) t.cpu;
}

static void
panfrost_assign_index(
        struct panfrost_batch *batch,
        struct panfrost_transfer transfer)
{
        /* Assign the index */
        unsigned index = ++batch->job_index;
        job_descriptor_header(transfer)->job_index = index;
}

/* Helper to add a dependency to a job */

static void
panfrost_add_dependency(
        struct panfrost_transfer depender,
        struct panfrost_transfer dependent)
{

        struct mali_job_descriptor_header *first =
                job_descriptor_header(dependent);

        struct mali_job_descriptor_header *second =
                job_descriptor_header(depender);

        /* Look for an open slot */

        if (!second->job_dependency_index_1)
                second->job_dependency_index_1 = first->job_index;
        else if (!second->job_dependency_index_2)
                second->job_dependency_index_2 = first->job_index;
        else
                unreachable("No available slot for new dependency");
}

/* Queues a job WITHOUT updating pointers. Be careful. */

static void
panfrost_scoreboard_queue_job_internal(
        struct panfrost_batch *batch,
        struct panfrost_transfer job)
{
        panfrost_assign_index(batch, job);

        /* Queue a pointer to the job */
        util_dynarray_append(&batch->headers, void*, job.cpu);
        util_dynarray_append(&batch->gpu_headers, mali_ptr, job.gpu);
}


/* Queues a compute job, with no special dependencies. This is a bit of a
 * misnomer -- internally, all job types are queued with this function, but
 * outside of this file, it's for pure compute jobs */

void
panfrost_scoreboard_queue_compute_job(
        struct panfrost_batch *batch,
        struct panfrost_transfer job)
{
        panfrost_scoreboard_queue_job_internal(batch, job);

        /* Update the linked list metadata as appropriate */
        batch->last_job = job;

        if (!batch->first_job.gpu)
                batch->first_job = job;
}

/* Queues a vertex job. There are no special dependencies yet, but if
 * tiling is required (anytime 'rasterize discard' is disabled), we have
 * some extra bookkeeping for later */

void
panfrost_scoreboard_queue_vertex_job(
        struct panfrost_batch *batch,
        struct panfrost_transfer vertex,
        bool requires_tiling)
{
        panfrost_scoreboard_queue_compute_job(batch, vertex);

        if (requires_tiling && !batch->first_vertex_for_tiler.gpu)
                batch->first_vertex_for_tiler = vertex;
}

/* Queues a tiler job, respecting the dependency of each tiler job on the
 * previous */

void
panfrost_scoreboard_queue_tiler_job(
        struct panfrost_batch *batch,
        struct panfrost_transfer tiler)
{
        panfrost_scoreboard_queue_compute_job(batch, tiler);

        if (!batch->first_tiler.gpu)
                batch->first_tiler = tiler;

        if (batch->last_tiler.gpu)
                panfrost_add_dependency(tiler, batch->last_tiler);

        batch->last_tiler = tiler;
}

/* Queues a fused (vertex/tiler) job, or a pair of vertex/tiler jobs if
 * fused jobs are not supported (the default until Bifrost rolls out) */

void
panfrost_scoreboard_queue_fused_job(
        struct panfrost_batch *batch,
        struct panfrost_transfer vertex,
        struct panfrost_transfer tiler)
{
        panfrost_scoreboard_queue_vertex_job(batch, vertex, true);
        panfrost_scoreboard_queue_tiler_job(batch, tiler);
        panfrost_add_dependency(tiler, vertex);
}

/* Queues a fused (vertex/tiler) job prepended *before* the usual set, used for
 * wallpaper blits */

void
panfrost_scoreboard_queue_fused_job_prepend(
        struct panfrost_batch *batch,
        struct panfrost_transfer vertex,
        struct panfrost_transfer tiler)
{
        /* Sanity check */
        assert(batch->last_tiler.gpu);
        assert(batch->first_tiler.gpu);

        /* First, we add the vertex job directly to the queue, forcing it to
         * the front */

        panfrost_scoreboard_queue_job_internal(batch, vertex);
        batch->first_job = vertex;
        batch->first_vertex_for_tiler = vertex;

        /* Similarly, we add the tiler job directly to the queue, forcing it to
         * the front (second place), manually setting the tiler on vertex
         * dependency (since this is pseudofused) and forcing a dependency of
         * the now-second tiler on us (since all tiler jobs are linked in order
         * and we're injecting ourselves at the front) */

        panfrost_scoreboard_queue_job_internal(batch, tiler);
        panfrost_add_dependency(tiler, vertex);
        panfrost_add_dependency(batch->first_tiler, tiler);
        batch->first_tiler = tiler;
}

/* Generates a set value job, used below as part of TILER job scheduling. */

static struct panfrost_transfer
panfrost_set_value_job(struct panfrost_batch *batch, mali_ptr polygon_list)
{
        struct mali_job_descriptor_header job = {
                .job_type = JOB_TYPE_SET_VALUE,
                .job_descriptor_size = 1,
        };

        struct mali_payload_set_value payload = {
                .out = polygon_list,
                .unknown = 0x3,
        };

        struct panfrost_transfer transfer = panfrost_allocate_transient(batch, sizeof(job) + sizeof(payload));
        memcpy(transfer.cpu, &job, sizeof(job));
        memcpy(transfer.cpu + sizeof(job), &payload, sizeof(payload));

        return transfer;
}

/* If there are any tiler jobs, there needs to be a corresponding set value job
 * linked to the first vertex job feeding into tiling. */

static void
panfrost_scoreboard_set_value(struct panfrost_batch *batch)
{
        /* Check if we even need tiling */
        if (!batch->last_tiler.gpu)
                return;

        /* Okay, we do. Let's generate it. We'll need the job's polygon list
         * regardless of size. */

        mali_ptr polygon_list = panfrost_batch_get_polygon_list(batch, 0);

        struct panfrost_transfer job =
                panfrost_set_value_job(batch, polygon_list);

        /* Queue it */
        panfrost_scoreboard_queue_compute_job(batch, job);

        /* Tiler jobs need us */
        panfrost_add_dependency(batch->first_tiler, job);
}

/* Once all jobs have been added to a batch and we're ready to submit, we need
 * to order them to set each of the next_job fields, obeying the golden rule:
 * "A job's dependencies must appear earlier in the job chain than itself".
 * Fortunately, computing this job chain is a well-studied graph theory problem
 * known as "topological sorting", which has linear time algorithms. We let
 * each job represent a node, each dependency a directed edge, and the entire
 * set of jobs to be a dependency graph. This graph is inherently acyclic, as
 * otherwise there are unresolveable dependencies.
 *
 * We implement Kahn's algorithm here to compute the next_job chain:
 * https://en.wikipedia.org/wiki/Topological_sorting#Kahn's_algorithm
 *
 * A few implementation notes: we represent S explicitly with a bitset, L
 * implicitly in the next_job fields. The indices of the bitset are off-by-one:
 * nodes are numbered [0, node_count - 1], whereas in reality job_index in the
 * hardware and dependencies are [1, node_count].
 *
 * We represent edge removal implicitly with another pair of bitsets, rather
 * than explicitly removing the edges, since we need to keep the dependencies
 * there for the hardware.
 */

#define DESCRIPTOR_FOR_NODE(count) \
        *(util_dynarray_element(&batch->headers, \
                struct mali_job_descriptor_header*, count))

#define GPU_ADDRESS_FOR_NODE(count) \
        *(util_dynarray_element(&batch->gpu_headers, \
                mali_ptr, count))

void
panfrost_scoreboard_link_batch(struct panfrost_batch *batch)
{
        /* Finalize the batch */
        panfrost_scoreboard_set_value(batch);

        /* Let no_incoming represent the set S described. */

        unsigned node_count = batch->job_index;

        size_t sz = BITSET_WORDS(node_count) * sizeof(BITSET_WORD);
        BITSET_WORD *no_incoming = calloc(sz, 1);

        /* Sets for edges being removed in dep 1 or 2 respectively */

        BITSET_WORD *edge_removal_1 = calloc(sz, 1);
        BITSET_WORD *edge_removal_2 = calloc(sz, 1);

        /* We compute no_incoming by traversing the batch. Simultaneously, we
         * would like to keep track of a parity-reversed version of the
         * dependency graph. Dependency indices are 16-bit and in practice (for
         * ES3.0, at least), we can guarantee a given node will be depended on
         * by no more than one other nodes. P.f:
         *
         * Proposition: Given a node N of type T, no more than one other node
         * depends on N.
         *
         * If type is SET_VALUE: The only dependency added against us is from
         * the first tiler job, so there is 1 dependent.
         *
         * If type is VERTEX: If there is a tiler node, that tiler node depends
         * on us; if there is not (transform feedback), nothing depends on us.
         * Therefore there is at most 1 dependent.
         *
         * If type is TILER: If there is another TILER job in succession, that
         * node depends on us. No other job type depends on us. Therefore there
         * is at most 1 dependent.
         *
         * If type is FRAGMENT: This type cannot be in a primary chain, so it
         * is irrelevant. Just for kicks, nobody would depend on us, so there
         * are zero dependents, so it holds anyway.
         *
         * TODO: Revise this logic for ES3.1 and above. This result may not
         * hold for COMPUTE/FUSED/GEOMETRY jobs; we might need to special case
         * those. Can FBO dependencies be expressed within a chain?
         * ---
         *
         * Point is, we only need to hold a single dependent, which is a pretty
         * helpful result.
         */

        unsigned *dependents = calloc(node_count, sizeof(unsigned));

        for (unsigned i = 0; i < node_count; ++i) {
                struct mali_job_descriptor_header *node = DESCRIPTOR_FOR_NODE(i);

                unsigned dep_1 = node->job_dependency_index_1;
                unsigned dep_2 = node->job_dependency_index_2;

                /* Record no_incoming info for this node */

                if (!(dep_1 || dep_2))
                        BITSET_SET(no_incoming, i);

                /* Record this node as the dependent of each of its
                 * dependencies */

                if (dep_1) {
                        assert(!dependents[dep_1 - 1]);
                        dependents[dep_1 - 1] = i + 1;
                }

                if (dep_2) {
                        assert(!dependents[dep_2 - 1]);
                        dependents[dep_2 - 1] = i + 1;
                }
        }

        /* No next_job fields are set at the beginning, so L is implciitly the
         * empty set. As next_job fields are filled, L is implicitly set. Tail
         * is the tail of L, however. */

        struct mali_job_descriptor_header *tail = NULL;

        /* We iterate, popping off elements of S. A simple foreach won't do,
         * since we mutate S as we go (even adding elements) */

        unsigned arr_size = BITSET_WORDS(node_count);

        for (unsigned node_n_1 = __bitset_ffs(no_incoming, arr_size);
             (node_n_1 != 0);
             node_n_1 = __bitset_ffs(no_incoming, arr_size)) {

                unsigned node_n = node_n_1 - 1;

                /* We've got a node n, pop it off */
                BITSET_CLEAR(no_incoming, node_n);

                /* Add it to the list */
                struct mali_job_descriptor_header *n =
                        DESCRIPTOR_FOR_NODE(node_n);

                mali_ptr addr = GPU_ADDRESS_FOR_NODE(node_n);

                if (tail) {
                        /* Link us to the last node */
                        panfrost_set_job_next(tail, addr);
                } else {
                        /* We are the first/last node */
                        batch->first_job.cpu = (uint8_t *) n;
                        batch->first_job.gpu = addr;
                }

                tail = n;

                /* Grab the dependent, if there is one */
                unsigned node_m_1 = dependents[node_n];

                if (node_m_1) {
                        unsigned node_m = node_m_1 - 1;

                        struct mali_job_descriptor_header *m =
                                DESCRIPTOR_FOR_NODE(node_m);

                        /* Get the deps, accounting for removal */
                        unsigned dep_1 = m->job_dependency_index_1;
                        unsigned dep_2 = m->job_dependency_index_2;

                        if (BITSET_TEST(edge_removal_1, node_m))
                                dep_1 = 0;

                        if (BITSET_TEST(edge_removal_2, node_m))
                                dep_2 = 0;

                        /* Pretend to remove edges */
                        if (dep_1 == node_n_1) {
                                BITSET_SET(edge_removal_1, node_m);
                                dep_1 = 0;
                        } else if (dep_2 == node_n_1) {
                                BITSET_SET(edge_removal_2, node_m);
                                dep_2 = 0;
                        } else {
                                /* This node has no relevant dependencies */
                                assert(0);
                        }

                        /* Are there edges left? If not, add us to S */
                        bool has_edges = dep_1 || dep_2;

                        if (!has_edges)
                                BITSET_SET(no_incoming, node_m);
                }
        }

        /* Cleanup */
        free(no_incoming);
        free(dependents);
        free(edge_removal_1);
        free(edge_removal_2);

}
