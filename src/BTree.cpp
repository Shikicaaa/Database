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

    uint32_t new_median_key = new_sp.get_first_key();
    insert_into_parent(old_raw_page, old_node_id, new_median_key, new_node_id);
    std::cout << "split!";
    
}

void BTree::split_internal_node(uint32_t old_node_id)
{
    return;
}

void BTree::insert_into_leaf(uint32_t leaf_id, uint32_t key, const void *data, uint16_t size)
{
    return;
}

void BTree::insert_into_parent(Page* old_node, uint32_t old_node_id, uint32_t key, uint32_t new_node_id)
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

        new_root_sp.insert_internal_cell(key, old_node_id);

        old_h->is_root = false;
        old_h->parent_page_id = new_root_id;

        Page* new_child_page = pager.get_page(new_node_id);
        SlottedPage new_child_sp(new_child_page->data);
        new_child_sp.header()->is_root = false;
        new_child_sp.header()->parent_page_id = new_root_id;

        this->root_page_id = new_root_id;
        pager.set_root_page_id(new_root_id);

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
    

    if (parent_h->right_child_page_id == old_node_id) {
        parent_h->right_child_page_id = new_node_id;
        
        parent_sp.insert_internal_cell(key, old_node_id);
    } 
    else {
        // TODO: Implement middle internal split.
        std::cout << "Middle internal split not implemented\n";
    }
    
    parent_page->is_dirty = true;

    Page* new_node_page = pager.get_page(new_node_id);
    SlottedPage new_node_sp(new_node_page->data);
    new_node_sp.header()->parent_page_id = parent_id;
    new_node_page->is_dirty = true;
}

uint32_t BTree::find_leaf_node(uint32_t leaf_key)
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
        current_page_id = sp.lookup_internal(leaf_key);
    }
}

BTree::BTree(Pager &p) : pager(p)
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

bool BTree::insert(uint32_t key, const void *data, uint16_t size)
{
    uint32_t leaf_node_id = find_leaf_node(key);

    Page* page = pager.get_page(leaf_node_id);
    SlottedPage sp(page->data);

    if(sp.insert_record(key, data, size)){
        std::cout << "BTREE: inserting key " << key << "into node " << leaf_node_id << std::endl;
        return true;
    }

    split_leaf_node(leaf_node_id);

    // TODO: RECURSIVELY CALL SPLIT IF ROOT IS FULL.
    return false;
}

