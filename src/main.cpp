#include "main.h"
#include "Pager.h"
#include "SlottedPage.h"
#include "BTree.h"

int main()
{
	Pager pager("test.db", "projekat");

	BTree bt(pager);


	Page* p = pager.get_page(1);
	p->is_dirty = true;

    return 0;
}
