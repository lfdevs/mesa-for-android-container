// Copyright 2026 Google
// SPDX-License-Identifier: MIT

use std::os::fd::OwnedFd;

use rustix::event::epoll;
use rustix::event::epoll::CreateFlags;
use rustix::event::epoll::Event;
use rustix::event::epoll::EventData;
use rustix::event::epoll::EventFlags;
use rustix::event::Timespec;
use rustix::io::Errno;

use crate::util::OwnedDescriptor;
use crate::util::Result;
use crate::util::WaitEvent;
use crate::util::WaitTimeout;
use crate::util::WAIT_CONTEXT_MAX;

pub struct WaitContext {
    epoll_ctx: OwnedFd,
}

impl WaitContext {
    pub fn new() -> Result<WaitContext> {
        let epoll = epoll::create(CreateFlags::CLOEXEC)?;
        Ok(WaitContext { epoll_ctx: epoll })
    }

    pub fn add(&mut self, connection_id: u64, descriptor: &OwnedDescriptor) -> Result<()> {
        epoll::add(
            &self.epoll_ctx,
            descriptor,
            EventData::new_u64(connection_id),
            EventFlags::IN,
        )?;
        Ok(())
    }

    pub fn wait(&mut self, timeout: WaitTimeout) -> Result<Vec<WaitEvent>> {
        let mut events_buffer: [epoll::Event; WAIT_CONTEXT_MAX] = [Event {
            flags: EventFlags::IN,
            data: EventData::new_u64(0),
        }; WAIT_CONTEXT_MAX];

        let epoll_timeout: Option<Timespec> = match timeout {
            WaitTimeout::Finite(duration) => Some(duration.try_into()?),
            WaitTimeout::NoTimeout => None, // Indefinite wait
        };

        let num_events = loop {
            match epoll::wait(&self.epoll_ctx, &mut events_buffer, epoll_timeout.as_ref()) {
                Err(Errno::INTR) => (), // Continue loop on EINTR
                result => break result?,
            }
        };

        let events = events_buffer[..num_events]
            .iter()
            .map(|e| {
                let flags: EventFlags = e.flags;
                WaitEvent {
                    connection_id: e.data.u64(),
                    readable: flags.contains(EventFlags::IN),
                    hung_up: flags.contains(EventFlags::HUP),
                }
            })
            .collect();

        Ok(events)
    }

    pub fn delete(&mut self, descriptor: &OwnedDescriptor) -> Result<()> {
        epoll::delete(&self.epoll_ctx, descriptor)?;
        Ok(())
    }
}
