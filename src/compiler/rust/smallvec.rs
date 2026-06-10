// Copyright © 2022 Collabora, Ltd.
// SPDX-License-Identifier: MIT

#[derive(Clone, Default)]
enum SmallVecImpl<T> {
    #[default]
    None,
    One(T),
    Many(Vec<T>),
}

/// `SmallVec` is an optimized data structure that handles collections of items.
/// It is designed to avoid allocating a `Vec` unless multiple items are present.
///
/// # Variants
///
/// * `None` - Represents an empty collection, no items are stored.
/// * `One(T)` - Stores a single item without allocating a `Vec`.
/// * `Many(Vec<T>)` - Stores multiple items in a heap-allocated `Vec`.
///
/// This helps to reduce the amount of Vec's allocated in the optimization passes.
#[derive(Clone)]
pub struct SmallVec<T>(SmallVecImpl<T>);

// We can't use #[derive(Default)] here because it's not quite smart enough.
// It requires Default to be implemented for T, even though our default enum
// value is None, which doesn't care about the type T.
impl<T> Default for SmallVec<T> {
    fn default() -> Self {
        SmallVec(Default::default())
    }
}

impl<T> SmallVec<T> {
    /// Constructs a new, empty `SmallVec`
    pub fn new() -> SmallVec<T> {
        SmallVec(Default::default())
    }

    /// Adds an item to the `SmallVec`.
    ///
    /// # Arguments
    ///
    /// * `item` - The item to be added.
    ///
    /// # Example
    ///
    /// ```
    /// let mut vec: SmallVec<String> = SmallVec::new();
    /// vec.push("Hello".to_string());
    /// vec.push("World".to_string());
    /// ```
    pub fn push(&mut self, i: T) {
        self.push_mut(i);
    }

    /// Adds an item to the `SmallVec`, returning a reference to it.
    ///
    /// # Arguments
    ///
    /// * `item` - The item to be added.
    ///
    /// # Example
    ///
    /// ```
    /// let mut vec: SmallVec<String> = SmallVec::new();
    /// let item = vec.push_mut("Hello".to_string());
    /// *item += "World";
    /// ```
    pub fn push_mut(&mut self, i: T) -> &mut T {
        // Explicitly borrow once here so we don't confuse the borrow checker
        // thinking self.0 gets mutably borrowed multiple times.
        let imp = &mut self.0;
        match imp {
            SmallVecImpl::None => {
                *imp = SmallVecImpl::One(i);
                match imp {
                    SmallVecImpl::One(i) => i,
                    _ => panic!("Not a One"),
                }
            }
            SmallVecImpl::One(_) => {
                *imp = match std::mem::take(imp) {
                    SmallVecImpl::One(o) => SmallVecImpl::Many(vec![o, i]),
                    _ => panic!("Not a One"),
                };
                match imp {
                    SmallVecImpl::Many(v) => v.last_mut().unwrap(),
                    _ => panic!("Not a Many"),
                }
            }
            SmallVecImpl::Many(v) => {
                // TODO: Replace with v.push_mut() when we update to
                // Rust 1.95.0 or newer.
                v.push(i);
                v.last_mut().unwrap()
            }
        }
    }
}

impl<T> std::ops::Deref for SmallVec<T> {
    type Target = [T];

    fn deref(&self) -> &[T] {
        match &self.0 {
            SmallVecImpl::None => &[],
            SmallVecImpl::One(i) => std::slice::from_ref(i),
            SmallVecImpl::Many(v) => v,
        }
    }
}

impl<T> std::ops::DerefMut for SmallVec<T> {
    fn deref_mut(&mut self) -> &mut [T] {
        match &mut self.0 {
            SmallVecImpl::None => &mut [],
            SmallVecImpl::One(i) => std::slice::from_mut(i),
            SmallVecImpl::Many(v) => v,
        }
    }
}

impl<T> Extend<T> for SmallVec<T> {
    fn extend<I>(&mut self, iter: I)
    where
        I: IntoIterator<Item = T>,
    {
        let mut iter = iter.into_iter();
        loop {
            match &mut self.0 {
                SmallVecImpl::None | SmallVecImpl::One(_) => {
                    if let Some(i) = iter.next() {
                        self.push(i);
                    } else {
                        // We ran out of items
                        return;
                    }
                }
                SmallVecImpl::Many(v) => {
                    v.extend(iter);
                    return;
                }
            }
        }
    }
}

impl<T> From<Vec<T>> for SmallVec<T> {
    fn from(v: Vec<T>) -> SmallVec<T> {
        if v.is_empty() {
            SmallVec(SmallVecImpl::None)
        } else if v.len() == 1 {
            // Hopefully, Rust can fold away most of this based on the
            // `v.len() == 1` check above.
            SmallVec(SmallVecImpl::One(v.into_iter().next().unwrap()))
        } else {
            SmallVec(SmallVecImpl::Many(v))
        }
    }
}

impl<T, const N: usize> From<[T; N]> for SmallVec<T> {
    fn from(i: [T; N]) -> SmallVec<T> {
        i.into_iter().collect()
    }
}

impl<T> FromIterator<T> for SmallVec<T> {
    fn from_iter<I>(iter: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut iter = iter.into_iter();
        let Some(x) = iter.next() else {
            return SmallVec(SmallVecImpl::None);
        };
        let Some(y) = iter.next() else {
            return SmallVec(SmallVecImpl::One(x));
        };
        SmallVec(SmallVecImpl::Many([x, y].into_iter().chain(iter).collect()))
    }
}

impl<T> From<SmallVec<T>> for Vec<T> {
    fn from(sv: SmallVec<T>) -> Vec<T> {
        match sv.0 {
            SmallVecImpl::None => Vec::new(),
            SmallVecImpl::One(i) => vec![i],
            SmallVecImpl::Many(v) => v,
        }
    }
}

enum IntoIterImpl<T> {
    None,
    One(T),
    Many(std::vec::IntoIter<T>),
}

pub struct IntoIter<T>(IntoIterImpl<T>);

impl<T> Iterator for IntoIter<T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        match &mut self.0 {
            IntoIterImpl::None => None,
            IntoIterImpl::One(_) => {
                match std::mem::replace(&mut self.0, IntoIterImpl::None) {
                    IntoIterImpl::One(i) => Some(i),
                    _ => panic!("Not a One"),
                }
            }
            IntoIterImpl::Many(vi) => vi.next(),
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        match &self.0 {
            IntoIterImpl::None => (0, Some(0)),
            IntoIterImpl::One(_) => (1, Some(1)),
            IntoIterImpl::Many(vi) => vi.size_hint(),
        }
    }
}

impl<T> std::iter::ExactSizeIterator for IntoIter<T> {}
impl<T> std::iter::FusedIterator for IntoIter<T> {}

impl<T> IntoIterator for SmallVec<T> {
    type Item = T;
    type IntoIter = IntoIter<T>;

    fn into_iter(self) -> IntoIter<T> {
        let imp = match self.0 {
            SmallVecImpl::None => IntoIterImpl::None,
            SmallVecImpl::One(i) => IntoIterImpl::One(i),
            SmallVecImpl::Many(v) => IntoIterImpl::Many(v.into_iter()),
        };
        IntoIter(imp)
    }
}
