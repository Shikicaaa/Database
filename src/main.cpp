#include "main.h"
#include "Pager.h"
#include "SlottedPage.h"
#include "BTree.h"
#include "Table.h"
#include "Catalog.h"
#include <iostream>

int main()
{
	Pager pager("employees.db", "Employee");
	Catalog catalog(pager);

	Table* employees = catalog.get_table("Employees");
	
	if (employees == nullptr) {
		
		std::vector<ColumnDefinition> schema = {
			{"ID", DataType::INT, true, false, true, 0},
			{"Name", DataType::VARCHAR, false, false, false, 255},
			{"Salary", DataType::NUMBER, false, true, false, 0}
		};
		
		if (catalog.create_table("Employees", schema)) {
			employees = catalog.get_table("Employees");
		}
	} else {
		std::cout << "\nLoaded existing Employees table\n";
	}
	
	if (employees != nullptr) {
		Row row1 = {42, std::string("Andrija"), 120000.0};
		Row row2 = {5, std::string("Kica"), 120000.0};
		Row row3 = {13, std::string("Novak"), 150000.0};
		
		employees->insert_row(row1);
		employees->insert_row(row2);
		employees->insert_row(row3);
		
		// Try to find a row
		auto found = employees->find_row(42);
		if (found.has_value()) {
			std::cout << "Found! Name: " << std::get<std::string>(found.value()[1]) << std::endl;
		}

		employees->remove_row(13);
	}

	Table* homes = catalog.get_table("Homes");

	if(homes == nullptr){
		std::vector<ColumnDefinition> home_schema = {
			{"ID", DataType::INT, true, false, true, 0},
			{"Address", DataType::VARCHAR, false, false, false, 255},
			{"Price", DataType::NUMBER, false, true, false, 0}
		};

		if(catalog.create_table("Homes", home_schema)){
			homes = catalog.get_table("Homes");
		}
	} else {
		std::cout << "\nLoaded existing Homes table\n";
	}

	if(homes != nullptr){
		Row home1 = {42, std::string("Milosa Obilica 23"), 694200.0};
		Row home2 = {6, std::string("Niska 12"), 120000.0};
		Row home3 = {15, std::string("Beogradska 45"), 150000.0};

		homes->insert_row(home1);
		homes->insert_row(home2);
		homes->insert_row(home3);

		homes->update_row(6, {6, std::string("Velikotrnavska 9"), 130000.0});
		homes->remove_row(15);
	}

    return 0;
}

