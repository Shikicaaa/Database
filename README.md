# Database Project

This is a challenge that I took because I wanted to learn how relational databases work under the hood.
This project will teach me fundamentals of memory management, modern C++, advanced algorithms and structures (BTrees, Indexes, File systems) and compilers, because we need to interpret our input so that our engine can give us the desired output.

## What is done

### SQL Parser

I've made a lexer and a parser that support a subset of SQL keywords like: `SELECT`, `INSERT`, `UPDATE`, `DELETE`, `CREATE TABLE`, `CREATE INDEX`, `DROP TABLE`, `DROP INDEX`, `ALTER TABLE` (`ADD`/`DROP`/`RENAME COLUMN`), `BEGIN`/`COMMIT`/`ROLLBACK`.
I've also added support for joins: `JOIN` (`INNER`, `LEFT`, `RIGHT`, `FULL OUTER`) and `WHERE` conditions with supported datatypes such as: `INT`, `VARCHAR(n)`, `NUMBER`, `BOOLEAN`, `DATE`.
Also an important thing is that I've added column restrictions such as: `PRIMARY KEY`, `UNIQUE`, `NULLABLE`, `REFERENCES` (foreign key).

### Query Engine

The query engine consists of a Logical and Physical planner that plan the queries depending on the parsed input. The query engine follows the Volcano/Iterator model of execution: every operator (`SeqScanOperator`, `IndexScanOperator`, `SecondaryIndexScanOperator`, `FilterOperator`, `ProjectOperator`, `JoinOperator`, `InsertOperator`, `UpdateOperator`, `DeleteOperator`, `ValuesOperator`) exposes an `Init()`/`Next()` interface and pulls rows one at a time from its child operator(s), so plans compose into a tree that gets pulled from the root down.

Where possible, the planner rewrites a plain table scan into an index-based scan instead - using the primary key B-Tree directly for point lookups on the PK, or a secondary index B-Tree when the `WHERE` column has one, instead of falling back to a full sequential scan.

### Storage Engine

Underneath the query engine sits a page-based storage engine: a `Pager` handles fixed-size block reads/writes to the database file with an in-memory `PageCache` (CLOCK eviction) sitting in front of it, and a `SlottedPage` layout organizes variable-length records within each page, including overflow pages for records too large to fit inline. Tables and indexes are both backed by the same `BTree` implementation, with `Table`/`Cursor` providing a row-level abstraction on top so the rest of the engine never has to think in raw bytes.

### Transactions & Durability

Every statement goes through a Write-Ahead Log (`WALManager`) - `BEGIN`/`COMMIT`/`ROLLBACK` and `INSERT`/`UPDATE`/`DELETE` are all logged before they're applied, whether the user wrapped them in an explicit transaction or not (implicit auto-commit per statement). On startup, the WAL is replayed to redo committed work and undo anything left behind by a crash. Table-level shared/exclusive locking is layered on top for transaction isolation.

### CLI

An interactive command-line front end (built on GNU Readline for history/line-editing) with a few extra helper commands on top of plain SQL: `/schema <table>` to inspect a table's columns, and `/showme` (or `/sm`, `/gimme`) to list all tables and indexes currently in the catalog.

---

## Phases

These are some phases that I will go through (they may change, because I am not sure how things will work out):

1. **Pager** - Read/write operations with blocks of fixed size
2. **Page layout** - How we write data inside those fixed-size blocks
3. **B-Tree** - Algorithm that connects pages into a tree and makes our search faster. Probably will implement some indexing strategies between phases 3 and 4.
4. **Cursor / Table** - An abstraction over the B-Tree; it just masks how the B-Tree works underneath
5. **Compiler / Interpreter** - This will interpret our SQL
6. **Query Planner + Joins**
7. **WAL and transactions** - Logging technique that will alow us the finally have the ACID properties.
8. **Advanced SQL keyword subset** - For example `LIKE`, support the wildcards and `%`, `_`, and implement things like `%xx%` contains, or `starts with` and `ends with` combinations with wildcards; implement `BETWEEN`, aggregation functions, `GROUP BY` etc... 

---

## What have I done so far?

I am currently on **phase 7**.

Checklist:

- [x] Read Page
- [x] Write Page
- [x] Page Cache
- [x] File Growth
- [x] Page Flush
- [x] BTree insert
- [x] BTree lookup
- [x] BTree split
- [x] Table class and schema
- [x] Schema serializer
- [x] Implement query planner
- [x] Implement joins (left, outer, inner, right)
- [x] Better UI
- [x] WAL implementation
- [ ] LIKE keyword
- [ ] Hash Join, Merge Join...
- [ ] Client-Server structure

---

## Platform Support

The core engine itself is standard C++ (STL containers, `<variant>`, `<optional>`, `std::shared_mutex`, `std::fstream`, etc.), but the CLI (`main.cpp`) depends on **GNU Readline** (`<readline/readline.h>`, `<readline/history.h>`) for interactive input, which limits where it runs out of the box, but will look into implementing my own library so it can run anywhere.:

| Platform | Support | Notes |
|---|---|---|
| **Linux** | Works | Readline is readily available (`libreadline-dev` / `readline-devel`) |
| **macOS** | Works (with setup) | The system-provided `libedit` shim isn't a full replacement; install real Readline via Homebrew (`brew install readline`) and link against it explicitly |
| **Windows (native/MSVC)** | Not supported | GNU Readline isn't natively available on Windows |
| **Windows with WSL** | Works | Runs like Linux inside a WSL distro |
| **Windows (MinGW/Cygwin)** | Not tested | Haven't been tried with this project, but if someone is willing to take one for the team I won't stop you |

*Tested on: `[TODO: e.g. Ubuntu 22.04, g++ 11.4]`*

## Building & Running

This project uses **CMake** as its build system.

### Prerequisites

- A C++17-capable compiler (GCC 9+, Clang 9+)
- CMake (3.11 or higher)
- GNU Readline development package

```bash
# Ubuntu / Debian
sudo apt install build-essential libreadline-dev

# Fedora
sudo dnf install gcc-c++ readline-devel

# macOS (Homebrew)
brew install readline
```

### Compile

```bash
mkdir build
cd build
cmake ..
make
```
For mac users, CMakes find_library should locate Readline instaled with homebrew. If it fails try to pass the path during the cmake step.

### Run

```bash
./shikidb
# optional log level: debug | warning | error | panic
./shikidb --loglevel debug
```

The database persists to `database.db` (data) and `database.wal` (write-ahead log) in the working directory.

## Usage

```sql
CREATE TABLE Employees (
    ID     INT PRIMARY KEY,
    Name   VARCHAR(255),
    Salary NUMBER NULLABLE
);

INSERT INTO Employees VALUES (1, 'Ana', 95000);

SELECT * FROM Employees WHERE ID = 1;

UPDATE Employees SET Salary = 100000 WHERE ID = 1;

DELETE FROM Employees WHERE ID = 1;
```

Transactions:

```sql
BEGIN TRANSACTION;
INSERT INTO Employees VALUES (2, 'Marko', 80000);
COMMIT;
```

CLI-only commands (no trailing `;`):

```
/schema Employees   -- show a table's schema
/showme             -- list all tables and indexes
/showme tables      -- list only tables
/showme indexes     -- list only indexes
exit | quit         -- exit
```