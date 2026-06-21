use std::sync::Arc;
use std::time::Duration;

use crate::device::{
    Device, HwError, SubmitGroup, TimelineSyncobj, VirtualMemory,
};
use crate::invocation::{InvocationInfo, new_invocation_cs};

pub struct TestRunner {
    vm: Arc<VirtualMemory>,
    group: SubmitGroup,
    timeline: TimelineSyncobj,
}

unsafe impl Send for TestRunner {}
unsafe impl Sync for TestRunner {}

impl TestRunner {
    pub fn new(debug: bool) -> Result<Self, HwError> {
        let dev = Device::new(debug)?;
        let vm = Arc::new(VirtualMemory::new(dev.clone())?);
        let group = SubmitGroup::new(&vm)?;
        let timeline = TimelineSyncobj::new(dev)?;
        Ok(TestRunner {
            vm,
            group,
            timeline,
        })
    }

    pub fn gpu_id(&self) -> u64 {
        self.vm.dev().props().gpu_id
    }

    pub fn run(&self, invoc: InvocationInfo) -> Result<(), HwError> {
        // Create the command stream
        let cs = new_invocation_cs(&self.vm, &invoc)?;

        let moment = self.timeline.signal();
        // Submit the command stream for execution
        self.group.submit(
            cs.cs_device_addr(),
            cs.cs_len as u32,
            &[moment.into()],
        )?;
        self.group.check_state()?;

        // Wait for it to finish
        moment.wait(Duration::MAX)?;
        self.group.check_state()?;

        // Copy the data back
        invoc.data.copy_from_slice(cs.read_host_data());

        Ok(())
    }
}
