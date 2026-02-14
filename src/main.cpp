#include "main.h"
#include "Pager.h"
#include "SlottedPage.h"
#include "BTree.h"

std::vector<char> create_row(int id, std::string name, std::string last_name, double salary){
	std::vector<char> buffer;

	char* id_bytes = reinterpret_cast<char*>(&id);
	buffer.insert(buffer.end(), id_bytes, id_bytes + sizeof(int));

	uint16_t name_len = name.size();
	uint16_t lastname_len = last_name.size();
	char* name_len_bytes = reinterpret_cast<char*>(&name_len);
	char* lastname_len_bytes = reinterpret_cast<char*>(&lastname_len);

	buffer.insert(buffer.end(), name_len_bytes, name_len_bytes + sizeof(uint16_t));
	buffer.insert(buffer.end(), name.begin(), name.end());

	buffer.insert(buffer.end(), lastname_len_bytes, lastname_len_bytes + sizeof(uint16_t));
	buffer.insert(buffer.end(), last_name.begin(), last_name.end());

	char* salary_bytes = reinterpret_cast<char*>(&salary);
    buffer.insert(buffer.end(), salary_bytes, salary_bytes + sizeof(double));

    return buffer;
}


int main()
{
	Pager pager("test.db", "projekat");

	BTree bt(pager);

	int i = 1;
	std::vector<char> row = create_row(i++, "Rajovic", "Boban", 1234.0+i*2);
	while(bt.insert(i, row.data(), row.size())){
		row = create_row(i++, "Rajovic", "Boban", 1234.0+i*2);
	}
	// bt.insert(10, row.data(), row.size());
	// bt.insert(5, row.data(), row.size());
	// bt.insert(20, row.data(), row.size());
	// bt.insert(7, row.data(), row.size());
	Page* p = pager.get_page(1);
	p->is_dirty = true;

    return 0;
}
