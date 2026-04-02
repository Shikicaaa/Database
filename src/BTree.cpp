#include "BTree.h"
#include "SlottedPage.h"

void BTree::split_leaf_node(uint32_t old_node_id)
{
    Page* old_raw_page = pager.get_page(old_node_id);
    SlottedPage old_sp(old_raw_page->data);


    uint32_t new_node_id = pager.allocate_new_page();
    Page* new_raw_page = pager.get_page(new_node_id);
    SlottedPage new_sp(new_raw_page->data);

    new_sp.init_as_leaf_node(false);
    old_sp.move_half(&new_sp);


    uint32_t old_next_id = old_sp.header()->right_child_page_id;
    old_sp.header()->right_child_page_id = new_node_id;
    new_sp.header()->right_child_page_id = old_next_id;

    old_raw_page->is_dirty = true;
    new_raw_page->is_dirty = true;

    auto [new_median_key, new_median_row_id] = new_sp.get_first_key_and_row_id();
    insert_into_parent(old_raw_page, old_node_id, new_median_key, new_median_row_id, new_node_id);
    std::cout << "split!";

}

void BTree::split_internal_node(uint32_t old_node_id)
{

    // Creating all cells vector for the old node
    // We will need this to move the right half of the cells to the new node, and also to find the median key to push up
    SlottedPage old_sp(pager.get_page(old_node_id)->data);
    PageHeader* old_h = old_sp.header();
    Page* old_raw_page = pager.get_page(old_node_id);
    std::vector<InternalNodeCell> all_cells;

    uint16_t* pointers = old_sp.get_cell_pointers();
    for(int i = 0; i < old_h->num_cells; i++){
        InternalNodeCell* cell = reinterpret_cast<InternalNodeCell*>(old_raw_page->data + pointers[i]);
        all_cells.push_back(*cell);
    }

    uint32_t old_right_child = old_h->right_child_page_id;

    // Find the median key and the index of the median key
    int total = all_cells.size();
    int median_index = total / 2;
    uint32_t median_key = all_cells[median_index].key;
    uint32_t median_row_id = all_cells[median_index].row_id;

    old_h->right_child_page_id = all_cells[median_index].page_id;

    uint32_t new_node_id = pager.allocate_new_page();
    SlottedPage new_sp(pager.get_page(new_node_id)->data);
    new_sp.init_as_internal_node(false);
    old_h->num_cells = 0;
    old_h->free_start = sizeof(PageHeader);
    old_h->free_end = PAGE_SIZE;

    old_raw_page->is_dirty = true;

    for(int i = 0; i < median_index; i++){
        old_sp.insert_internal_cell(all_cells[i].key, all_cells[i].row_id, all_cells[i].page_id);
    }

    PageHeader* new_h = new_sp.header();
    new_h->right_child_page_id = old_right_child;

    for(int i = median_index + 1; i < total; i++){
        new_sp.insert_internal_cell(all_cells[i].key, all_cells[i].row_id, all_cells[i].page_id);
    }

    insert_into_parent(old_raw_page, old_node_id, median_key, median_row_id, new_node_id);

}

bool BTree::insert_into_leaf(uint32_t leaf_id, uint32_t key, uint32_t row_id, const void *data, uint16_t size)
{
    Page* page = pager.get_page(leaf_id);
    SlottedPage sp(page->data);

    if(sp.insert_record(key, row_id, data, size)){
        page->is_dirty = true;
        return true;
    }
    return false;
}

void BTree::insert_into_parent(Page* old_node, uint32_t old_node_id, uint32_t key, uint32_t row_id, uint32_t new_node_id)
{
    SlottedPage old_sp(old_node->data);
    PageHeader* old_h = old_sp.header();

    /*

    CASE OLD NODE WAS ROOT

    */
    if (old_h->is_root) {
        uint32_t new_root_id = pager.allocate_new_page();
        Page* new_root_page = pager.get_page(new_root_id);
        SlottedPage new_root_sp(new_root_page->data);

        new_root_sp.header()->node_type = INTERNAL;
        new_root_sp.header()->is_root = true;
        new_root_sp.header()->num_cells = 0;
        new_root_sp.header()->free_start = sizeof(PageHeader);
        new_root_sp.header()->free_end = PAGE_SIZE;
        new_root_sp.header()->parent_page_id = 0;

        /*
        We now set keys < then key to the old and >= into the new node.

        We first set right node child, then we set the left child, after that
        we update parents of children
        */
        new_root_sp.header()->right_child_page_id = new_node_id;

        new_root_sp.insert_internal_cell(key, row_id, old_node_id);

        old_h->is_root = false;
        old_h->parent_page_id = new_root_id;

        Page* new_child_page = pager.get_page(new_node_id);
        SlottedPage new_child_sp(new_child_page->data);
        new_child_sp.header()->is_root = false;
        new_child_sp.header()->parent_page_id = new_root_id;

        this->root_page_id = new_root_id;
        if (use_catalog_root) {
            pager.set_catalog_root_page_id(new_root_id);
        } else {
            pager.set_root_page_id(new_root_id);
        }

        old_node->is_dirty = true;
        new_root_page->is_dirty = true;
        new_child_page->is_dirty = true;

        std::cout << "BTREE: ROOT SPLIT DONE! New Root ID: " << new_root_id << std::endl;
        return;
    }

    /*
    CASE 2 OLD NODE HAD A PARENT
    */
    uint32_t parent_id = old_h->parent_page_id;
    Page* parent_page = pager.get_page(parent_id);
    SlottedPage parent_sp(parent_page->data);
    PageHeader* parent_h = parent_sp.header();


    /*
    We check if there is space inside of its parent, if not
    we split parent recursively, but we dont do that as of now
    */

    uint16_t space_needed = sizeof(InternalNodeCell) + sizeof(uint16_t);
    if (parent_h->free_end - parent_h->free_start < space_needed) {
        split_internal_node(parent_id);
        // split_internal_node updated old_h->parent_page_id to the correct new parent
        parent_id = old_h->parent_page_id;
        parent_page = pager.get_page(parent_id);
        parent_sp = SlottedPage(parent_page->data);
        parent_h = parent_sp.header();
    }


    if (parent_h->right_child_page_id == old_node_id) {
        parent_h->right_child_page_id = new_node_id;

        parent_sp.insert_internal_cell(key, row_id, old_node_id);
    }
    else {
        uint16_t* pointers = parent_sp.get_cell_pointers();
        bool found = false;
        for(int i = 0; i < parent_h->num_cells; i++){
            InternalNodeCell* cell = reinterpret_cast<InternalNodeCell*>(parent_page->data + pointers[i]);
            if(cell->page_id == old_node_id){
                cell->page_id = new_node_id;
                found = true;
                break;
            }
        }
        if(!found){
            std::cerr << "ERROR: COULDN'T FIND THE OLD NODE IN THE PARENT DURING INSERT INTO PARENT\n";
            return;
        }
        parent_sp.insert_internal_cell(key, row_id, old_node_id);
    }

    parent_page->is_dirty = true;

    Page* new_node_page = pager.get_page(new_node_id);
    SlottedPage new_node_sp(new_node_page->data);
    new_node_sp.header()->parent_page_id = parent_id;
    new_node_page->is_dirty = true;
}

uint32_t BTree::find_leaf_node(uint32_t leaf_key, uint32_t row_id)
{
    uint32_t current_page_id = root_page_id;

    while(true){
        std::cout << "BTREE: Currently on page: " << current_page_id << std::endl;
        Page* page = pager.get_page(current_page_id);
        SlottedPage sp(page->data);

        PageHeader* h = (PageHeader*)page->data;

        if(h->node_type == LEAF){
            return current_page_id;
        }

        std::cout << "BTREE: Didn't find key at the current page, looking internally...";
        current_page_id = sp.lookup_internal(leaf_key, row_id);
    }
}

BTree::BTree(Pager &p) : pager(p), use_catalog_root(false)
{
    this->root_page_id = p.get_root_page_id();


    if(root_page_id == 0){
        std::cout << "BTREE: Initializing new database...\n";

        uint32_t new_page_id = p.allocate_new_page();
        p.set_root_page_id(new_page_id);
        this->root_page_id = new_page_id;

        Page* page = pager.get_page(new_page_id);
        SlottedPage sp(page->data);
        sp.init_as_leaf_node(true);
    }
}

BTree::BTree(Pager& p, uint32_t explicit_root_page_id, bool is_catalog)
    : pager(p), root_page_id(explicit_root_page_id), use_catalog_root(is_catalog)
{
}

bool BTree::insert(uint32_t key, uint32_t row_id, const void *data, uint16_t size)
{
    uint16_t record_size = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t) + size + sizeof(uint16_t); // key + row_id + size + data + pointer
    if(record_size > PAGE_SIZE - sizeof(PageHeader) - sizeof(uint16_t)){
        std::cerr << "ERROR: Record too large to fit in a page! Size: " << size << std::endl;
        return false;
    }

    const int MAX_RETRIES = 3;

    for(int attempt = 0; attempt < MAX_RETRIES; attempt++){
        uint32_t leaf_node_id = find_leaf_node(key, row_id);

        Page* page = pager.get_page(leaf_node_id);
        SlottedPage sp(page->data);

        if(sp.insert_record(key, row_id, data, size)){
            std::cout << "BTREE: inserting key " << key << " row_id " << row_id << " into node " << leaf_node_id << std::endl;
            page->is_dirty = true;
            return true;
        }

        std::cout << "BTREE: No space in leaf " << leaf_node_id << ", splitting... (attempt " << (attempt + 1) << ")" << std::endl;
        split_leaf_node(leaf_node_id);
    }

    std::cerr << "ERROR: Failed to insert key " << key << " after " << MAX_RETRIES << " split attempts!" << std::endl;
    return false;
}

std::optional<std::vector<char>> BTree::find(uint32_t key, uint32_t row_id)
{
    std::cout << "BTREE: Searching for key " << key << " row_id " << row_id << std::endl;

    uint32_t leaf_node_id = find_leaf_node(key, row_id);

    Page* page = pager.get_page(leaf_node_id);
    SlottedPage sp(page->data);

    std::optional<std::vector<char>> result = sp.get_record(key, row_id, pager);

    if(result.has_value()){
        std::cout << "BTREE: Found record in node " << leaf_node_id << std::endl;
    } else {
        std::cout << "BTREE: Record not found" << std::endl;
    }

    return result;
}

bool BTree::update(uint32_t key, uint32_t row_id, const void *data, uint16_t size)
{
    std::optional<std::vector<char>> old_record = find(key, row_id);
    if (!old_record.has_value()) {
        return false;
    }

    if (!remove(key, row_id)) {
        return false;
    }

    if (insert(key, row_id, data, size)) {
        return true;
    }

    const std::vector<char>& old_data = old_record.value();
    if (!insert(key, row_id, old_data.data(), static_cast<uint16_t>(old_data.size()))) {
        std::cerr << "ERROR: Update failed and rollback insert failed for key " << key << " row_id " << row_id << "\n";
    }

    return false;
}

bool BTree::remove(uint32_t key, uint32_t row_id)
{
    uint32_t leaf_id = find_leaf_node(key,row_id);
    Page* page = pager.get_page(leaf_id);

    SlottedPage sp(page->data);
    if(sp.delete_record(key,row_id, pager)){
        page->is_dirty = true;
        return true;
    }
    return false;
}
