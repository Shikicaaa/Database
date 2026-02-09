#include "main.h"
#include "Pager.h"
#include "SlottedPage.h"

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
	Pager pager("test.db");

    Page* page0 = pager.get_page(0);

    SlottedPage sp(page0->data);

    std::vector<char> row1 = create_row(1, "Ana", "Ivanovic", 1500.0);
    std::vector<char> row2 = create_row(2, "Rajovic", "Boban", 6420.0);

    if (sp.insert_record(1, row1.data(), row1.size())) {
        std::cout << "Row1" << std::endl;
    } else {
        std::cout << "No Space for row1" << std::endl;
    }

    if (sp.insert_record(2, row2.data(), row2.size())) {
        std::cout << "Row2" << std::endl;
    }else{
        std::cout << "No Space for row2" << std::endl;

	}

    page0->is_dirty = true;

    return 0;
}
