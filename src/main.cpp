#include "main.h"
#include "DBClass.h"

int main()
{
	DBClass db("test.db");
	db.insert(1, "Ovo je prva instanca");
	db.insert(2, "Ovo je druga instanca");
	std::cout << db.get(1);
	std::cout << db.get(2);
	std::cout << db.get(3);
	return 0;
}

