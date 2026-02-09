# Database Project

This is a challenge that I took because I wanted to learn how relational databases work under the hood.
This project will teach me fundamentals of memory management, modern c++, advanced algorithms and structures (BTrees, Indexes, File systems) and compilators because we need to interpret our input so that our engine can give us desired output.

---

## Phases

These are some phases that I will go under(they may be changed because I am not sure how things will work out):

1. Pager - Read Write operations with blocks of fixed size
2. Page layout - How do we write data inside of those blocks of fixed size
3. B Tree - Algorithm that connects pages into a tree and makes our search faster. Probably will implement between 3 and 4 some indexing strategies.
4. Cursor / Table - This is an apstraction of B tree, it will just mask how B tree works
5. Compiler / Interpreter - This will interpret our SQL.

---

## What have I done so far?

I am currently on **phase 1**.

Checklist:

- [ ] Read Page
- [x] Write Page
- [x] Page Cache
- [ ] File Growth
- [x] Page Flush
