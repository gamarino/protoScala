# object_tree.py - CPython/protoPython twin of benchmarks/comparable/object_tree.scala:
# build a complete binary tree (depth 16, 131071 objects), path-copy its
# leftmost spine, fold both versions. Prints the result on the last line.
# Result: 131071 278364170 725606090.
import sys

sys.setrecursionlimit(10000)
M = 1000000007


class Leaf:
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value


class Node:
    __slots__ = ("left", "right", "weight")

    def __init__(self, left, right, weight):
        self.left = left
        self.right = right
        self.weight = weight


def build(depth, seed):
    if depth == 0:
        return Leaf(seed % 1000)
    return Node(build(depth - 1, seed * 2 + 1), build(depth - 1, seed * 2 + 2), depth)


def bump(t):
    if isinstance(t, Leaf):
        return Leaf(t.value + 1)
    return Node(bump(t.left), t.right, t.weight)


def count(t):
    if isinstance(t, Leaf):
        return 1
    return 1 + count(t.left) + count(t.right)


def checksum(t):
    if isinstance(t, Leaf):
        return t.value
    return (checksum(t.left) * 31 + checksum(t.right) + t.weight) % M


t = build(16, 0)
t2 = bump(t)
print(count(t), checksum(t), checksum(t2))
