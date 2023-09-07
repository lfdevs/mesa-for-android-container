use crate::api::icd::*;
use crate::core::context::*;
use crate::core::device::*;
use crate::core::event::*;
use crate::impl_cl_type_trait;

use mesa_rust::pipe::context::PipeContext;
use mesa_rust_util::properties::*;
use rusticl_opencl_gen::*;

use std::collections::HashSet;
use std::sync::mpsc;
use std::sync::Arc;
use std::sync::Mutex;
use std::thread;
use std::thread::JoinHandle;

struct QueueState {
    pending: Vec<Arc<Event>>,
    last: Option<Arc<Event>>,
}

pub struct Queue {
    pub base: CLObjectBase<CL_INVALID_COMMAND_QUEUE>,
    pub context: Arc<Context>,
    pub device: &'static Device,
    pub props: cl_command_queue_properties,
    pub props_v2: Option<Properties<cl_queue_properties>>,
    state: Mutex<QueueState>,
    _thrd: Option<JoinHandle<()>>,
    chan_in: mpsc::Sender<Vec<Arc<Event>>>,
}

impl_cl_type_trait!(cl_command_queue, Queue, CL_INVALID_COMMAND_QUEUE);

fn flush_events(evs: &mut Vec<Arc<Event>>, pipe: &PipeContext) {
    if !evs.is_empty() {
        pipe.flush().wait();
        evs.drain(..).for_each(|e| e.signal());
    }
}

impl Queue {
    pub fn new(
        context: Arc<Context>,
        device: &'static Device,
        props: cl_command_queue_properties,
        props_v2: Option<Properties<cl_queue_properties>>,
    ) -> CLResult<Arc<Queue>> {
        // we assume that memory allocation is the only possible failure. Any other failure reason
        // should be detected earlier (e.g.: checking for CAPs).
        let pipe = device.screen().create_context().unwrap();
        let (tx_q, rx_t) = mpsc::channel::<Vec<Arc<Event>>>();
        Ok(Arc::new(Self {
            base: CLObjectBase::new(),
            context: context,
            device: device,
            props: props,
            props_v2: props_v2,
            state: Mutex::new(QueueState {
                pending: Vec::new(),
                last: None,
            }),
            _thrd: Some(
                thread::Builder::new()
                    .name("rusticl queue thread".into())
                    .spawn(move || loop {
                        let r = rx_t.recv();
                        if r.is_err() {
                            break;
                        }

                        let new_events = r.unwrap();
                        let mut flushed = Vec::new();

                        for e in new_events {
                            // If we hit any deps from another queue, flush so we don't risk a dead
                            // lock.
                            if e.deps.iter().any(|ev| ev.queue != e.queue) {
                                flush_events(&mut flushed, &pipe);
                            }

                            // We have to wait on user events or events from other queues.
                            let err = e
                                .deps
                                .iter()
                                .filter(|ev| ev.is_user() || ev.queue != e.queue)
                                .map(|e| e.wait())
                                .find(|s| *s < 0);

                            if let Some(err) = err {
                                // If a dependency failed, fail this event as well.
                                e.set_user_status(err);
                                continue;
                            }

                            e.call(&pipe);

                            if !e.is_user() {
                                flushed.push(e);
                            } else {
                                // On each user event we flush our events as application might
                                // wait on them before signaling user events.
                                flush_events(&mut flushed, &pipe);

                                // Wait on user events as they are synchronization points in the
                                // application's control.
                                e.wait();
                            }
                        }

                        flush_events(&mut flushed, &pipe);
                    })
                    .unwrap(),
            ),
            chan_in: tx_q,
        }))
    }

    pub fn queue(&self, e: Arc<Event>) {
        self.state.lock().unwrap().pending.push(e);
    }

    pub fn flush(&self, wait: bool) -> CLResult<()> {
        let mut state = self.state.lock().unwrap();

        // Update last if and only if we get new events, this prevents breaking application code
        // doing things like `clFlush(q); clFinish(q);`
        if let Some(last) = state.pending.last() {
            state.last = Some(last.clone());
        }

        // This should never ever error, but if it does return an error
        self.chan_in
            .send((state.pending).drain(0..).collect())
            .map_err(|_| CL_OUT_OF_HOST_MEMORY)?;
        if wait {
            // Waiting on the last event is good enough here as the queue will process it in order,
            // also take the value so we can release the event once we are done
            state.last.take().map(|e| e.wait());
        }
        Ok(())
    }

    pub fn dependencies_for_pending_events(&self) -> HashSet<Arc<Queue>> {
        let state = self.state.lock().unwrap();

        let mut queues = Event::deep_unflushed_queues(&state.pending);
        queues.remove(self);
        queues
    }
}

impl Drop for Queue {
    fn drop(&mut self) {
        // when deleting the application side object, we have to flush
        // From the OpenCL spec:
        // clReleaseCommandQueue performs an implicit flush to issue any previously queued OpenCL
        // commands in command_queue.
        // TODO: maybe we have to do it on every release?
        let _ = self.flush(true);
    }
}
