#pragma once
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <cstring>

#define MAX_ENTRY_VALUE 128

struct DBRecord {
    int key;
    char value[MAX_ENTRY_VALUE];
};

class DBClass {
    private:
        std::fstream dbFile;
        std::string fileName;

        // KEY - OFFSET
        std::unordered_map<int, std::streampos> index;
    
    private:
        void Load();
        bool CreateDBFile(const std::string& fName);

    public:
        DBClass(const std::string& fName);
        ~DBClass();

        void insert(int key, const std::string& value);
        std::string get(int key);
        bool delete_entry(int key);
};