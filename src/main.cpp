#include "main.h"
#include "Pager.h"
#include "SlottedPage.h"
#include "BTree.h"
#include "Table.h"

int main()
{
	Pager pager("employees.db", "Employee");

	BTree bt(pager);

	std::vector<ColumnDefinition> schema = {
		{"ID", DataType::INT, true, false, true, 0},
		{"Name", DataType::VARCHAR, false, false, false, 255},
		{"Salary", DataType::NUMBER, false, true, false, 0}
	};

	Table table("Employees", schema, bt);

	Row row1 = {42, std::string("Andrija"), 120000.0};
	Row row2 = {5, std::string("Matija"), 120000.0};
	Row row3 = {13, std::string("Novak"), 150000.0};

	table.insert_row(row1);
	table.insert_row(row2);
	table.insert_row(row3);

	Page* p = pager.get_page(1);
	p->is_dirty = true;

    return 0;
}

