#include "DBClass.h"

DBClass::DBClass(const std::string& fName) {

    if(!CreateDBFile(fName)){
        std::cerr << "[DATABASE]: Error while initializing database.";
        return;
    }

    Load();
    std::cout << "[DATABASE]: Loaded successfully " << index.size() << " entries." << std::endl;

}

bool DBClass::CreateDBFile(const std::string& fName){
    this->fileName = fName;
    dbFile.open(fileName, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    
    if(!dbFile.is_open()) {
        dbFile.clear();
        dbFile.open(fileName, std::ios::out | std::ios::binary);
        dbFile.close();
        dbFile.open(fileName, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    }

    if (!dbFile.is_open()) {
        std::cerr << "[ERROR]: Can't open file!" << std::endl;
        return false;
    }
    return true;
}

void DBClass::Load()
{
    //Treba da ga ucita i da svaki offset ubaci u ove indekse.
    dbFile.clear();
    dbFile.seekg(0, std::ios::beg);

    std::streampos position = dbFile.tellg();
    while (dbFile.tellg() != std::fstream::pos_type(-1) && !dbFile.eof())
    {
        DBRecord record;
        if(dbFile.read(reinterpret_cast<char*>(&record), sizeof(record)))
            this->index[record.key] = position;
        
        position = dbFile.tellg();
    }

    dbFile.clear();
}

DBClass::~DBClass()
{
    if (dbFile.is_open()) {
        dbFile.close();
    }
}

void DBClass::insert(int key, const std::string& value) {

    if(index.find(key) != index.end()){
        std::cerr << "[ERROR]: Duplicate key found, skipping insertion.";
        return;
    }

    DBRecord record;
    record.key = key;

    std::strncpy(record.value, value.c_str(), MAX_ENTRY_VALUE - 1);
    record.value[MAX_ENTRY_VALUE - 1] = '\0';
    
    dbFile.clear();
    dbFile.seekp(0, std::ios::end);
    std::streampos position = dbFile.tellp();

    dbFile.write(reinterpret_cast<char*>(&record), sizeof(DBRecord));
    dbFile.flush();
    
    index[key] = position;
    
    std::cout << "[DATABASE]: Inserted key " << key << " at pos " << position << std::endl;
}

std::string DBClass::get(int key) {
    if (index.find(key) == index.end()) {
        return "[DATABASE]: Error, key doesnt exist.\n";
    }

    std::streampos position = index[key];
    
    dbFile.clear();
    dbFile.seekg(position);
    
    DBRecord record;
    dbFile.read(reinterpret_cast<char*>(&record), sizeof(DBRecord));

    std::string result = "[DATABASE]: ";
    result += record.value;
    result += "\n";
    
    return result;
}

bool DBClass::delete_entry(int key){
    if (index.find(key) == index.end()){
        std::cerr << "[ERROR]: Entry with key " << key << " not found!";
        return false;
    }

    dbFile.clear();
    // EDGE CASE
    // If it's the only one in the array just create another empty one
    if(index.size() == 1){
        std::remove(fileName.c_str());
        CreateDBFile(fileName);
    }

    std::streampos write_pos = index[key];
    std::streampos read_pos = index[key];
    // setting get and put pointer to the start index of our entry
    DBRecord record;

    dbFile.seekg(index[key]);
    dbFile.seekp(index[key]);

    // we move now the get pointer to the next position so its always ahead of put pointer.
    dbFile.read(reinterpret_cast<char*>(&record), sizeof(DBRecord));

    while(dbFile.read(reinterpret_cast<char*>(&record), sizeof(DBRecord))){
        dbFile.write(reinterpret_cast<char*>(&record), sizeof(DBRecord));
        dbFile.flush();
    }

    dbFile.clear();



}