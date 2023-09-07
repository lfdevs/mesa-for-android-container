use rusticl_opencl_gen::*;

use std::borrow::Borrow;
use std::iter::Product;

#[macro_export]
macro_rules! cl_closure {
    (|$obj:ident| $cb:ident($($arg:ident$(,)?)*)) => {
        Box::new(
            unsafe {
                move|$obj| $cb.unwrap()($($arg,)*)
            }
        )
    }
}

macro_rules! cl_callback {
    ($cb:ident {
        $($p:ident : $ty:ty,)*
    }) => {
        #[allow(dead_code)]
        pub type $cb = unsafe extern "C" fn(
            $($p: $ty,)*
        );
    }
}

cl_callback!(
    CreateContextCB {
        errinfo: *const ::std::os::raw::c_char,
        private_info: *const ::std::ffi::c_void,
        cb: usize,
        user_data: *mut ::std::ffi::c_void,
    }
);

cl_callback!(
    DeleteContextCB {
        context: cl_context,
        user_data: *mut ::std::os::raw::c_void,
    }
);

cl_callback!(
    EventCB {
        event: cl_event,
        event_command_status: cl_int,
        user_data: *mut ::std::os::raw::c_void,
    }
);

cl_callback!(
    MemCB {
        memobj: cl_mem,
        user_data: *mut ::std::os::raw::c_void,
    }
);

cl_callback!(
    ProgramCB {
        program: cl_program,
        user_data: *mut ::std::os::raw::c_void,
    }
);

cl_callback!(
    SVMFreeCb {
        queue: cl_command_queue,
        num_svm_pointers: cl_uint,
        svm_pointers: *mut *mut ::std::os::raw::c_void,
        user_data: *mut ::std::os::raw::c_void,
    }
);

// a lot of APIs use 3 component vectors passed as C arrays
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct CLVec<T> {
    vals: [T; 3],
}

impl<T: Copy> CLVec<T> {
    pub fn new(vals: [T; 3]) -> Self {
        Self { vals: vals }
    }

    /// # Safety
    ///
    /// This function is intended for use around OpenCL vectors of size 3.
    /// Most commonly for `origin` and `region` API arguments.
    ///
    /// Using it for anything else is undefined.
    pub unsafe fn from_raw(v: *const T) -> Self {
        Self {
            vals: unsafe { *v.cast() },
        }
    }

    pub fn pixels<'a>(&'a self) -> T
    where
        T: Product<&'a T>,
    {
        self.vals.iter().product()
    }
}

impl CLVec<usize> {
    /// returns the offset of point in linear memory.
    pub fn calc_offset<T: Borrow<Self>>(point: T, pitch: [usize; 3]) -> usize {
        *point.borrow() * pitch
    }

    /// returns the scalar size of the described region in linear memory.
    pub fn calc_size<T: Borrow<Self>>(region: T, pitch: [usize; 3]) -> usize {
        (*region.borrow() - [0, 1, 1]) * pitch
    }

    pub fn calc_offset_size<T1: Borrow<Self>, T2: Borrow<Self>>(
        base: T1,
        region: T2,
        pitch: [usize; 3],
    ) -> (usize, usize) {
        (
            Self::calc_offset(base, pitch),
            Self::calc_size(region, pitch),
        )
    }
}

impl<T: Default + Copy> Default for CLVec<T> {
    fn default() -> Self {
        Self {
            vals: [T::default(); 3],
        }
    }
}

// provides a ton of functions
impl<T> std::ops::Deref for CLVec<T> {
    type Target = [T; 3];

    fn deref(&self) -> &Self::Target {
        &self.vals
    }
}

impl<T> std::ops::DerefMut for CLVec<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.vals
    }
}

impl<T: Copy + std::ops::Add<Output = T>> std::ops::Add for CLVec<T> {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        self + other.vals
    }
}

impl<T: Copy + std::ops::Add<Output = T>> std::ops::Add<[T; 3]> for CLVec<T> {
    type Output = Self;

    fn add(self, other: [T; 3]) -> Self {
        Self {
            vals: [self[0] + other[0], self[1] + other[1], self[2] + other[2]],
        }
    }
}

impl<T: Copy + std::ops::Sub<Output = T>> std::ops::Sub<[T; 3]> for CLVec<T> {
    type Output = Self;

    fn sub(self, other: [T; 3]) -> Self {
        Self {
            vals: [self[0] - other[0], self[1] - other[1], self[2] - other[2]],
        }
    }
}

impl<T> std::ops::Mul for CLVec<T>
where
    T: Copy + std::ops::Mul<Output = T> + std::ops::Add<Output = T>,
{
    type Output = T;

    fn mul(self, other: Self) -> T {
        self * other.vals
    }
}

impl<T> std::ops::Mul<[T; 3]> for CLVec<T>
where
    T: Copy + std::ops::Mul<Output = T> + std::ops::Add<Output = T>,
{
    type Output = T;

    fn mul(self, other: [T; 3]) -> T {
        self[0] * other[0] + self[1] * other[1] + self[2] * other[2]
    }
}

impl<S, T> TryInto<[T; 3]> for CLVec<S>
where
    S: Copy,
    T: TryFrom<S>,
    [T; 3]: TryFrom<Vec<T>>,
{
    type Error = cl_int;

    fn try_into(self) -> Result<[T; 3], cl_int> {
        let vec: Result<Vec<T>, _> = self
            .vals
            .iter()
            .map(|v| T::try_from(*v).map_err(|_| CL_OUT_OF_HOST_MEMORY))
            .collect();
        vec?.try_into().map_err(|_| CL_OUT_OF_HOST_MEMORY)
    }
}

impl<T> From<[T; 3]> for CLVec<T>
where
    T: Copy,
{
    fn from(arr: [T; 3]) -> Self {
        Self::new(arr)
    }
}

#[allow(non_snake_case)]
pub mod IdpAccelProps {
    use rusticl_opencl_gen::cl_bool;
    use rusticl_opencl_gen::cl_device_integer_dot_product_acceleration_properties_khr;
    pub fn new(
        signed_accelerated: cl_bool,
        unsigned_accelerated: cl_bool,
        mixed_signedness_accelerated: cl_bool,
        accumulating_saturating_signed_accelerated: cl_bool,
        accumulating_saturating_unsigned_accelerated: cl_bool,
        accumulating_saturating_mixed_signedness_accelerated: cl_bool,
    ) -> cl_device_integer_dot_product_acceleration_properties_khr {
        cl_device_integer_dot_product_acceleration_properties_khr {
            signed_accelerated,
            unsigned_accelerated,
            mixed_signedness_accelerated,
            accumulating_saturating_signed_accelerated,
            accumulating_saturating_unsigned_accelerated,
            accumulating_saturating_mixed_signedness_accelerated,
        }
    }
}
