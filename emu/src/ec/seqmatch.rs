// SPDX-License-Identifier: BSD-2-Clause-Patent
//! Python's difflib.SequenceMatcher (no junk, autojunk off), line for
//! line: the EC comparison counts "events in order" the way test_ec.py
//! did, so the port's counts can be checked against it. (util::diff is a
//! longest common subsequence; SequenceMatcher's longest-block recursion
//! can match fewer, and the counts differ.)

use std::collections::HashMap;
use std::hash::Hash;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Tag {
    Equal,
    Replace,
    Delete,
    Insert,
}

pub struct Matcher<'a, T> {
    a: &'a [T],
    b: &'a [T],
    b2j: HashMap<&'a T, Vec<usize>>,
}

impl<'a, T: Eq + Hash> Matcher<'a, T> {
    pub fn new(a: &'a [T], b: &'a [T]) -> Self {
        let mut b2j: HashMap<&T, Vec<usize>> = HashMap::new();
        for (j, x) in b.iter().enumerate() {
            b2j.entry(x).or_default().push(j);
        }
        Matcher { a, b, b2j }
    }

    fn find_longest_match(&self, alo: usize, ahi: usize, blo: usize, bhi: usize) -> (usize, usize, usize) {
        let (a, b) = (self.a, self.b);
        let (mut besti, mut bestj, mut bestsize) = (alo, blo, 0usize);
        let mut j2len: HashMap<usize, usize> = HashMap::new();
        for i in alo..ahi {
            let mut newj2len: HashMap<usize, usize> = HashMap::new();
            if let Some(js) = self.b2j.get(&a[i]) {
                for &j in js {
                    if j < blo {
                        continue;
                    }
                    if j >= bhi {
                        break;
                    }
                    let k = if j > 0 { j2len.get(&(j - 1)).copied().unwrap_or(0) } else { 0 } + 1;
                    newj2len.insert(j, k);
                    if k > bestsize {
                        besti = i + 1 - k;
                        bestj = j + 1 - k;
                        bestsize = k;
                    }
                }
            }
            j2len = newj2len;
        }
        while besti > alo && bestj > blo && a[besti - 1] == b[bestj - 1] {
            besti -= 1;
            bestj -= 1;
            bestsize += 1;
        }
        while besti + bestsize < ahi && bestj + bestsize < bhi && a[besti + bestsize] == b[bestj + bestsize] {
            bestsize += 1;
        }
        (besti, bestj, bestsize)
    }

    pub fn matching_blocks(&self) -> Vec<(usize, usize, usize)> {
        let (la, lb) = (self.a.len(), self.b.len());
        let mut queue = vec![(0, la, 0, lb)];
        let mut blocks = Vec::new();
        while let Some((alo, ahi, blo, bhi)) = queue.pop() {
            let (i, j, k) = self.find_longest_match(alo, ahi, blo, bhi);
            if k > 0 {
                blocks.push((i, j, k));
                if alo < i && blo < j {
                    queue.push((alo, i, blo, j));
                }
                if i + k < ahi && j + k < bhi {
                    queue.push((i + k, ahi, j + k, bhi));
                }
            }
        }
        blocks.sort();
        let (mut i1, mut j1, mut k1) = (0, 0, 0);
        let mut out = Vec::new();
        for (i2, j2, k2) in blocks {
            if i1 + k1 == i2 && j1 + k1 == j2 {
                k1 += k2;
            } else {
                if k1 > 0 {
                    out.push((i1, j1, k1));
                }
                (i1, j1, k1) = (i2, j2, k2);
            }
        }
        if k1 > 0 {
            out.push((i1, j1, k1));
        }
        out.push((la, lb, 0));
        out
    }

    pub fn opcodes(&self) -> Vec<(Tag, usize, usize, usize, usize)> {
        let (mut i, mut j) = (0, 0);
        let mut out = Vec::new();
        for (ai, bj, size) in self.matching_blocks() {
            let tag = if i < ai && j < bj {
                Some(Tag::Replace)
            } else if i < ai {
                Some(Tag::Delete)
            } else if j < bj {
                Some(Tag::Insert)
            } else {
                None
            };
            if let Some(t) = tag {
                out.push((t, i, ai, j, bj));
            }
            i = ai + size;
            j = bj + size;
            if size > 0 {
                out.push((Tag::Equal, ai, i, bj, j));
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn like_python() {
        // difflib.SequenceMatcher(None, "abxcd", "abcd").get_opcodes()
        let a: Vec<char> = "abxcd".chars().collect();
        let b: Vec<char> = "abcd".chars().collect();
        let m = Matcher::new(&a, &b);
        assert_eq!(m.opcodes(), vec![(Tag::Equal, 0, 2, 0, 2), (Tag::Delete, 2, 3, 2, 2), (Tag::Equal, 3, 5, 2, 4)]);
        // SequenceMatcher(None, "qabxcd", "abycdf"): equal ab, replace x->y, equal cd, insert f, delete q
        let a: Vec<char> = "qabxcd".chars().collect();
        let b: Vec<char> = "abycdf".chars().collect();
        let m = Matcher::new(&a, &b);
        assert_eq!(
            m.opcodes(),
            vec![
                (Tag::Delete, 0, 1, 0, 0),
                (Tag::Equal, 1, 3, 0, 2),
                (Tag::Replace, 3, 4, 2, 3),
                (Tag::Equal, 4, 6, 3, 5),
                (Tag::Insert, 6, 6, 5, 6)
            ]
        );
    }
}
