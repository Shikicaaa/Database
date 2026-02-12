#include "BTree.h"
#include "SlottedPage.h"

void BTree::split_leaf_node(uint32_t old_node_id)
{
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
    uint32_t leaf_to_insert = root_page_id; 

    Page* page = pager.get_page(leaf_to_insert);
    SlottedPage sp(page->data);

    if (sp.insert_record(key, data, size)) {
        return true;
    }

    split_leaf_node(leaf_to_insert);

    // TODO: RECURSIVELY CALL SPLIT IF ROOT IS FULL.
    return false;
}

