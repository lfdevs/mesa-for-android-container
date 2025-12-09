// Copyright 2020 Red Hat.
// SPDX-License-Identifier: MIT

use crate::ptr::BetterPointer;

#[derive(Default)]
pub struct Properties<T> {
    props: Vec<T>,
}

/// This encapsulates a C property array, where the list is 0 terminated.
impl<T> Properties<T> {
    /// Creates a Properties object copying from the supplied pointer.
    ///
    /// It returns `None` if any property is found twice.
    ///
    /// If `p` is null the saved list of properties will be empty. Otherwise it will be 0
    /// terminated.
    ///
    /// # Safety
    ///
    /// Besides `p` being valid to be dereferenced, it also needs to point to a `T::default()`
    /// terminated array of `T`.
    pub unsafe fn new(mut p: *const T) -> Option<Self>
    where
        T: Copy + Default + PartialEq,
    {
        let mut res = Self::default();
        if !p.is_null() {
            unsafe {
                let mut val = p.read_and_advance();
                while val != T::default() {
                    // Property lists are expected to be small, so no point in using HashSet or
                    // sorting.
                    if res.get(&val).is_some() {
                        return None;
                    }

                    res.props.push(val);
                    res.props.push(p.read_and_advance());
                    val = p.read_and_advance();
                }
            }

            // terminate the array
            res.props.push(T::default());
        }

        Some(res)
    }

    /// Returns the value for the given `key` if existent.
    pub fn get(&self, key: &T) -> Option<&T>
    where
        T: PartialEq,
    {
        self.iter().find_map(|(k, v)| (k == key).then_some(v))
    }

    /// Returns true when there is no property available.
    pub fn is_empty(&self) -> bool {
        self.props.len() <= 1
    }

    pub fn iter(&self) -> impl Iterator<Item = (&T, &T)> {
        // TODO: use array_chuncks once stabilized
        self.props
            .chunks_exact(2)
            .map(|elems| (&elems[0], &elems[1]))
    }

    /// Returns the amount of key/value pairs available.
    pub fn len(&self) -> usize {
        // only valid lengths are all uneven numbers and 0, so division by 2 gives us always the
        // correct result.
        self.props.len() / 2
    }

    /// Returns a slice to the raw buffer.
    ///
    /// It will return an empty slice if `self` was created with a null pointer. A `T::default()`
    /// terminated one otherwise.
    pub fn raw_data(&self) -> &[T] {
        &self.props
    }
}

#[derive(Default)]
pub struct MultiValProperties<T> {
    props: Vec<T>,
    keys_pos: Vec<(usize, usize)>,
}

/// This encapsulates a C property array, where the list is 0 terminated and keys might contain
/// multiple values.
impl<T> MultiValProperties<T> {
    /// Creates a Properties object copying from the supplied pointer.
    ///
    /// It returns `None` if any property is found twice.
    ///
    /// If `p` is null the saved list of properties will be empty. Otherwise it will be 0
    /// terminated.
    ///
    /// # Safety
    ///
    /// Besides `p` being valid to be dereferenced, it also needs to point to a `T::default()`
    /// terminated array of `T`. In addition to that, keys that contain list of values also need to
    /// be `T::default()` terminated.
    ///
    /// # Examples
    ///
    /// ```
    /// let raw_props = [
    ///     KEY_A, VAL_A,
    ///     MULTI_VAL_KEY_A, VAL_C, VAL_D, VAL_E, 0,
    ///     KEY_B, VAL_B,
    ///     0,
    /// ];
    /// let props = MultiValProperties::new(&raw_props, &[MULTI_VAL_KEY_A]).unwrap();
    /// assert_eq!(props.get(KEY_B), Some(&[VAL_B]));
    /// assert_eq!(props.get(MULTI_VAL_KEY_A), SOME(&[VAL_C, VAL_D, VAL_E]));
    /// ```
    pub unsafe fn new(mut p: *const T, multi_val_keys: &[T]) -> Option<Self>
    where
        T: Copy + Default + PartialEq,
    {
        let mut res = Self::default();
        if !p.is_null() {
            // SAFETY: Reading add offset 0 is safe according to the requirements of this function.
            let mut val = unsafe { p.read_and_advance() };
            while val != T::default() {
                if res.get(val).is_some() {
                    return None;
                }

                let start = res.props.len();
                res.props.push(val);

                let mut end = start + 1;
                if multi_val_keys.contains(&val) {
                    loop {
                        // SAFETY: For a multi-value key it's safe to read until we hit a
                        //         T::default()
                        val = unsafe { p.read_and_advance() };
                        res.props.push(val);

                        // We break after pushing the value in order to keep the original values
                        // intact.
                        if val == T::default() {
                            break;
                        }
                        end += 1;
                    }
                } else {
                    // SAFETY: It's safe to read key-value pairs.
                    res.props.push(unsafe { p.read_and_advance() });
                    end += 1;
                }

                res.keys_pos.push((start, end));
                // SAFETY: it's either a new key or T::default, therefore safe to read.
                val = unsafe { p.read_and_advance() };
            }

            // terminate the array
            res.props.push(T::default());
        }

        Some(res)
    }

    /// Returns the values for the given `key` if existent.
    pub fn get(&self, key: T) -> Option<&[T]>
    where
        T: Copy + PartialEq,
    {
        // Property lists are expected to be small, so we can do a lookup each time without hurting
        // perf too much.
        self.iter().find_map(|(k, v)| (k == key).then_some(v))
    }

    /// Returns true when there is no property available.
    pub fn is_empty(&self) -> bool {
        self.props.len() <= 1
    }

    pub fn iter(&self) -> impl Iterator<Item = (T, &[T])>
    where
        T: Copy,
    {
        self.keys_pos.iter().map(|&(start, end)| {
            let key = self.props[start];
            (key, &self.props[start + 1..end])
        })
    }

    /// Returns the amount of key/values pairs available.
    pub fn len(&self) -> usize {
        self.keys_pos.len()
    }

    /// Returns a slice to the raw buffer.
    ///
    /// It will return an empty slice if `self` was created with a null pointer. A `T::default()`
    /// terminated one otherwise.
    pub fn as_raw_slice(&self) -> &[T] {
        &self.props
    }
}
