Smallest unit that can be written or read is a **page**. However we can only make changes to the empty **memory cells**. Smallest entity to erase is a **block**. Pages in an empty block have to be written sequentially.
**Cells** can hold 1-2b of data.
**Page** can hold 2 to 6 Kb of data.
**Block** holds from 64 to 512 pages.
**Planes** - **Blocks** are organized into **planes**.
**Planes** are placed on a **Die**.
**SSD** can have 1+ **dies**.
SO: *die* > *plane* > *block* > *page* > *cell*

When we do splitting, we split lower half to the new node and keep other half inside "old" node. Both nodes now have a parent that was always above them. Also all data is inside leaf nodes. 


23.3
I have to implement database versioning and schema versioning to support data migration and schema update.
This will be done by adding additional options in arguments, such as schema_version and num_columns.
This will require the following thing?:
* Save schema
* Verision the schema
* Iterator for columns
* Support migrations